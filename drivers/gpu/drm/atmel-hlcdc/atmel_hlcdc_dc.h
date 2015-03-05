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

#ifndef DRM_ATMEL_HLCDC_H
#define DRM_ATMEL_HLCDC_H

#include <linux/clk.h>
#include <linux/irqdomain.h>
#include <linux/pwm.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_panel.h>
#include <drm/drmP.h>

#include "atmel_hlcdc_layer.h"

#define ATMEL_HLCDC_MAX_LAYERS		5

/**
 * Atmel HLCDC Display Controller description structure.
 *
 * This structure describe the HLCDC IP capabilities and depends on the
 * HLCDC IP version (or Atmel SoC family).
 *
 * @min_width: minimum width supported by the Display Controller
 * @min_height: minimum height supported by the Display Controller
 * @max_width: maximum width supported by the Display Controller
 * @max_height: maximum height supported by the Display Controller
 * @layer: a layer description table describing available layers
 * @nlayers: layer description table size
 */
struct atmel_hlcdc_dc_desc {
	int min_width;
	int min_height;
	int max_width;
	int max_height;
	const struct atmel_hlcdc_layer_desc *layers;
	int nlayers;
};

/**
 * Atmel HLCDC Plane properties.
 *
 * This structure stores plane property definitions.
 *
 * @alpha: alpha blending (or transparency) property
 * @csc: YUV to RGB conversion factors property
 */
struct atmel_hlcdc_plane_properties {
	struct drm_property *alpha;
	struct drm_property *rotation;
};

/**
 * Atmel HLCDC plane rotation enum
 *
 * TODO: export DRM_ROTATE_XX macros defined by omap driver and use them
 * instead of defining this enum.
 */
enum atmel_hlcdc_plane_rotation {
	ATMEL_HLCDC_PLANE_NO_ROTATION,
	ATMEL_HLCDC_PLANE_90DEG_ROTATION,
	ATMEL_HLCDC_PLANE_180DEG_ROTATION,
	ATMEL_HLCDC_PLANE_270DEG_ROTATION,
};

/**
 * Atmel HLCDC Plane.
 *
 * @base: base DRM plane structure
 * @layer: HLCDC layer structure
 * @properties: pointer to the property definitions structure
 * @alpha: current alpha blending (or transparency) status
 */
struct atmel_hlcdc_plane {
	struct drm_plane base;
	struct atmel_hlcdc_layer layer;
	struct atmel_hlcdc_plane_properties *properties;
	enum atmel_hlcdc_plane_rotation rotation;
};

static inline struct atmel_hlcdc_plane *
drm_plane_to_atmel_hlcdc_plane(struct drm_plane *p)
{
	return container_of(p, struct atmel_hlcdc_plane, base);
}

static inline struct atmel_hlcdc_plane *
atmel_hlcdc_layer_to_plane(struct atmel_hlcdc_layer *l)
{
	return container_of(l, struct atmel_hlcdc_plane, layer);
}

/**
 * Atmel HLCDC Plane update request structure.
 *
 * @crtc_x: x position of the plane relative to the CRTC
 * @crtc_y: y position of the plane relative to the CRTC
 * @crtc_w: visible width of the plane
 * @crtc_h: visible height of the plane
 * @src_x: x buffer position
 * @src_y: y buffer position
 * @src_w: buffer width
 * @src_h: buffer height
 * @pixel_format: pixel format
 * @gems: GEM object object containing image buffers
 * @offsets: offsets to apply to the GEM buffers
 * @pitches: line size in bytes
 * @crtc: crtc to display on
 * @finished: finished callback
 * @finished_data: data passed to the finished callback
 * @bpp: bytes per pixel deduced from pixel_format
 * @xstride: value to add to the pixel pointer between each line
 * @pstride: value to add to the pixel pointer between each pixel
 * @nplanes: number of planes (deduced from pixel_format)
 */
struct atmel_hlcdc_plane_update_req {
	int crtc_x;
	int crtc_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	struct drm_framebuffer *fb;
	struct drm_crtc *crtc;
	void (*finished)(void *data);
	void *finished_data;

	/* These fields are private and should not be touched */
	int bpp[ATMEL_HLCDC_MAX_PLANES];
	unsigned int offsets[ATMEL_HLCDC_MAX_PLANES];
	int xstride[ATMEL_HLCDC_MAX_PLANES];
	int pstride[ATMEL_HLCDC_MAX_PLANES];
	int nplanes;
};

/**
 * Atmel HLCDC Planes.
 *
 * This structure stores the instantiated HLCDC Planes and can be accessed by
 * the HLCDC Display Controller or the HLCDC CRTC.
 *
 * @primary: primary plane
 * @cursor: hardware cursor plane
 * @overlays: overlay plane table
 * @noverlays: number of overlay planes
 */
struct atmel_hlcdc_planes {
	struct atmel_hlcdc_plane *primary;
	struct atmel_hlcdc_plane *cursor;
	struct atmel_hlcdc_plane **overlays;
	int noverlays;
};

/**
 * Atmel HLCDC Display Controller.
 *
 * @desc: HLCDC Display Controller description
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @fbdev: framebuffer device attached to the Display Controller
 * @planes: instantiated planes
 * @layers: active HLCDC layer
 * @wq: display controller workqueue
 */
struct atmel_hlcdc_dc {
	const struct atmel_hlcdc_dc_desc *desc;
	struct atmel_hlcdc *hlcdc;
	struct drm_fbdev_cma *fbdev;
	struct atmel_hlcdc_planes *planes;
	struct atmel_hlcdc_layer *layers[ATMEL_HLCDC_MAX_LAYERS];
	struct workqueue_struct *wq;
};

extern struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_formats;
extern struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_and_yuv_formats;

struct atmel_hlcdc_planes *
atmel_hlcdc_create_planes(struct drm_device *dev);

int atmel_hlcdc_plane_prepare_update_req(struct drm_plane *p,
				struct atmel_hlcdc_plane_update_req *req);

int atmel_hlcdc_plane_apply_update_req(struct drm_plane *p,
				struct atmel_hlcdc_plane_update_req *req);

void atmel_hlcdc_crtc_cancel_page_flip(struct drm_crtc *crtc,
				       struct drm_file *file);

int atmel_hlcdc_crtc_create(struct drm_device *dev);

int atmel_hlcdc_create_outputs(struct drm_device *dev);

struct atmel_hlcdc_pwm_chip *atmel_hlcdc_pwm_create(struct drm_device *dev,
						    struct clk *slow_clk,
						    struct clk *sys_clk,
						    void __iomem *regs);

int atmel_hlcdc_pwm_destroy(struct drm_device *dev,
			    struct atmel_hlcdc_pwm_chip *chip);

#endif /* DRM_ATMEL_HLCDC_H */
