/*
 * Video Bus API header
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_VIDEO_BUS_FORMAT_H
#define __LINUX_VIDEO_BUS_FORMAT_H

/*
 * These bus formats uniquely identify data formats on the data bus. Mostly
 * they correspond to similarly named VIDEO_PIX_FMT_* formats, format 0 is
 * reserved, VIDEO_BUS_FMT_FIXED shall be used by host-client pairs, where the
 * data format is fixed. Additionally, "2X8" means that one pixel is transferred
 * in two 8-bit samples, "BE" or "LE" specify in which order those samples are
 * transferred over the bus: "LE" means that the least significant bits are
 * transferred first, "BE" means that the most significant bits are transferred
 * first, and "PADHI" and "PADLO" define which bits - low or high, in the
 * incomplete high byte, are filled with padding bits.
 *
 * The bus formats are grouped by type, bus_width, bits per component, samples
 * per pixel and order of subsamples. Numerical values are sorted using generic
 * numerical sort order (8 thus comes before 10).
 *
 * As their value can't change when a new bus format is inserted in the
 * enumeration, the bus formats are explicitly given a numerical value. The next
 * free values for each category are listed below, update them when inserting
 * new pixel codes.
 */
enum video_bus_format {
	VIDEO_BUS_FMT_FIXED = 0x0001,

	/* RGB - next is 0x1010 */
	VIDEO_BUS_FMT_RGB444_2X8_PADHI_BE = 0x1001,
	VIDEO_BUS_FMT_RGB444_2X8_PADHI_LE = 0x1002,
	VIDEO_BUS_FMT_RGB555_2X8_PADHI_BE = 0x1003,
	VIDEO_BUS_FMT_RGB555_2X8_PADHI_LE = 0x1004,
	VIDEO_BUS_FMT_BGR565_2X8_BE = 0x1005,
	VIDEO_BUS_FMT_BGR565_2X8_LE = 0x1006,
	VIDEO_BUS_FMT_RGB565_2X8_BE = 0x1007,
	VIDEO_BUS_FMT_RGB565_2X8_LE = 0x1008,
	VIDEO_BUS_FMT_RGB666_1X18 = 0x1009,
	VIDEO_BUS_FMT_RGB888_1X24 = 0x100a,
	VIDEO_BUS_FMT_RGB888_2X12_BE = 0x100b,
	VIDEO_BUS_FMT_RGB888_2X12_LE = 0x100c,
	VIDEO_BUS_FMT_ARGB8888_1X32 = 0x100d,
	VIDEO_BUS_FMT_RGB444_1X12 = 0x100e,
	VIDEO_BUS_FMT_RGB565_1X16 = 0x100f,

	/* YUV (including grey) - next is 0x2024 */
	VIDEO_BUS_FMT_Y8_1X8 = 0x2001,
	VIDEO_BUS_FMT_UV8_1X8 = 0x2015,
	VIDEO_BUS_FMT_UYVY8_1_5X8 = 0x2002,
	VIDEO_BUS_FMT_VYUY8_1_5X8 = 0x2003,
	VIDEO_BUS_FMT_YUYV8_1_5X8 = 0x2004,
	VIDEO_BUS_FMT_YVYU8_1_5X8 = 0x2005,
	VIDEO_BUS_FMT_UYVY8_2X8 = 0x2006,
	VIDEO_BUS_FMT_VYUY8_2X8 = 0x2007,
	VIDEO_BUS_FMT_YUYV8_2X8 = 0x2008,
	VIDEO_BUS_FMT_YVYU8_2X8 = 0x2009,
	VIDEO_BUS_FMT_Y10_1X10 = 0x200a,
	VIDEO_BUS_FMT_UYVY10_2X10 = 0x2018,
	VIDEO_BUS_FMT_VYUY10_2X10 = 0x2019,
	VIDEO_BUS_FMT_YUYV10_2X10 = 0x200b,
	VIDEO_BUS_FMT_YVYU10_2X10 = 0x200c,
	VIDEO_BUS_FMT_Y12_1X12 = 0x2013,
	VIDEO_BUS_FMT_UYVY8_1X16 = 0x200f,
	VIDEO_BUS_FMT_VYUY8_1X16 = 0x2010,
	VIDEO_BUS_FMT_YUYV8_1X16 = 0x2011,
	VIDEO_BUS_FMT_YVYU8_1X16 = 0x2012,
	VIDEO_BUS_FMT_YDYUYDYV8_1X16 = 0x2014,
	VIDEO_BUS_FMT_UYVY10_1X20 = 0x201a,
	VIDEO_BUS_FMT_VYUY10_1X20 = 0x201b,
	VIDEO_BUS_FMT_YUYV10_1X20 = 0x200d,
	VIDEO_BUS_FMT_YVYU10_1X20 = 0x200e,
	VIDEO_BUS_FMT_YUV10_1X30 = 0x2016,
	VIDEO_BUS_FMT_AYUV8_1X32 = 0x2017,
	VIDEO_BUS_FMT_UYVY12_2X12 = 0x201c,
	VIDEO_BUS_FMT_VYUY12_2X12 = 0x201d,
	VIDEO_BUS_FMT_YUYV12_2X12 = 0x201e,
	VIDEO_BUS_FMT_YVYU12_2X12 = 0x201f,
	VIDEO_BUS_FMT_UYVY12_1X24 = 0x2020,
	VIDEO_BUS_FMT_VYUY12_1X24 = 0x2021,
	VIDEO_BUS_FMT_YUYV12_1X24 = 0x2022,
	VIDEO_BUS_FMT_YVYU12_1X24 = 0x2023,

