/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
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

#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_panel.h>

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC RGB output mode
 */
enum atmel_hlcdc_connector_rgb_mode {
	ATMEL_HLCDC_CONNECTOR_RGB444,
	ATMEL_HLCDC_CONNECTOR_RGB565,
	ATMEL_HLCDC_CONNECTOR_RGB666,
	ATMEL_HLCDC_CONNECTOR_RGB888,
};

struct atmel_hlcdc_slave;

/**
 * Atmel HLCDC Slave device operations structure
 *
 * This structure defines an abstraction to be implemented by each slave
 * device type (panel, convertors, ...).
 *
 * @enable: Enable the slave device
 * @disable: Disable the slave device
 * @get_modes: retrieve modes supported by the slave device
 * @destroy: detroy the slave device and all associated data
 */
struct atmel_hlcdc_slave_ops {
	int (*enable)(struct atmel_hlcdc_slave *slave);
	int (*disable)(struct atmel_hlcdc_slave *slave);
	int (*get_modes)(struct atmel_hlcdc_slave *slave);
	void (*destroy)(struct atmel_hlcdc_slave *slave);
};

/**
 * Atmel HLCDC Slave device structure
 *
 * This structure is the base slave device structure to be overloaded by
 * each slave device implementation.
 *
 * @ops: slave device operations
 */
struct atmel_hlcdc_slave {
	const struct atmel_hlcdc_slave_ops *ops;
};

/**
 * Atmel HLCDC Panel device structure
 *
 * This structure is specialization of the slave device structure to
 * interface with drm panels.
 *
 * @slave: base slave device fields
 * @panel: drm panel attached to this slave device
 */
struct atmel_hlcdc_panel {
	struct atmel_hlcdc_slave slave;
	struct drm_panel *panel;
};

static inline struct atmel_hlcdc_panel *
atmel_hlcdc_slave_to_panel(struct atmel_hlcdc_slave *slave)
{
	return container_of(slave, struct atmel_hlcdc_panel, slave);
}

/**
 * Atmel HLCDC RGB connector structure
 *
 * This structure stores informations about an DRM panel connected through
 * the RGB connector.
 *
 * @connector: DRM connector
 * @encoder: DRM encoder
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @slave: slave device connected to this output
 * @endpoint: DT endpoint representing this output
 * @dpms: current DPMS mode
 */
struct atmel_hlcdc_rgb_output {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct atmel_hlcdc *hlcdc;
	struct atmel_hlcdc_slave *slave;
	struct of_endpoint endpoint;
	int dpms;
};

static inline struct atmel_hlcdc_rgb_output *
drm_connector_to_atmel_hlcdc_rgb_output(struct drm_connector *connector)
{
	return container_of(connector, struct atmel_hlcdc_rgb_output,
			    connector);
}

static inline struct atmel_hlcdc_rgb_output *
drm_encoder_to_atmel_hlcdc_rgb_output(struct drm_encoder *encoder)
{
	return container_of(encoder, struct atmel_hlcdc_rgb_output, encoder);
}

static int atmel_hlcdc_panel_enable(struct atmel_hlcdc_slave *slave)
{
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_slave_to_panel(slave);

	return drm_panel_enable(panel->panel);
}

static int atmel_hlcdc_panel_disable(struct atmel_hlcdc_slave *slave)
{
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_slave_to_panel(slave);

	return drm_panel_disable(panel->panel);
}

static int atmel_hlcdc_panel_get_modes(struct atmel_hlcdc_slave *slave)
{
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_slave_to_panel(slave);

	return panel->panel->funcs->get_modes(panel->panel);
}

static void atmel_hlcdc_panel_destroy(struct atmel_hlcdc_slave *slave)
{
	struct atmel_hlcdc_panel *panel = atmel_hlcdc_slave_to_panel(slave);

	drm_panel_detach(panel->panel);
	kfree(panel);
}

static const struct atmel_hlcdc_slave_ops atmel_hlcdc_panel_ops = {
	.enable = atmel_hlcdc_panel_enable,
	.disable = atmel_hlcdc_panel_disable,
	.get_modes = atmel_hlcdc_panel_get_modes,
	.destroy = atmel_hlcdc_panel_destroy,
};

