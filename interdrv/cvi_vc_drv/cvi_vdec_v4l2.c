// SPDX-License-Identifier: GPL-2.0-only
/* Hardware backend for the SG2002 V4L2 decoder; no MMF userspace dependency. */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/sophgo-vdec.h>
#include <media/v4l2-jpeg.h>
#include <sys.h>
#include "cvi_vdec_v4l2.h"
#include "cvi_dec_internal.h"
#include "cvi_h265_interface.h"
#include "cvi_jpg_interface.h"

struct decoder_session {
	u32 fourcc;
	void *h264;
	u8 *h264_sps;
	size_t h264_sps_size;
	CVIJpgHandle jpeg;
	int display_index;
	bool frame_held;
	bool jpeg_initialized;
	u64 jpeg_physical;
	void *jpeg_virtual;
};

static DEFINE_MUTEX(session_lock);
static struct sophgo_vdec *v4l2_decoder;
static bool decoder_active;
static bool h264_active;
static unsigned int h265_encoders;

int cvi_vdec_v4l2_encoder_begin(int codec)
{
	mutex_lock(&session_lock);
	if (codec == CODEC_H265 && h264_active) {
		mutex_unlock(&session_lock);
		return -EBUSY;
	}
	return 0;
}

void cvi_vdec_v4l2_encoder_end(int codec, bool opened)
{
	if (opened && codec == CODEC_H265)
		h265_encoders++;
	mutex_unlock(&session_lock);
}

void cvi_vdec_v4l2_encoder_closed(bool h265)
{
	if (!h265)
		return;
	mutex_lock(&session_lock);
	if (WARN_ON(!h265_encoders))
		goto out;
	h265_encoders--;
out:
	mutex_unlock(&session_lock);
}

static int decoder_open(u32 fourcc, void **handle)
{
	struct decoder_session *s;
	int ret = 0;

	*handle = NULL;
	if (fourcc != V4L2_PIX_FMT_H264 && fourcc != V4L2_PIX_FMT_MJPEG &&
	    fourcc != V4L2_PIX_FMT_JPEG)
		return -EINVAL;
	mutex_lock(&session_lock);
	if (decoder_active || (fourcc == V4L2_PIX_FMT_H264 && h265_encoders)) {
		ret = -EBUSY;
		goto unlock;
	}
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		goto unlock;
	}
	s->fourcc = fourcc;
	if (fourcc == V4L2_PIX_FMT_H264) {
		cviInitDecConfig cfg = {
			.codec = CODEC_H264,
			.cviApiMode = API_MODE_SDK,
			/* This mode allocates codec-owned ION buffers. It does not
			 * use or change the running encoder's MMF VB pools. */
			.vbSrcMode = E_CVI_VB_SRC_COMMON,
			.bsBufferSize = SOPHGO_VDEC_STREAM_SIZE,
			.reorderEnable = 0,
		};

		ret = cviVDecOpen(&cfg, &s->h264);
		if (ret < 0) {
			ret = -EIO;
			kfree(s);
			goto unlock;
		}
		h264_active = true;
	}
	decoder_active = true;
	*handle = s;
unlock:
	mutex_unlock(&session_lock);
	return ret;
}

static void release_frame(void *handle)
{
	struct decoder_session *s = handle;

	if (!s->frame_held)
		return;
	if (s->h264)
		cviVDecReleaseFrame(s->h264, (void *)(uintptr_t)s->display_index);
	else if (s->jpeg) {
		CVIJpgClose(s->jpeg);
		s->jpeg = NULL;
	}
	if (s->jpeg_physical) {
		sys_ion_free_nofd(s->jpeg_physical);
		s->jpeg_physical = 0;
		s->jpeg_virtual = NULL;
	}
	s->frame_held = false;
}

static void decoder_close(void *handle)
{
	struct decoder_session *s = handle;

	mutex_lock(&session_lock);
	release_frame(s);
	if (s->h264)
		cviVDecClose(s->h264);
	if (s->jpeg)
		CVIJpgClose(s->jpeg);
	if (s->jpeg_initialized)
		CVIJpgUninit();
	kfree(s->h264_sps);
	decoder_active = h264_active = false;
	mutex_unlock(&session_lock);
	kfree(s);
}

static int decoder_reset(void *handle)
{
	struct decoder_session *s = handle;
	int ret = 0;

	release_frame(s);
	if (s->h264 && cviVDecReset(s->h264) < 0)
		ret = -EIO;
	kfree(s->h264_sps);
	s->h264_sps = NULL;
	s->h264_sps_size = 0;
	return ret;
}

