/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _CVI_VDEC_V4L2_H
#define _CVI_VDEC_V4L2_H

#include <linux/kconfig.h>
#include <linux/types.h>
struct device;

#if IS_ENABLED(CONFIG_VIDEO_SOPHGO_VDEC)
void cvi_vdec_v4l2_register(struct device *dev);
void cvi_vdec_v4l2_unregister(void);
/* The H.264/H.265 cores share SRAM. Serialize session creation and reject
 * an H.265 encoder while the V4L2 H.264 decoder holds that hardware.
 * No encoding format, parameter, packet, or capture API is changed.
 */
int cvi_vdec_v4l2_encoder_begin(int codec);
void cvi_vdec_v4l2_encoder_end(int codec, bool opened);
void cvi_vdec_v4l2_encoder_closed(bool h265);
#else
static inline void cvi_vdec_v4l2_register(struct device *dev) {}
static inline void cvi_vdec_v4l2_unregister(void) {}
static inline int cvi_vdec_v4l2_encoder_begin(int codec) { return 0; }
static inline void cvi_vdec_v4l2_encoder_end(int codec, bool opened) {}
static inline void cvi_vdec_v4l2_encoder_closed(bool h265) {}
#endif
#endif