static struct atmel_hlcdc_slave *
atmel_hlcdc_panel_detect(struct atmel_hlcdc_rgb_output *rgb)
{
	struct device_node *np;
	struct drm_panel *p = NULL;
	struct atmel_hlcdc_panel *panel;

	np = of_graph_get_remote_port_parent(rgb->endpoint.local_node);
	if (!np)
		return NULL;

	p = of_drm_find_panel(np);
	of_node_put(np);

	if (p) {
		panel = kzalloc(sizeof(*panel), GFP_KERNEL);
		if (!panel)
			return NULL;

		drm_panel_attach(p, &rgb->connector);
		panel->panel = p;
		panel->slave.ops = &atmel_hlcdc_panel_ops;
		return &panel->slave;
	}

	return NULL;
}

static void atmel_hlcdc_rgb_encoder_dpms(struct drm_encoder *encoder,
					 int mode)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);
	struct regmap *regmap = rgb->hlcdc->regmap;
	unsigned int status;

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (mode == rgb->dpms)
		return;

	if (mode != DRM_MODE_DPMS_ON) {
		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_DISP);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_DISP))
			cpu_relax();

		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_SYNC);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_SYNC))
			cpu_relax();

		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_PIXEL_CLK);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_PIXEL_CLK))
			cpu_relax();

		clk_disable_unprepare(rgb->hlcdc->sys_clk);

		rgb->slave->ops->disable(rgb->slave);
	} else {
		rgb->slave->ops->enable(rgb->slave);

		clk_prepare_enable(rgb->hlcdc->sys_clk);

		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_PIXEL_CLK);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_PIXEL_CLK))
			cpu_relax();

		/*
		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_SYNC);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_SYNC))
			cpu_relax();
		*/
		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_DISP);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_DISP))
			cpu_relax();
	}

	rgb->dpms = mode;
}

static bool
atmel_hlcdc_rgb_encoder_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted)
{
	return true;
}

static void atmel_hlcdc_rgb_encoder_prepare(struct drm_encoder *encoder)
{
	atmel_hlcdc_rgb_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void atmel_hlcdc_rgb_encoder_commit(struct drm_encoder *encoder)
{
	atmel_hlcdc_rgb_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void
atmel_hlcdc_rgb_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_encoder_to_atmel_hlcdc_rgb_output(encoder);
	struct drm_display_info *info = &rgb->connector.display_info;
	unsigned long prate = clk_get_rate(rgb->hlcdc->sys_clk);
	unsigned long mode_rate = mode->clock * 1000;
	u32 cfg = ATMEL_HLCDC_CLKPOL;
	int div;

	if ((prate / 2) < mode_rate) {
		prate *= 2;
		cfg |= ATMEL_HLCDC_CLKSEL;
	}

	div = DIV_ROUND_UP(prate, mode_rate);
	if (div < 2)
		div = 2;

	cfg |= ATMEL_HLCDC_CLKDIV(div);

	regmap_update_bits(rgb->hlcdc->regmap, ATMEL_HLCDC_CFG(0),
			   ATMEL_HLCDC_CLKSEL | ATMEL_HLCDC_CLKDIV_MASK |
			   ATMEL_HLCDC_CLKPOL, cfg);

	cfg = 0;

	if (info->nbus_formats) {
		switch (info->bus_formats[0]) {
		case VIDEO_BUS_FMT_RGB565_1X16:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB565 << 8;
			break;
		case VIDEO_BUS_FMT_RGB666_1X18:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB666 << 8;
			break;
		case VIDEO_BUS_FMT_RGB888_1X24:
			cfg |= ATMEL_HLCDC_CONNECTOR_RGB888 << 8;
			break;
		case VIDEO_BUS_FMT_RGB444_1X12:
		default:
			break;
		}
	}

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		cfg |= ATMEL_HLCDC_VSPOL;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		cfg |= ATMEL_HLCDC_HSPOL;

	regmap_update_bits(rgb->hlcdc->regmap, ATMEL_HLCDC_CFG(5),
			   ATMEL_HLCDC_HSPOL | ATMEL_HLCDC_VSPOL |
			   ATMEL_HLCDC_VSPDLYS | ATMEL_HLCDC_VSPDLYE |
			   ATMEL_HLCDC_DISPPOL | ATMEL_HLCDC_DISPDLY |
			   ATMEL_HLCDC_VSPSU | ATMEL_HLCDC_VSPHO |
			   ATMEL_HLCDC_MODE_MASK | ATMEL_HLCDC_GUARDTIME_MASK,
			   cfg);
}

static struct drm_encoder_helper_funcs atmel_hlcdc_rgb_encoder_helper_funcs = {
	.dpms = atmel_hlcdc_rgb_encoder_dpms,
	.mode_fixup = atmel_hlcdc_rgb_encoder_mode_fixup,
	.prepare = atmel_hlcdc_rgb_encoder_prepare,
	.commit = atmel_hlcdc_rgb_encoder_commit,
	.mode_set = atmel_hlcdc_rgb_encoder_mode_set,
};

static void atmel_hlcdc_rgb_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	memset(encoder, 0, sizeof(*encoder));
}

