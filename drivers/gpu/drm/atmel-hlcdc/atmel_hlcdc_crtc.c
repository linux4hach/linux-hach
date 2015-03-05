/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
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

#include <linux/clk.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>

#include <video/videomode.h>

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC CRTC structure
 *
 * @base: base DRM CRTC structure
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @event: pointer to the current page flip event
 * @id: CRTC id (returned by drm_crtc_index)
 * @dpms: DPMS mode
 */
struct atmel_hlcdc_crtc {
	struct drm_crtc base;
	struct atmel_hlcdc *hlcdc;
	struct drm_pending_vblank_event *event;
	int id;
	int dpms;
};

static inline struct atmel_hlcdc_crtc *
drm_crtc_to_atmel_hlcdc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct atmel_hlcdc_crtc, base);
}


static void atmel_hlcdc_crtc_dpms(struct drm_crtc *c, int mode)
{
	struct drm_device *dev = c->dev;

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	pm_runtime_get_sync(dev->dev);

	if (mode == DRM_MODE_DPMS_ON)
		pm_runtime_forbid(dev->dev);
	else
		pm_runtime_allow(dev->dev);

	pm_runtime_put_sync(dev->dev);
}

static int atmel_hlcdc_crtc_mode_set(struct drm_crtc *c,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted,
				     int x, int y,
				     struct drm_framebuffer *old_fb)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->hlcdc->regmap;
	struct drm_plane *plane = c->primary;
	struct drm_framebuffer *fb;
	struct videomode vm;
	int ret;

	vm.vfront_porch = mode->vsync_start - mode->vdisplay;
	vm.vback_porch = mode->vtotal - mode->vsync_end;
	vm.vsync_len = mode->vsync_end - mode->vsync_start;
	vm.hfront_porch = mode->hsync_start - mode->hdisplay;
	vm.hback_porch = mode->htotal - mode->hsync_end;
	vm.hsync_len = mode->hsync_end - mode->hsync_start;

	if (vm.hsync_len > 0x40 || vm.hsync_len < 1 ||
	    vm.vsync_len > 0x40 || vm.vsync_len < 1 ||
	    vm.vfront_porch > 0x40 || vm.vfront_porch < 1 ||
	    vm.vback_porch > 0x40 || vm.vback_porch < 0 ||
	    vm.hfront_porch > 0x200 || vm.hfront_porch < 1 ||
	    vm.hback_porch > 0x200 || vm.hback_porch < 1 ||
	    mode->hdisplay > 2048 || mode->hdisplay < 1 ||
	    mode->vdisplay > 2048 || mode->vdisplay < 1)
		return -EINVAL;

	regmap_write(regmap, ATMEL_HLCDC_CFG(1),
		     (vm.hsync_len - 1) | ((vm.vsync_len - 1) << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(2),
		     (vm.vfront_porch - 1) | (vm.vback_porch << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(3),
		     (vm.hfront_porch - 1) | ((vm.hback_porch - 1) << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(4),
		     (mode->hdisplay - 1) | ((mode->vdisplay - 1) << 16));

	fb = plane->fb;
	plane->fb = old_fb;

	ret = plane->funcs->update_plane(plane, c, fb,
					 0, 0,
					 mode->hdisplay, mode->vdisplay,
					 c->x << 16, c->y << 16,
					 mode->hdisplay << 16,
					 mode->vdisplay << 16);

	if (!ret)
		c->primary->fb = fb;

	return ret;
}

static void atmel_hlcdc_crtc_prepare(struct drm_crtc *crtc)
{
	atmel_hlcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void atmel_hlcdc_crtc_commit(struct drm_crtc *crtc)
{
	atmel_hlcdc_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool atmel_hlcdc_crtc_mode_fixup(struct drm_crtc *crtc,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	return true;
}


static const struct drm_crtc_helper_funcs lcdc_crtc_helper_funcs = {

	.mode_fixup = atmel_hlcdc_crtc_mode_fixup,
	.dpms = atmel_hlcdc_crtc_dpms,
	.mode_set = atmel_hlcdc_crtc_mode_set,
	.prepare = atmel_hlcdc_crtc_prepare,
	.commit = atmel_hlcdc_crtc_commit,
};

static void atmel_hlcdc_crtc_destroy(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	drm_crtc_cleanup(c);
	kfree(crtc);
}

void atmel_hlcdc_crtc_cancel_page_flip(struct drm_crtc *c,
				       struct drm_file *file)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = c->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = crtc->event;
	if (event && event->base.file_priv == file) {
		event->base.destroy(&event->base);
		drm_vblank_put(dev, crtc->id);
		crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void atmel_hlcdc_crtc_finish_page_flip(void *data)
{
	struct atmel_hlcdc_crtc *crtc = data;
	struct drm_device *dev = crtc->base.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (crtc->event) {
		drm_send_vblank_event(dev, crtc->id, crtc->event);
		drm_vblank_put(dev, crtc->id);
		crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int atmel_hlcdc_crtc_page_flip(struct drm_crtc *c,
				      struct drm_framebuffer *fb,
				      struct drm_pending_vblank_event *event,
				      uint32_t page_flip_flags)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct atmel_hlcdc_plane_update_req req;
	struct drm_plane *plane = c->primary;
	int ret;

	if (crtc->event)
		return -EBUSY;

	memset(&req, 0, sizeof(req));
	req.crtc_x = 0;
	req.crtc_y = 0;
	req.crtc_h = c->mode.crtc_vdisplay;
	req.crtc_w = c->mode.crtc_hdisplay;
	req.src_x = c->x << 16;
	req.src_y = c->y << 16;
	req.src_w = req.crtc_w << 16;
	req.src_h = req.crtc_h << 16;
	req.fb = fb;
	req.crtc = c;
	req.finished = atmel_hlcdc_crtc_finish_page_flip;
	req.finished_data = crtc;

	ret = atmel_hlcdc_plane_prepare_update_req(plane, &req);
	if (ret)
		return ret;

	if (event) {
		crtc->event = event;
		drm_vblank_get(c->dev, crtc->id);
	}

	ret = atmel_hlcdc_plane_apply_update_req(plane, &req);
	if (ret) {
		crtc->event = NULL;
		drm_vblank_put(c->dev, crtc->id);
	} else {
		plane->fb = fb;
	}

	return ret;
}

static const struct drm_crtc_funcs atmel_hlcdc_crtc_funcs = {
	.page_flip = atmel_hlcdc_crtc_page_flip,
	.set_config = drm_crtc_helper_set_config,
	.destroy = atmel_hlcdc_crtc_destroy,
};

int atmel_hlcdc_crtc_create(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_planes *planes = dc->planes;
	struct atmel_hlcdc_crtc *crtc;
	int ret;
	int i;

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		dev_err(dev->dev, "allocation failed\n");
		return -ENOMEM;
	}

	crtc->hlcdc = dc->hlcdc;

	ret = drm_crtc_init_with_planes(dev, &crtc->base,
				&planes->primary->base,
				planes->cursor ? &planes->cursor->base : NULL,
				&atmel_hlcdc_crtc_funcs);
	if (ret < 0)
		goto fail;

	crtc->id = drm_crtc_index(&crtc->base);

	if (planes->cursor)
		planes->cursor->base.possible_crtcs = 1 << crtc->id;

	for (i = 0; i < planes->noverlays; i++)
		planes->overlays[i]->base.possible_crtcs = 1 << crtc->id;

	drm_crtc_helper_add(&crtc->base, &lcdc_crtc_helper_funcs);

	return 0;

fail:
	atmel_hlcdc_crtc_destroy(&crtc->base);
	return ret;
}

