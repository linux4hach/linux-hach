/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include "atmel_hlcdc_dc.h"

static void
atmel_hlcdc_layer_fb_flip_release(struct drm_flip_work *work, void *val)
{
	struct atmel_hlcdc_layer_fb_flip *flip = val;

	if (flip->fb)
		drm_framebuffer_unreference(flip->fb);
	kfree(flip);
}

static void
atmel_hlcdc_layer_fb_flip_destroy(struct atmel_hlcdc_layer_fb_flip *flip)
{
	if (flip->fb)
		drm_framebuffer_unreference(flip->fb);
	kfree(flip->task);
	kfree(flip);
}

static void
atmel_hlcdc_layer_fb_flip_release_queue(struct atmel_hlcdc_layer *layer,
					struct atmel_hlcdc_layer_fb_flip *flip)
{
	int i;

	if (!flip)
		return;

	for (i = 0; i < layer->max_planes; i++) {
		if (!flip->dscrs[i])
			break;

		flip->dscrs[i]->status = 0;
		flip->dscrs[i] = NULL;
	}

	drm_flip_work_queue_task(&layer->gc, flip->task);
	drm_flip_work_commit(&layer->gc, layer->wq);
}

static void atmel_hlcdc_layer_update_reset(struct atmel_hlcdc_layer *layer,
					   int id)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_update_slot *slot;

	if (id < 0 || id > 1)
		return;

	slot = &upd->slots[id];
	bitmap_clear(slot->updated_configs, 0, layer->desc->nconfigs);
	memset(slot->configs, 0,
	       sizeof(*slot->configs) * layer->desc->nconfigs);

	if (slot->fb_flip) {
		atmel_hlcdc_layer_fb_flip_release_queue(layer, slot->fb_flip);
		slot->fb_flip = NULL;
	}
}

static void atmel_hlcdc_layer_update_apply(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	const struct atmel_hlcdc_layer_desc *desc = layer->desc;
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct regmap *regmap = layer->hlcdc->regmap;
	struct atmel_hlcdc_layer_update_slot *slot;
	struct atmel_hlcdc_layer_fb_flip *fb_flip;
	struct atmel_hlcdc_dma_channel_dscr *dscr;
	unsigned int cfg;
	u32 action = 0;
	int i = 0;

	if (upd->pending < 0 || upd->pending > 1 ||
	    dma->status == ATMEL_HLCDC_LAYER_DISABLING)
		return;

	slot = &upd->slots[upd->pending];

	for_each_set_bit(cfg, slot->updated_configs, layer->desc->nconfigs) {
		regmap_write(regmap,
			     desc->regs_offset +
			     ATMEL_HLCDC_LAYER_CFG(layer, cfg),
			     slot->configs[cfg]);
		action |= ATMEL_HLCDC_LAYER_UPDATE;
	}

	fb_flip = slot->fb_flip;

	if (!fb_flip->fb)
		goto apply;

	if (dma->status == ATMEL_HLCDC_LAYER_DISABLED) {
		for (i = 0; i < fb_flip->ngems; i++) {
			dscr =  fb_flip->dscrs[i];
			dscr->ctrl = ATMEL_HLCDC_LAYER_DFETCH |
				     ATMEL_HLCDC_LAYER_DMA_IRQ |
				     ATMEL_HLCDC_LAYER_ADD_IRQ |
				     ATMEL_HLCDC_LAYER_DONE_IRQ;

			regmap_write(regmap,
				     desc->regs_offset +
				     ATMEL_HLCDC_LAYER_PLANE_ADDR(i),
				     dscr->addr);
			regmap_write(regmap,
				     desc->regs_offset +
				     ATMEL_HLCDC_LAYER_PLANE_CTRL(i),
				     dscr->ctrl);
			regmap_write(regmap,
				     desc->regs_offset +
				     ATMEL_HLCDC_LAYER_PLANE_NEXT(i),
				     dscr->next);
		}

		action |= ATMEL_HLCDC_LAYER_DMA_CHAN;
		dma->status = ATMEL_HLCDC_LAYER_ENABLED;
	} else {
		for (i = 0; i < fb_flip->ngems; i++) {
			dscr =  fb_flip->dscrs[i];
			dscr->ctrl = ATMEL_HLCDC_LAYER_DFETCH |
				     ATMEL_HLCDC_LAYER_DMA_IRQ |
				     ATMEL_HLCDC_LAYER_DSCR_IRQ |
				     ATMEL_HLCDC_LAYER_DONE_IRQ;

			regmap_write(regmap,
				     desc->regs_offset +
				     ATMEL_HLCDC_LAYER_PLANE_HEAD(i),
				     dscr->next);
		}

		action |= ATMEL_HLCDC_LAYER_A2Q;
	}

	/* Release unneeded descriptors */
	for (i = fb_flip->ngems; i < layer->max_planes; i++) {
		fb_flip->dscrs[i]->status = 0;
		fb_flip->dscrs[i] = NULL;
	}

	dma->queue = fb_flip;
	slot->fb_flip = NULL;

apply:
	if (action)
		regmap_write(regmap,
			     desc->regs_offset + ATMEL_HLCDC_LAYER_CHER,
			     action);

	atmel_hlcdc_layer_update_reset(layer, upd->pending);

	upd->pending = -1;
}