/* Return the first Annex-B SPS NAL, if this access unit carries one. */
static const u8 *find_h264_sps(const u8 *data, size_t size, size_t *sps_size)
{
	size_t pos = 0, start, next;

	while (pos + 3 < size) {
		if (data[pos] || data[pos + 1] || data[pos + 2]) {
			pos++;
			continue;
		}
		if (data[pos + 2] == 1)
			start = pos + 3;
		else if (pos + 3 < size && data[pos + 3] == 1)
			start = pos + 4;
		else {
			pos++;
			continue;
		}
		if (start >= size)
			break;
		next = start;
		while (next + 3 < size &&
		       (data[next] || data[next + 1] || data[next + 2]))
			next++;
		if (next + 3 >= size)
			next = size;
		if ((data[start] & 0x1f) == 7) {
			while (next > start && !data[next - 1])
				next--;
			*sps_size = next - start;
			return data + start;
		}
		pos = next;
	}
	return NULL;
}

/* Coda's H.264 path cannot consume a new sequence without an explicit reset. */
static int prepare_h264_sequence(struct decoder_session *s,
				 const u8 *data, size_t size)
{
	const u8 *sps;
	size_t sps_size;
	u8 *copy;
	bool changed;

	sps = find_h264_sps(data, size, &sps_size);
	if (!sps || !sps_size)
		return 0;
	changed = s->h264_sps &&
		(s->h264_sps_size != sps_size ||
		 memcmp(s->h264_sps, sps, sps_size));
	if (changed && cviVDecReset(s->h264) < 0)
		return -EIO;
	copy = kmemdup(sps, sps_size, GFP_KERNEL);
	if (!copy)
		return -ENOMEM;
	kfree(s->h264_sps);
	s->h264_sps = copy;
	s->h264_sps_size = sps_size;
	return 0;
}

static const u8 *map_h264_plane(cviVideoDecoder *decoder, u64 phys, u32 length)
{
	unsigned int i, j;
	vpu_buffer_t *sets[] = { decoder->fbMem, decoder->PPUFbMem };

	for (i = 0; i < ARRAY_SIZE(sets); i++)
		for (j = 0; j < MAX_REG_FRAME; j++) {
			vpu_buffer_t *buf = &sets[i][j];

			if (buf->virt_addr && phys >= buf->phys_addr &&
			    phys - buf->phys_addr <= buf->size &&
			    length <= buf->size - (phys - buf->phys_addr))
				return (const u8 *)buf->virt_addr + phys - buf->phys_addr;
		}
	return NULL;
}

/* The kernel JPEG parser rejects baseline streams whose chroma component uses
 * a vertical sampling factor of two. The SG2002 JPU accepts these streams, so
 * recover only the frame geometry and sampling from their SOF0/SOF1 header. */
static int jpeg_frame_fallback(const u8 *data, size_t size,
			       struct v4l2_jpeg_frame_header *frame)
{
	size_t pos = 2;

	while (pos + 4 < size) {
		u16 length;
		u8 marker, components, y_hv, c_hv;

		if (data[pos] != 0xff) {
			pos++;
			continue;
		}
		while (pos < size && data[pos] == 0xff)
			pos++;
		if (pos >= size)
			return -EINVAL;
		marker = data[pos++];
		if (marker == 0xd8 || marker == 0xd9 ||
		    (marker >= 0xd0 && marker <= 0xd7))
			continue;
		if (pos + 2 > size)
			return -EINVAL;
		length = ((u16)data[pos] << 8) | data[pos + 1];
		if (length < 8 || pos + length > size)
			return -EINVAL;
		if (marker != 0xc0 && marker != 0xc1) {
			pos += length;
			continue;
		}
		if (length < 8 + 3 || data[pos + 2] != 8)
			return -EINVAL;
		frame->precision = data[pos + 2];
		frame->height = ((u16)data[pos + 3] << 8) | data[pos + 4];
		frame->width = ((u16)data[pos + 5] << 8) | data[pos + 6];
		components = data[pos + 7];
		if (components != 1 && components != 3)
			return -EINVAL;
		if (components == 1) {
			frame->num_components = 1;
			frame->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;
			return 0;
		}
		if (length < 8 + 9)
			return -EINVAL;
		y_hv = data[pos + 9];
		c_hv = data[pos + 12];
		frame->num_components = 3;
		if (y_hv == 0x11)
			frame->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_444;
		else if (y_hv == 0x21)
			frame->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422;
		else if (y_hv == 0x22 && c_hv == 0x11)
			frame->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_420;
		else if (y_hv == 0x22 && c_hv == 0x12)
			frame->subsampling = V4L2_JPEG_CHROMA_SUBSAMPLING_422;
		else
			return -EINVAL;
		return 0;
	}
	return -EINVAL;
}