	/* Bayer - next is 0x3019 */
	VIDEO_BUS_FMT_SBGGR8_1X8 = 0x3001,
	VIDEO_BUS_FMT_SGBRG8_1X8 = 0x3013,
	VIDEO_BUS_FMT_SGRBG8_1X8 = 0x3002,
	VIDEO_BUS_FMT_SRGGB8_1X8 = 0x3014,
	VIDEO_BUS_FMT_SBGGR10_ALAW8_1X8 = 0x3015,
	VIDEO_BUS_FMT_SGBRG10_ALAW8_1X8 = 0x3016,
	VIDEO_BUS_FMT_SGRBG10_ALAW8_1X8 = 0x3017,
	VIDEO_BUS_FMT_SRGGB10_ALAW8_1X8 = 0x3018,
	VIDEO_BUS_FMT_SBGGR10_DPCM8_1X8 = 0x300b,
	VIDEO_BUS_FMT_SGBRG10_DPCM8_1X8 = 0x300c,
	VIDEO_BUS_FMT_SGRBG10_DPCM8_1X8 = 0x3009,
	VIDEO_BUS_FMT_SRGGB10_DPCM8_1X8 = 0x300d,
	VIDEO_BUS_FMT_SBGGR10_2X8_PADHI_BE = 0x3003,
	VIDEO_BUS_FMT_SBGGR10_2X8_PADHI_LE = 0x3004,
	VIDEO_BUS_FMT_SBGGR10_2X8_PADLO_BE = 0x3005,
	VIDEO_BUS_FMT_SBGGR10_2X8_PADLO_LE = 0x3006,
	VIDEO_BUS_FMT_SBGGR10_1X10 = 0x3007,
	VIDEO_BUS_FMT_SGBRG10_1X10 = 0x300e,
	VIDEO_BUS_FMT_SGRBG10_1X10 = 0x300a,
	VIDEO_BUS_FMT_SRGGB10_1X10 = 0x300f,
	VIDEO_BUS_FMT_SBGGR12_1X12 = 0x3008,
	VIDEO_BUS_FMT_SGBRG12_1X12 = 0x3010,
	VIDEO_BUS_FMT_SGRBG12_1X12 = 0x3011,
	VIDEO_BUS_FMT_SRGGB12_1X12 = 0x3012,

	/* JPEG compressed formats - next is 0x4002 */
	VIDEO_BUS_FMT_JPEG_1X8 = 0x4001,

	/* Vendor specific formats - next is 0x5002 */

	/* S5C73M3 sensor specific interleaved UYVY and JPEG */
	VIDEO_BUS_FMT_S5C_UYVY_JPEG_1X8 = 0x5001,

	/* HSV - next is 0x6002 */
	VIDEO_BUS_FMT_AHSV8888_1X32 = 0x6001,
};

#endif /* __LINUX_VIDEO_BUS_FORMAT_H */