void atmel_hlcdc_layer_irq(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	const struct atmel_hlcdc_layer_desc *desc = layer->desc;
	struct regmap *regmap = layer->hlcdc->regmap;
	struct atmel_hlcdc_layer_fb_flip *flip;
	unsigned long flags;
	unsigned int isr, imr;
	unsigned int status;
	unsigned int plane_status;
	u32 flip_status;

	int i;

	regmap_read(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_IMR, &imr);
	regmap_read(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_ISR, &isr);
	status = imr & isr;
	if (!status)
		return;

	spin_lock_irqsave(&layer->lock, flags);

	flip = dma->queue ? dma->queue : dma->cur;

	if (!flip) {
		spin_unlock_irqrestore(&layer->lock, flags);
		return;
	}

	flip_status = 0;
	for (i = 0; i < flip->ngems; i++) {
		plane_status = (status >> (8 * i));

		if (plane_status &
		    (ATMEL_HLCDC_LAYER_ADD_IRQ |
		     ATMEL_HLCDC_LAYER_DSCR_IRQ) &
		    ~flip->dscrs[i]->ctrl) {
			flip->dscrs[i]->status |=
					ATMEL_HLCDC_DMA_CHANNEL_DSCR_LOADED;
			flip->dscrs[i]->ctrl |=
					ATMEL_HLCDC_LAYER_ADD_IRQ |
					ATMEL_HLCDC_LAYER_DSCR_IRQ;
		}

		if (plane_status &
		    ATMEL_HLCDC_LAYER_DONE_IRQ &
		    ~flip->dscrs[i]->ctrl) {
			flip->dscrs[i]->status |=
					ATMEL_HLCDC_DMA_CHANNEL_DSCR_DONE;
			flip->dscrs[i]->ctrl |=
					ATMEL_HLCDC_LAYER_DONE_IRQ;
		}

		if (plane_status & ATMEL_HLCDC_LAYER_OVR_IRQ)
			flip->dscrs[i]->status |=
					ATMEL_HLCDC_DMA_CHANNEL_DSCR_OVERRUN;

		flip_status |= flip->dscrs[i]->status;
	}

	/* Get changed bits */
	flip_status ^= flip->status;
	flip->status |= flip_status;

	if (flip_status & ATMEL_HLCDC_DMA_CHANNEL_DSCR_LOADED) {
		atmel_hlcdc_layer_fb_flip_release_queue(layer, dma->cur);
		dma->cur = dma->queue;
		dma->queue = NULL;
		if (flip->finished)
			flip->finished(flip->finished_data);
	}

	if (flip_status & ATMEL_HLCDC_DMA_CHANNEL_DSCR_DONE) {
		atmel_hlcdc_layer_fb_flip_release_queue(layer, dma->cur);
		dma->cur = NULL;
	}

	if (flip_status & ATMEL_HLCDC_DMA_CHANNEL_DSCR_OVERRUN) {
		regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_CHDR,
			     ATMEL_HLCDC_LAYER_RST);
		if (dma->queue) {
			atmel_hlcdc_layer_fb_flip_release_queue(layer, dma->queue);
			if (dma->queue->finished)
				dma->queue->finished(flip->finished_data);
		}

		if (dma->cur)
			atmel_hlcdc_layer_fb_flip_release_queue(layer, dma->cur);

		dma->cur = NULL;
		dma->queue = NULL;
	}

	if (!dma->queue) {
		atmel_hlcdc_layer_update_apply(layer);

		if (!dma->cur)
			dma->status = ATMEL_HLCDC_LAYER_DISABLED;
	}

	spin_unlock_irqrestore(&layer->lock, flags);
}