static int decode_h264(struct decoder_session *s, const u8 *data, size_t size,
		       bool eos, struct sophgo_vdec_frame *frame)
{
	cviDecOnePicCfg cfg = {
		.bsAddr = (void *)data,
		.bsLen = size,
		.bEndOfStream = eos,
	};
	cviDispFrameCfg result = { 0 };
	cviVideoDecoder *decoder = s->h264;
	VpuRect *crop = &decoder->sequenceInfo.picCropRect;
	u64 physical[3];
	unsigned int p;
	int ret;

	ret = prepare_h264_sequence(s, data, size);
	if (ret)
		return ret;
	ret = cviVDecDecOnePic(s->h264, &cfg, 1000);
	if (ret == CVI_VDEC_RET_LAST_FRM || ret == CVI_VDEC_RET_CONTI)
		return 0;
	if (ret == CVI_DECODE_CONTINUE)
		return 0;
	if (ret == CVI_VDEC_RET_LOCK_TIMEOUT)
		return -ETIMEDOUT;
	if (ret != CVI_VDEC_RET_FRM_DONE && ret != CVI_VDEC_RET_NO_FB_WITH_DISP)
		return -EIO;
	if (cviVDecGetFrame(s->h264, &result) < 0)
		return 0;
	s->display_index = result.indexFrameDisplay;
	s->frame_held = true;
	if (crop->left < 0 || crop->top < 0 || crop->right <= crop->left ||
	    crop->bottom <= crop->top || result.strideY <= 0 || result.strideC <= 0) {
		release_frame(s);
		return -EIO;
	}
	frame->width = crop->right - crop->left;
	frame->height = crop->bottom - crop->top;
	if (frame->width < 32 || frame->height < 32 ||
	    frame->width > SOPHGO_VDEC_MAX_WIDTH || frame->height > SOPHGO_VDEC_MAX_HEIGHT) {
		release_frame(s);
		return -EINVAL;
	}
	frame->fourcc = V4L2_PIX_FMT_YUV420M;
	physical[0] = result.phyAddrY;
	physical[1] = result.phyAddrCb;
	physical[2] = result.phyAddrCr;
	for (p = 0; p < 3; p++) {
		u32 rows = p ? DIV_ROUND_UP(frame->height, 2) : frame->height;
		u32 columns = p ? DIV_ROUND_UP(frame->width, 2) : frame->width;
		u32 top = p ? crop->top / 2 : crop->top;
		u32 left = p ? crop->left / 2 : crop->left;

		frame->stride[p] = p ? result.strideC : result.strideY;
		frame->size[p] = frame->stride[p] * (rows - 1) + columns;
		physical[p] += (u64)top * frame->stride[p] + left;
		frame->data[p] = map_h264_plane(s->h264, physical[p], frame->size[p]);
		if (!frame->data[p]) {
			release_frame(s);
			return -EIO;
		}
	}
	return 1;
}

static int decode_jpeg(struct decoder_session *s, const u8 *data, size_t size,
		       struct sophgo_vdec_frame *frame)
{
	CVIJpgConfig cfg = { .type = CVIJPGCOD_DEC };
	CVIFRAMEBUF result = { 0 };
	struct v4l2_jpeg_header header = { 0 };
	unsigned long hw_time = 0;
	unsigned int p, count = 3, vshift = 0;
	u32 plane_span;
	int ret;