static const struct drm_encoder_funcs atmel_hlcdc_rgb_encoder_funcs = {
	.destroy = atmel_hlcdc_rgb_encoder_destroy,
};

static int atmel_hlcdc_rgb_get_modes(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	return rgb->slave->ops->get_modes(rgb->slave);
}

static int atmel_hlcdc_rgb_mode_valid(struct drm_connector *connector,
				      struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder *
atmel_hlcdc_rgb_best_encoder(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	return &rgb->encoder;
}

static struct drm_connector_helper_funcs atmel_hlcdc_rgb_connector_helper_funcs = {
	.get_modes = atmel_hlcdc_rgb_get_modes,
	.mode_valid = atmel_hlcdc_rgb_mode_valid,
	.best_encoder = atmel_hlcdc_rgb_best_encoder,
};

static enum drm_connector_status
atmel_hlcdc_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	if (!rgb->slave) {
		/* At the moment we only support panel devices */
		rgb->slave = atmel_hlcdc_panel_detect(rgb);
	}

	if (rgb->slave)
		return connector_status_connected;

	return connector_status_disconnected;
}

static void
atmel_hlcdc_rgb_connector_destroy(struct drm_connector *connector)
{
	struct atmel_hlcdc_rgb_output *rgb =
			drm_connector_to_atmel_hlcdc_rgb_output(connector);

	if (rgb->slave && rgb->slave->ops->destroy)
		rgb->slave->ops->destroy(rgb->slave);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs atmel_hlcdc_rgb_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = atmel_hlcdc_rgb_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = atmel_hlcdc_rgb_connector_destroy,
};

static int atmel_hlcdc_create_output(struct drm_device *dev,
				     struct of_endpoint *ep)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_rgb_output *rgb;

	rgb = devm_kzalloc(dev->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->endpoint = *ep;

	rgb->dpms = DRM_MODE_DPMS_OFF;

	rgb->hlcdc = dc->hlcdc;

	drm_connector_init(dev, &rgb->connector,
			   &atmel_hlcdc_rgb_connector_funcs,
			   DRM_MODE_CONNECTOR_LVDS);
	drm_connector_helper_add(&rgb->connector,
				 &atmel_hlcdc_rgb_connector_helper_funcs);
	rgb->connector.dpms = DRM_MODE_DPMS_OFF;
	rgb->connector.polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_encoder_init(dev, &rgb->encoder, &atmel_hlcdc_rgb_encoder_funcs,
			 DRM_MODE_ENCODER_LVDS);
	drm_encoder_helper_add(&rgb->encoder,
			       &atmel_hlcdc_rgb_encoder_helper_funcs);

	drm_mode_connector_attach_encoder(&rgb->connector, &rgb->encoder);
	drm_sysfs_connector_add(&rgb->connector);

	rgb->encoder.possible_crtcs = 0x1;

	return 0;
}

int atmel_hlcdc_create_outputs(struct drm_device *dev)
{
	struct device_node *port_np, *np;
	struct of_endpoint ep;
	int ret;

	port_np = of_get_child_by_name(dev->dev->of_node, "port");
	if (!port_np)
		return -EINVAL;

	np = of_get_child_by_name(port_np, "endpoint");
	of_node_put(port_np);

	if (!np)
		return -EINVAL;

	ret = of_graph_parse_endpoint(np, &ep);
	of_node_put(port_np);

	if (ret)
		return ret;

	ret = atmel_hlcdc_create_output(dev, &ep);
	if (ret)
		return ret;

	return 0;
}