int atmel_hlcdc_layer_disable(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_fb_flip *flip;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&layer->lock, flags);

	/*
	 * First disable DMA transfers. If a DMA transfer has been queued
	 * we're stopping this one instead of the current one because we
	 * can't know for sure if queued transfer has been started or not.
	 */
	flip = dma->queue ? dma->queue : dma->cur;
	if (flip) {
		for (i = 0; i < flip->ngems; i++)
			flip->dscrs[i]->ctrl &= ~(ATMEL_HLCDC_LAYER_DFETCH |
						  ATMEL_HLCDC_LAYER_DONE_IRQ);

		dma->status = ATMEL_HLCDC_LAYER_DISABLING;
	}

	/*
	 * Then discard the pending update request (if any) to prevent
	 * DMA irq handler from restarting the DMA channel after it has
	 * been disabled.
	 */
	if (upd->pending >= 0) {
		atmel_hlcdc_layer_update_reset(layer, upd->pending);
		upd->pending = -1;
	}

	spin_unlock_irqrestore(&layer->lock, flags);

	return 0;
}

int atmel_hlcdc_layer_update_start(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct regmap *regmap = layer->hlcdc->regmap;
	struct atmel_hlcdc_layer_fb_flip *fb_flip;
	struct atmel_hlcdc_layer_update_slot *slot;
	unsigned long flags;
	int i, j = 0;

	fb_flip = kzalloc(sizeof(*fb_flip), GFP_KERNEL);
	if (!fb_flip)
		return -ENOMEM;

	fb_flip->task = drm_flip_work_allocate_task(fb_flip, GFP_KERNEL);
	if (!fb_flip->task) {
		kfree(fb_flip);
		return -ENOMEM;
	}

	spin_lock_irqsave(&layer->lock, flags);

	upd->next = upd->pending ? 0 : 1;

	slot = &upd->slots[upd->next];

	for (i = 0; i < layer->max_planes * 4; i++) {
		if (!dma->dscrs[i].status) {
			fb_flip->dscrs[j++] = &dma->dscrs[i];
			dma->dscrs[i].status =
				ATMEL_HLCDC_DMA_CHANNEL_DSCR_RESERVED;
			if (j == layer->max_planes)
				break;
		}
	}

	if (j < layer->max_planes) {
		for (i = 0; i < j; i++)
			fb_flip->dscrs[i]->status = 0;
	}

	if (j < layer->max_planes) {
		spin_unlock_irqrestore(&layer->lock, flags);
		atmel_hlcdc_layer_fb_flip_destroy(fb_flip);
		return -EBUSY;
	}

	slot->fb_flip = fb_flip;

	if (upd->pending >= 0) {
		memcpy(slot->configs,
		       upd->slots[upd->pending].configs,
		       layer->desc->nconfigs * sizeof(u32));
		memcpy(slot->updated_configs,
		       upd->slots[upd->pending].updated_configs,
		       DIV_ROUND_UP(layer->desc->nconfigs,
				    BITS_PER_BYTE * sizeof(unsigned long)) *
		       sizeof(unsigned long));
		slot->fb_flip->fb = upd->slots[upd->pending].fb_flip->fb;
		if (upd->slots[upd->pending].fb_flip->fb) {
			slot->fb_flip->fb =
				upd->slots[upd->pending].fb_flip->fb;
			slot->fb_flip->ngems =
				upd->slots[upd->pending].fb_flip->ngems;
			drm_framebuffer_reference(slot->fb_flip->fb);
		}
	} else {
		regmap_bulk_read(regmap,
				 layer->desc->regs_offset +
				 ATMEL_HLCDC_LAYER_CFG(layer, 0),
				 upd->slots[upd->next].configs,
				 layer->desc->nconfigs);
	}

	spin_unlock_irqrestore(&layer->lock, flags);

	return 0;
}