	if (size < 4 || data[0] != 0xff || data[1] != 0xd8 ||
	    data[size - 2] != 0xff || data[size - 1] != 0xd9)
		return -EINVAL;
	ret = v4l2_jpeg_parse_header((void *)data, size, &header);
	if (ret && jpeg_frame_fallback(data, size, &header.frame))
		return -EINVAL;
	if (header.frame.width < 32 || header.frame.height < 32 ||
	    header.frame.width > SOPHGO_VDEC_MAX_WIDTH ||
	    header.frame.height > SOPHGO_VDEC_MAX_HEIGHT ||
	    header.frame.width * header.frame.height > SOPHGO_VDEC_MAX_PIXELS ||
	    header.frame.precision != 8 ||
	    (header.frame.num_components == 3 && header.app14_tf == V4L2_JPEG_APP14_TF_CMYK_RGB) ||
	    (header.frame.num_components != 1 && header.frame.num_components != 3) ||
	    header.frame.subsampling == V4L2_JPEG_CHROMA_SUBSAMPLING_411)
		return -EINVAL;
	/* Separate, uncached buffers protect the live MMF pools. Each plane
	 * reserves the largest supported subsampling, including MCU padding. */
	plane_span = ALIGN(header.frame.width, 64) * ALIGN(header.frame.height, 64);
	ret = sys_ion_alloc_nofd(&s->jpeg_physical, &s->jpeg_virtual,
		(CVI_U8 *)"V4L2_JPEG_Decode", plane_span * 3, false);
	if (ret)
		return -ENOMEM;
	if (!s->jpeg_initialized) {
		if (CVIJpgInit()) {
			ret = -EIO;
			goto error;
		}
		s->jpeg_initialized = true;
	}
	cfg.u.dec.iDataLen = size;
	cfg.u.dec.dst_type = JPEG_MEM_EXTERNAL;
	cfg.u.dec.dec_buf.chromaInterleave = CVI_CBCR_SEPARATED;
	cfg.u.dec.dec_buf.vbY.phys_addr = s->jpeg_physical;
	cfg.u.dec.dec_buf.vbCb.phys_addr = s->jpeg_physical + plane_span;
	cfg.u.dec.dec_buf.vbCr.phys_addr = s->jpeg_physical + 2 * plane_span;
	s->jpeg = CVIJpgOpen(cfg);
	if (!s->jpeg) {
		ret = -EIO;
		goto error;
	}
	ret = CVIJpgSendFrameData(s->jpeg, (void *)data, size, 1000);
	if (ret)
		goto error;
	s->frame_held = true;
	ret = CVIJpgGetFrameData(s->jpeg, &result, sizeof(result), &hw_time);
	if (ret)
		goto error;
	/* GetFrameData has released the JPU transaction. Our external output
	 * survives until release_frame, without holding the shared JPU lock. */
	frame->width = result.width;
	frame->height = result.height;
	switch (result.format) {
	case CVI_FORMAT_420:
		frame->fourcc = V4L2_PIX_FMT_YUV420M;
		vshift = 1;
		break;
	case CVI_FORMAT_422:
		frame->fourcc = V4L2_PIX_FMT_YUV422M;
		break;
	case CVI_FORMAT_444:
		frame->fourcc = V4L2_PIX_FMT_YUV444M;
		break;
	case CVI_FORMAT_400:
		frame->fourcc = V4L2_PIX_FMT_GREY;
		count = 1;
		break;
	default:
		ret = -EINVAL;
		goto error;
	}
	for (p = 0; p < count; p++) {
		frame->stride[p] = p ? result.strideC : result.strideY;
		frame->size[p] = frame->stride[p] *
			(p ? DIV_ROUND_UP(frame->height, 1 << vshift) : frame->height);
		frame->data[p] = (const u8 *)s->jpeg_virtual + p * plane_span;
		if (frame->size[p] > plane_span) {
			ret = -EIO;
			goto error;
		}
	}
	return 1;
error:
	if (s->frame_held)
		release_frame(s);
	else if (s->jpeg) {
		CVIJpgClose(s->jpeg);
		s->jpeg = NULL;
	}
	if (s->jpeg_physical) {
		sys_ion_free_nofd(s->jpeg_physical);
		s->jpeg_physical = 0;
		s->jpeg_virtual = NULL;
	}
	return ret < 0 ? ret : -EIO;
}

static int decoder_decode(void *handle, const u8 *data, size_t size,
			  u64 timestamp, bool eos, struct sophgo_vdec_frame *frame)
{
	struct decoder_session *s = handle;

	if (s->frame_held || size > SOPHGO_VDEC_STREAM_SIZE)
		return -EINVAL;
	memset(frame, 0, sizeof(*frame));
	frame->timestamp = timestamp;
	if (s->h264)
		return decode_h264(s, data, size, eos, frame);
	if (!size)
		return 0;
	return decode_jpeg(s, data, size, frame);
}

static const struct sophgo_vdec_ops decoder_ops = {
	.owner = THIS_MODULE,
	.open = decoder_open,
	.close = decoder_close,
	.reset = decoder_reset,
	.decode = decoder_decode,
	.release_frame = release_frame,
};

void cvi_vdec_v4l2_register(struct device *dev)
{
	v4l2_decoder = sophgo_vdec_register(dev, &decoder_ops);
	if (IS_ERR(v4l2_decoder))
		dev_warn(dev, "V4L2 decoder registration failed: %ld\n", PTR_ERR(v4l2_decoder));
}

void cvi_vdec_v4l2_unregister(void)
{
	sophgo_vdec_unregister(v4l2_decoder);
	v4l2_decoder = NULL;
}