void atmel_hlcdc_layer_update_rollback(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;

	atmel_hlcdc_layer_update_reset(layer, upd->next);
	upd->next = -1;
}

void atmel_hlcdc_layer_update_set_fb(struct atmel_hlcdc_layer *layer,
				     struct drm_framebuffer *fb,
				     unsigned int *offsets)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_fb_flip *fb_flip;
	struct atmel_hlcdc_layer_update_slot *slot;
	struct atmel_hlcdc_dma_channel_dscr *dscr;
	struct drm_framebuffer *old_fb;
	int nplanes = 0;
	int i;

	if (upd->next < 0 || upd->next > 1)
		return;

	if (fb)
		nplanes = drm_format_num_planes(fb->pixel_format);

	if (nplanes > layer->max_planes)
		return;

	slot = &upd->slots[upd->next];

	fb_flip = slot->fb_flip;
	old_fb = slot->fb_flip->fb;

	for (i = 0; i < nplanes; i++) {
		struct drm_gem_cma_object *gem;

		dscr = slot->fb_flip->dscrs[i];
		gem = drm_fb_cma_get_gem_obj(fb, i);
		dscr->addr = gem->paddr + offsets[i];
	}

	fb_flip->ngems = nplanes;
	fb_flip->fb = fb;

	if (fb)
		drm_framebuffer_reference(fb);

	if (old_fb)
		drm_framebuffer_unreference(old_fb);
}

void atmel_hlcdc_layer_update_set_finished(struct atmel_hlcdc_layer *layer,
					   void (*finished)(void *data),
					   void *finished_data)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_update_slot *slot;

	if (upd->next < 0 || upd->next > 1)
		return;

	slot = &upd->slots[upd->next];

	slot->fb_flip->finished = finished;
	slot->fb_flip->finished_data = finished_data;
}

void atmel_hlcdc_layer_update_cfg(struct atmel_hlcdc_layer *layer, int cfg,
				  u32 mask, u32 val)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_update_slot *slot;

	if (upd->next < 0 || upd->next > 1)
		return;

	if (cfg >= layer->desc->nconfigs)
		return;

	slot = &upd->slots[upd->next];
	slot->configs[cfg] &= ~mask;
	slot->configs[cfg] |= (val & mask);
	set_bit(cfg, slot->updated_configs);
}

void atmel_hlcdc_layer_update_commit(struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	struct atmel_hlcdc_layer_update_slot *slot;
	unsigned long flags;

	if (upd->next < 0  || upd->next > 1)
		return;

	slot = &upd->slots[upd->next];

	spin_lock_irqsave(&layer->lock, flags);

	/*
	 * Release pending update request and replace it by the new one.
	 */
	if (upd->pending >= 0)
		atmel_hlcdc_layer_update_reset(layer, upd->pending);

	upd->pending = upd->next;
	upd->next = -1;

	if (!dma->queue)
		atmel_hlcdc_layer_update_apply(layer);

	spin_unlock_irqrestore(&layer->lock, flags);


	upd->next = -1;
}

static int atmel_hlcdc_layer_dma_init(struct drm_device *dev,
				      struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	dma_addr_t dma_addr;
	int i;

	dma->dscrs = dma_alloc_coherent(dev->dev,
					layer->max_planes * 4 *
					sizeof(*dma->dscrs),
					&dma_addr, GFP_KERNEL);
	if (!dma->dscrs)
		return -ENOMEM;

	for (i = 0; i < layer->max_planes * 4; i++) {
		struct atmel_hlcdc_dma_channel_dscr *dscr = &dma->dscrs[i];

		dscr->next = dma_addr + (i * sizeof(*dscr));
	}

	return 0;
}

static void atmel_hlcdc_layer_dma_cleanup(struct drm_device *dev,
					  struct atmel_hlcdc_layer *layer)
{
	struct atmel_hlcdc_layer_dma_channel *dma = &layer->dma;
	int i;

	for (i = 0; i < layer->max_planes * 4; i++) {
		struct atmel_hlcdc_dma_channel_dscr *dscr = &dma->dscrs[i];

		dscr->status = 0;
	}

	dma_free_coherent(dev->dev, layer->max_planes * 4 *
			  sizeof(*dma->dscrs), dma->dscrs,
			  dma->dscrs[0].next);
}

static int atmel_hlcdc_layer_update_init(struct drm_device *dev,
				struct atmel_hlcdc_layer *layer,
				const struct atmel_hlcdc_layer_desc *desc)
{
	struct atmel_hlcdc_layer_update *upd = &layer->update;
	int updated_size;
	void *buffer;
	int i;

	updated_size = DIV_ROUND_UP(desc->nconfigs,
				    BITS_PER_BYTE *
				    sizeof(unsigned long));

	buffer = devm_kzalloc(dev->dev,
			      ((desc->nconfigs * sizeof(u32)) +
				(updated_size * sizeof(unsigned long))) * 2,
			      GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	for (i = 0; i < 2; i++) {
		upd->slots[i].updated_configs = buffer;
		buffer += updated_size * sizeof(unsigned long);
		upd->slots[i].configs = buffer;
		buffer += desc->nconfigs * sizeof(u32);
	}

	upd->pending = -1;
	upd->next = -1;

	return 0;
}

int atmel_hlcdc_layer_init(struct drm_device *dev,
			   struct atmel_hlcdc_layer *layer,
			   const struct atmel_hlcdc_layer_desc *desc)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct regmap *regmap = dc->hlcdc->regmap;
	unsigned int tmp;
	int ret;
	int i;

	layer->hlcdc = dc->hlcdc;
	layer->wq = dc->wq;
	layer->desc = desc;

	regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_CHDR,
		     ATMEL_HLCDC_LAYER_RST);
	for (i = 0; i < desc->formats->nformats; i++) {
		int nplanes = drm_format_num_planes(desc->formats->formats[i]);

		if (nplanes > layer->max_planes)
			layer->max_planes = nplanes;
	}

	spin_lock_init(&layer->lock);
	drm_flip_work_init(&layer->gc, desc->name,
			   atmel_hlcdc_layer_fb_flip_release);
	ret = atmel_hlcdc_layer_dma_init(dev, layer);
	if (ret)
		return ret;

	ret = atmel_hlcdc_layer_update_init(dev, layer, desc);
	if (ret)
		return ret;

	/* Flush Status Register */
	regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_IDR,
		     0xffffffff);
	regmap_read(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_ISR,
		    &tmp);

	tmp = 0;
	for (i = 0; i < layer->max_planes; i++)
		tmp |= (ATMEL_HLCDC_LAYER_DMA_IRQ |
			ATMEL_HLCDC_LAYER_DSCR_IRQ |
			ATMEL_HLCDC_LAYER_ADD_IRQ |
			ATMEL_HLCDC_LAYER_DONE_IRQ |
			ATMEL_HLCDC_LAYER_OVR_IRQ) << (8 * i);

	regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_IER, tmp);

	return 0;
}

void atmel_hlcdc_layer_cleanup(struct drm_device *dev,
			       struct atmel_hlcdc_layer *layer)
{
	const struct atmel_hlcdc_layer_desc *desc = layer->desc;
	struct regmap *regmap = layer->hlcdc->regmap;

	regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_IDR,
		     0xffffffff);
	regmap_write(regmap, desc->regs_offset + ATMEL_HLCDC_LAYER_CHDR,
		     ATMEL_HLCDC_LAYER_RST);

	atmel_hlcdc_layer_dma_cleanup(dev, layer);
	drm_flip_work_cleanup(&layer->gc);
}
