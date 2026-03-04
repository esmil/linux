// SPDX-License-Identifier: GPL-2.0
/*
 * vdev.c - video divece functions
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */
#include <media/v4l2-dev.h>
#include <media/media-entity.h>
#include <media/media-device.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ioctl.h>
#include <linux/media-bus-format.h>
#include <linux/compat.h>
#include <media/spacemit/ccic_uapi.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include "ccic_vdev.h"
#include "ccic_dma.h"
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
#include "ccic_iommu.h"
#endif
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
extern struct ccic_iommu_device *mmu_dev;
extern struct mmu_ctx mmu_ctx[CCIC_IOMMU_CH_NUM];
#endif

static struct {
	__u32 pixelformat;
	__u8 num_planes;
	__u32 pixel_width_align;
	__u32 pixel_height_align;
	__u32 plane_bytes_align[VIDEO_MAX_PLANES];
	struct {
		__u32 num;
		__u32 den;
	}plane_bpp[VIDEO_MAX_PLANES];
	struct {
		__u32 num;
		__u32 den;
	}height_subsampling[VIDEO_MAX_PLANES];
} ccic_formats_table[] = {
	/* bayer raw8 */
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR8,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 8,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGBRG8,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 8,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGRBG8,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 8,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SRGGB8,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 8,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	/* bayer raw10 */
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR10P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 10,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGBRG10P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 10,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGRBG10P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 10,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SRGGB10P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 10,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	/* bayer raw12 */
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR12P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 12,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGBRG12P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 12,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SGRBG12P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 12,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	{
		.pixelformat = V4L2_PIX_FMT_SRGGB12P,
		.num_planes = 1,
		.pixel_width_align = 1,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 12,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	/* yuv */
	/* YUYV YUV422 */
	{
		.pixelformat = V4L2_PIX_FMT_YUYV,
		.num_planes = 1,
		.pixel_width_align = 2,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 16,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
	/* YVYU YUV422 */
	{
		.pixelformat = V4L2_PIX_FMT_YVYU,
		.num_planes = 1,
		.pixel_width_align = 2,
		.plane_bytes_align = {
			[0] = 1,
		},
		.plane_bpp = {
			[0] = {
				.num = 16,
				.den = 1,
			},
		},
		.height_subsampling = {
			[0] = {
				.num = 1,
				.den = 1,
			},
		},
	},
};

static int cvdev_lookup_formats_table(struct v4l2_format *f, int *bit_depth)
{
	struct v4l2_pix_format_mplane *pix_fmt = &f->fmt.pix_mp;
	int loop = 0;

	for (loop = 0; loop < ARRAY_SIZE(ccic_formats_table); loop++) {
		if (ccic_formats_table[loop].pixelformat == pix_fmt->pixelformat) {
			*bit_depth = ccic_formats_table[loop].plane_bpp[0].num;
			break;
		}
	}
	if (loop >= ARRAY_SIZE(ccic_formats_table))
		return -1;

	return 0;
}

void cvdev_fill_v4l2_format(struct v4l2_format *f)
{
	int loop = 0, plane = 0;
	unsigned int width = 0, height = 0, stride = 0;
	struct v4l2_plane_pix_format *plane_fmt = NULL;

	for (loop = 0; loop < ARRAY_SIZE(ccic_formats_table); loop++) {
		if (f->fmt.pix_mp.pixelformat == ccic_formats_table[loop].pixelformat) {
			width = CAM_ALIGN(f->fmt.pix_mp.width, ccic_formats_table[loop].pixel_width_align);
			if (0 == ccic_formats_table[loop].pixel_height_align)
				ccic_formats_table[loop].pixel_height_align = 1;
			height = CAM_ALIGN(f->fmt.pix_mp.height, ccic_formats_table[loop].pixel_height_align);
			pr_debug("%s width=%u, width_align=%u",__func__ ,width, ccic_formats_table[loop].pixel_width_align);
			f->fmt.pix_mp.num_planes = ccic_formats_table[loop].num_planes;
			for (plane = 0; plane < f->fmt.pix_mp.num_planes; plane++) {
				plane_fmt = &f->fmt.pix_mp.plane_fmt[plane];
				stride = CAM_ALIGN((width * ccic_formats_table[loop].plane_bpp[plane].num) / (ccic_formats_table[loop].plane_bpp[plane].den * 8),
								ccic_formats_table[loop].plane_bytes_align[plane]);
				plane_fmt->sizeimage =
					height * stride * ccic_formats_table[loop].height_subsampling[plane].num / ccic_formats_table[loop].height_subsampling[plane].den;
				plane_fmt->bytesperline = stride;
				pr_debug("plane%d stride=%u", plane, stride);
			}
			break;
		}
	}
}

static int cvdev_queue_setup(struct vb2_queue *q,
				unsigned int *num_buffers,
				unsigned int *num_planes,
				unsigned int sizes[],
				struct device *alloc_devs[])
{
	struct ccic_vnode *sc_vnode = container_of(q, struct ccic_vnode, buf_queue);
	int loop = 0;

	if (num_buffers && num_planes) {
		*num_planes = sc_vnode->cur_fmt.fmt.pix_mp.num_planes;
		pr_debug("%s num_buffers=%d num_planes=%d ", __func__, *num_buffers, *num_planes);
		for (loop = 0; loop < *num_planes; loop++) {
			sizes[loop] = sc_vnode->cur_fmt.fmt.pix_mp.plane_fmt[loop].sizeimage;
			pr_debug("plane%d size=%u ", loop, sizes[loop]);
		}
	}
	else {
		pr_err("%s NULL num_buffers or num_planes\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static void cvdev_wait_prepare(struct vb2_queue *q)
{
	/* going to wait sleep, release all locks that may block any vb2 buf/stream functions */
	struct ccic_vnode *sc_vnode = container_of(q, struct ccic_vnode, buf_queue);
	mutex_unlock(&sc_vnode->mlock);
}

static void cvdev_wait_finish(struct vb2_queue *q)
{
	/* wakeup from wait sleep, reacquire all locks */
	struct ccic_vnode *sc_vnode = container_of(q, struct ccic_vnode, buf_queue);
	mutex_lock(&sc_vnode->mlock);
}

static int cvdev_buf_init(struct vb2_buffer *vb)
{
	struct ccic_vbuffer *sc_vb = to_ccic_vbuffer(vb);

	INIT_LIST_HEAD(&sc_vb->list_entry);
	sc_vb->reset_flag = 0;
	return 0;
}

static int cvdev_buf_prepare(struct vb2_buffer *vb)
{
	struct ccic_vbuffer *sc_vb = to_ccic_vbuffer(vb);
	struct ccic_vnode *sc_vnode = container_of(vb->vb2_queue, struct ccic_vnode, buf_queue);

	sc_vb->flags = 0;
	memset(sc_vb->reserved, 0, BUF_RESERVED_DATA_LEN);
	/* sc_vb->vb2_v4l2_buf.flags &= ~V4L2_BUF_FLAG_IGNOR; */
	sc_vb->vb2_v4l2_buf.flags = 0;
	sc_vb->sc_vnode = sc_vnode;
	return 0;
}

static void cvdev_buf_finish(struct vb2_buffer *vb)
{
}

static void cvdev_buf_cleanup(struct vb2_buffer *vb)
{

}

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
static uint32_t cvdev_fill_trans_tab_by_sg(uint32_t *tt_base, struct sg_table *sgt, uint32_t offset, uint32_t length)
{
	struct scatterlist *sg = NULL;
	size_t temp_size = 0, temp_offset = 0, temp_length = 0;
	dma_addr_t start_addr = 0, end_addr = 0, dmad = 0;
	int i = 0;
	uint32_t tt_size = 0;

	sg = sgt->sgl;
	for (i = 0; i < sgt->nents; ++i, sg = sg_next(sg)) {
		pr_debug("sg%d: addr 0x%llx, size 0x%x", i, sg_phys(sg),
			sg_dma_len(sg));
		temp_size += sg_dma_len(sg);
		if (temp_size <= offset) {
			continue;
		}

		if (offset > temp_size - sg_dma_len(sg)) {
			temp_offset =
				offset - temp_size + sg_dma_len(sg);
		} else {
			temp_offset = 0;
		}
		start_addr = ((sg_phys(sg) + temp_offset) >> 12) << 12;

		temp_length = temp_size - offset;
		if (temp_length >= length) {
			temp_offset = sg_dma_len(sg) - temp_length + length;
		} else {
			temp_offset = sg_dma_len(sg);
		}
		end_addr = ((sg_phys(sg) + temp_offset + 0xfff) >> 12) << 12;

		for (dmad = start_addr; dmad < end_addr; dmad += 0x1000) {
			tt_base[tt_size++] = (dmad >> 12) & 0x3fffff;
		}

		if (temp_length >= length)
			break;
	}

	return tt_size;
}

dma_addr_t vb2_buf_paddr(struct vb2_buffer *vb, unsigned int plane_no)
{
	unsigned int offset = 0, length = 0, tt_size = 0, tid = 0;
	int index = 0;
	uint32_t *tt_base = NULL;
	dma_addr_t tt_addr = 0;
	dma_addr_t paddr = 0;
	struct ccic_vbuffer *sc_vb = NULL;
	struct ccic_vnode *sc_vnode = NULL;
	/* struct scatterlist *sg = NULL; */
	struct sg_table *sgt = NULL;
	unsigned int dma_ch = 0;

	sc_vb = vb2_buffer_to_ccic_vbuffer(vb);
	sc_vnode = sc_vb->sc_vnode;
	BUG_ON(!sc_vnode);
	sgt = (struct sg_table*)vb2_plane_cookie(vb, plane_no);
	offset = sc_vnode->planes_offset[vb->index][plane_no];
	length = vb->planes[plane_no].length;
	BUG_ON(sc_vnode->dma_ctx.dma_ch >= MAX_CCIC_DMA_CNT);
	dma_ch = sc_vnode->dma_ctx.dma_ch;
        index = (mmu_ctx[dma_ch].tbu_update_cnt[plane_no])++ & 0x1;
        tt_base = mmu_ctx[dma_ch].tt_base[index][plane_no];
        tt_addr = mmu_ctx[dma_ch].tt_addr[index][plane_no];
        tid = MMU_TID(dma_ch);
	tt_size = cvdev_fill_trans_tab_by_sg(tt_base, sgt, offset, length);
	ccic_mmu_call(mmu_dev, config_channel, tid, tt_addr, tt_size);
	ccic_mmu_call(mmu_dev, enable_channel, tid);
	paddr = (dma_addr_t)mmu_dev->ops->get_sva(mmu_dev, tid, offset);
	return paddr;
}
#endif

static int cvdev_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct ccic_vnode *sc_vnode = container_of(q, struct ccic_vnode, buf_queue);
	struct ccic_dma *ccic_dma = get_ccic_dma();
	struct device *dev = sc_vnode->ccic_dev->dev;
	struct ccic_vbuffer *sc_vb = NULL;
	int ret = 0;

	pr_debug("%s(%s)", __func__, sc_vnode->name);
	sc_vnode->total_frm = 0;
	sc_vnode->sw_err_frm = 0;
	sc_vnode->hw_err_frm = 0;
	sc_vnode->ok_frm = 0;
	sc_vnode->frame_id = 0;

	ret = ccic_dma_ch_irq_enable(ccic_dma, sc_vnode->dma_ctx.dma_ch, 1);
	if (ret < 0) {
		dev_err(dev, "%s(%s) ccic_dma_ch_irq_enable failed ret=%d\n", __func__, sc_vnode->name, ret);
		return ret;
	}
	ret = ccic_dma_ch_enable(ccic_dma, sc_vnode->dma_ctx.dma_ch, 1);
	if (ret < 0) {
		dev_err(dev, "%s(%s) ccic_dma_ch_enable failed ret=%d\n", __func__, sc_vnode->name, ret);
		return ret;
	}

	ret = cvdev_dq_idle_vbuffer(sc_vnode, &sc_vb);
	if (ret) {
		dev_info(dev, "%s(%s) no initial buffer available\n", __func__, sc_vnode->name);
	} else {
		cvdev_q_busy_vbuffer(sc_vnode, sc_vb);
	}
	if (sc_vb) {
		ccic_update_dma_addr(sc_vnode, sc_vb, 0);
		ccic_dma_ch_shadow_ready(ccic_dma, sc_vnode->dma_ctx.dma_ch, 1);
	}
	sc_vnode->is_streaming = 1;
	return 0;
}

static void cvdev_stop_streaming(struct vb2_queue *q)
{
	struct ccic_vnode *sc_vnode = container_of(q, struct ccic_vnode, buf_queue);
	struct ccic_dma *ccic_dma = get_ccic_dma();
	unsigned long flags = 0;

	pr_debug("%s(%s) enter", __func__, sc_vnode->name);

	pr_notice("%s total_frm(%u) sw_err_frm(%u) hw_err_frm(%u) ok_frm(%u)\n",
			sc_vnode->name, sc_vnode->total_frm, sc_vnode->sw_err_frm, sc_vnode->hw_err_frm, sc_vnode->ok_frm);

	spin_lock_irqsave(&(sc_vnode->waitq_head.lock), flags);
	wait_event_interruptible_locked_irq(sc_vnode->waitq_head, !sc_vnode->in_irq && !sc_vnode->in_tasklet);
	sc_vnode->in_streamoff = 1;
	spin_unlock_irqrestore(&(sc_vnode->waitq_head.lock), flags);

	ccic_dma_ch_irq_enable(ccic_dma, sc_vnode->dma_ctx.dma_ch, 0);
	ccic_dma_ch_enable(ccic_dma, sc_vnode->dma_ctx.dma_ch, 0);

	sc_vnode->is_streaming = 0;
	sc_vnode->dma_ctx.dma_ch = MAX_CCIC_DMA_CNT;
	spin_lock_irqsave(&(sc_vnode->waitq_head.lock), flags);
	sc_vnode->in_streamoff = 0;
	spin_unlock_irqrestore(&(sc_vnode->waitq_head.lock), flags);
	pr_debug("%s(%s) leave", __func__, sc_vnode->name);
}

static void cvdev_buf_queue(struct vb2_buffer *vb)
{
	unsigned long flags = 0;
	struct ccic_vbuffer *sc_vb = to_ccic_vbuffer(vb);
	struct vb2_queue *buf_queue = vb->vb2_queue;
	struct ccic_vnode *sc_vnode = container_of(buf_queue, struct ccic_vnode, buf_queue);
	/* struct ccic_dma *ccic_dma = sc_vnode->ccic_dev->dma; */
	/* unsigned int v4l2_buf_flags = sc_vnode->v4l2_buf_flags[vb->index]; */

	spin_lock_irqsave(&sc_vnode->slock, flags);
	atomic_inc(&sc_vnode->queued_buf_cnt);
	list_add_tail(&sc_vb->list_entry, &sc_vnode->queued_list);
	/* if (sc_vnode->is_streaming) {
		if (__cvdev_busy_list_empty(sc_vnode)) {
			__cvdev_dq_idle_vbuffer(sc_vnode, &sc_vb);
			if (sc_vb) {
				__cvdev_q_busy_vbuffer(sc_vnode, sc_vb);
				ccic_update_dma_addr(sc_vnode, sc_vb, 0);
				ccic_dma->ops->shadow_ready(ccic_dma);
			}
		}
	} */
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
}

static struct vb2_ops ccic_vb2_ops = {
	.queue_setup = cvdev_queue_setup,
	.wait_prepare = cvdev_wait_prepare,
	.wait_finish = cvdev_wait_finish,
	.buf_init = cvdev_buf_init,
	.buf_prepare = cvdev_buf_prepare,
	.buf_finish = cvdev_buf_finish,
	.buf_cleanup = cvdev_buf_cleanup,
	.start_streaming = cvdev_start_streaming,
	.stop_streaming = cvdev_stop_streaming,
	.buf_queue = cvdev_buf_queue,
};

static void cvdev_cancel_all_buffers(struct ccic_vnode *sc_vnode)
{
	unsigned long flags = 0;
	struct ccic_vbuffer *pos = NULL, *n = NULL;
	struct vb2_buffer *vb2_buf = NULL;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	list_for_each_entry_safe(pos, n, &sc_vnode->queued_list, list_entry) {
		vb2_buf = &(pos->vb2_v4l2_buf.vb2_buf);
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		list_del_init(&pos->list_entry);
		atomic_dec(&sc_vnode->queued_buf_cnt);
	}
	list_for_each_entry_safe(pos, n, &sc_vnode->busy_list, list_entry) {
		vb2_buf = &(pos->vb2_v4l2_buf.vb2_buf);
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		list_del_init(&pos->list_entry);
		atomic_dec(&sc_vnode->busy_buf_cnt);
	}
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
}

static void cvdev_flush_all_buffers(struct ccic_vnode *sc_vnode)
{
	unsigned long flags = 0;
	struct ccic_vbuffer *pos = NULL, *n = NULL;
	struct vb2_buffer *vb2_buf = NULL;
	struct ccic_dma *ccic_dma = get_ccic_dma();
	unsigned long timeout = 0;
	int ret, wait_done = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);

	/* cancel next buffer output by dma. */
	if (atomic_read(&sc_vnode->busy_buf_cnt) >= 1) {
		wait_done = 1;
		sc_vnode->wait_done_flush = 1;
		if (atomic_read(&sc_vnode->busy_buf_cnt) > 1) {
			ccic_dma_ch_shadow_ready(ccic_dma, sc_vnode->dma_ctx.dma_ch, 0);
			pos = list_last_entry(&sc_vnode->busy_list, struct ccic_vbuffer, list_entry);
			vb2_buf = &(pos->vb2_v4l2_buf.vb2_buf);
			vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
			list_del_init(&pos->list_entry);
			atomic_dec(&sc_vnode->busy_buf_cnt);
		}
	}

	list_for_each_entry_safe(pos, n, &sc_vnode->queued_list, list_entry) {
		vb2_buf = &(pos->vb2_v4l2_buf.vb2_buf);
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		list_del_init(&pos->list_entry);
		atomic_dec(&sc_vnode->queued_buf_cnt);
	}

	spin_unlock_irqrestore(&sc_vnode->slock, flags);

	if (wait_done) {
		timeout = msecs_to_jiffies(300);
		ret = wait_for_completion_timeout(&sc_vnode->flush_complete, timeout);
		if (0 == ret) {
			pr_warn("csi flush buffer wait completion timeout.");
		}
	}
}

static int cvdev_vidioc_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
	ret = vb2_reqbufs(&sc_vnode->buf_queue, b);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
	ret = vb2_querybuf(&sc_vnode->buf_queue, b);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;
	unsigned int i = 0;

	sc_vnode->v4l2_buf_flags[b->index] = b->flags;
	if (!b->m.planes) {
		return -EINVAL;
	}
	for (i = 0; i < b->length; i++) {
		sc_vnode->planes_offset[b->index][i] = b->m.planes[i].data_offset;
	}
	mutex_lock(&sc_vnode->mlock);
	ret = vb2_qbuf(&sc_vnode->buf_queue, vnode->v4l2_dev->mdev, b);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_expbuf(struct file *file, void *fh, struct v4l2_exportbuffer *e)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
#ifndef MODULE
	ret = vb2_expbuf(&sc_vnode->buf_queue, e);
#endif
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
	ret = vb2_dqbuf(&sc_vnode->buf_queue, b, file->f_flags & O_NONBLOCK);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_create_bufs(struct file *file, void *fh, struct v4l2_create_buffers *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
#ifndef MODULE
	ret = vb2_create_bufs(&sc_vnode->buf_queue, b);
#endif
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_prepare_buf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
	ret = vb2_prepare_buf(&sc_vnode->buf_queue, vnode->v4l2_dev->mdev, b);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_streamon(struct file *file, void *fn, enum v4l2_buf_type i)
{
	int ret = 0;
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	mutex_lock(&sc_vnode->mlock);
	ret = vb2_streamon(&sc_vnode->buf_queue, i);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_streamoff(struct file *file, void *fn, enum v4l2_buf_type i)
{
	int ret = 0;
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	unsigned long flags = 0;

	pr_debug("%s(%s) enter", __func__, sc_vnode->name);
	pr_debug("%s(%s) queued_buf_cnt=%d busy_buf_cnt=%d.", __func__, sc_vnode->name, atomic_read(&sc_vnode->queued_buf_cnt), atomic_read(&sc_vnode->busy_buf_cnt));
	spin_lock_irqsave(&sc_vnode->waitq_head.lock, flags);
	wait_event_interruptible_locked_irq(sc_vnode->waitq_head, !sc_vnode->in_tasklet && !sc_vnode->in_irq);
	sc_vnode->in_streamoff = 1;
	spin_unlock_irqrestore(&sc_vnode->waitq_head.lock, flags);
	pr_debug("%s tasklet clean", sc_vnode->name);
	mutex_lock(&sc_vnode->mlock);
	pr_debug("%s cancel all buffers", sc_vnode->name);
	cvdev_cancel_all_buffers(sc_vnode);
	pr_debug("%s streamoff", sc_vnode->name);
	ret = vb2_streamoff(&sc_vnode->buf_queue, i);
	mutex_unlock(&sc_vnode->mlock);
	spin_lock_irqsave(&sc_vnode->waitq_head.lock, flags);
	sc_vnode->in_streamoff = 0;
	spin_unlock_irqrestore(&sc_vnode->waitq_head.lock, flags);
	pr_debug("%s(%s) leave", __func__, sc_vnode->name);
	return ret;
}

static int cvdev_vidioc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);

	strncpy(cap->driver, sc_vnode->name, 16);
	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	return 0;
}

/*
 * static int cvdev_vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *fh, struct v4l2_fmtdesc *f)
 * {
 *     return 0;
 * }
 */

static int cvdev_vidioc_g_fmt_vid_cap_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);

	pr_debug("get format fourcc code[0x%08x] (%dx%d)",
			sc_vnode->cur_fmt.fmt.pix_mp.pixelformat,
			sc_vnode->cur_fmt.fmt.pix_mp.width,
			sc_vnode->cur_fmt.fmt.pix_mp.height);
	*f = sc_vnode->cur_fmt;
	return 0;
}

static int __cvdev_vidioc_s_fmt_vid_cap_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	struct vb2_queue *vb2_queue = &sc_vnode->buf_queue;
	struct ccic_dma *ccic_dma = get_ccic_dma();
	struct device *dev = &vnode->dev;
	int ret = 0, bit_depth = 0;
	unsigned int fmt_code = 0, width = 0, height = 0, h_offset, v_offset;

	pr_debug("set format fourcc code[0x%08x] (%dx%d)",
			f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height);
	width = f->fmt.pix_mp.width;
	height = f->fmt.pix_mp.height;
	h_offset = f->fmt.pix_mp.reserved[0];
	v_offset = f->fmt.pix_mp.reserved[1];

	ret = cvdev_lookup_formats_table(f, &bit_depth);
	if (ret) {
		pr_err("%s failed to lookup formats table fourcc code[0x%08x]\n", __func__, f->fmt.pix_mp.pixelformat);
		return ret;
	}

	if (bit_depth == 8) {
		fmt_code = CSI_DUMP_FMT_RAW8;
	} else if (bit_depth == 10) {
		fmt_code = CSI_DUMP_FMT_RAW10;
	} else if (bit_depth == 12) {
		fmt_code = CSI_DUMP_FMT_RAW12;
	} else if (bit_depth == 16) {
		fmt_code = CSI_DUMP_FMT_YUV422;
		/* width *= 2; */
	} else {
		dev_err(dev, "unknown bit_depth=%d\n", bit_depth);
		return -EINVAL;
	}
	if (vb2_is_streaming(vb2_queue)) {
		/* ret = ccic_dma->ops->change_window_size(ccic_dma, width, height, h_offset, v_offset, fmt_code); */
		/*if (ret) {
			dev_err(dev, "%s resize windwo(%ux%u offset:%ux%u) failed\n", __func__,
							width, height, h_offset, v_offset);
			return ret;
		} */
	} else {
		ret = ccic_dma_ch_set_fmt(ccic_dma, sc_vnode->dma_ctx.dma_ch,
								width, height, h_offset, v_offset, fmt_code);
		if (ret) {
			dev_err(dev, "%s set fmt(%ux%u code:0x%08x) failed\n", __func__,
					width, height, fmt_code);
			return ret;
		}
	}
	cvdev_fill_v4l2_format(f);
	sc_vnode->cur_fmt = *f;

	return 0;
}

static int cvdev_vidioc_s_fmt_vid_cap_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	int ret = 0;

	mutex_lock(&sc_vnode->mlock);
	ret = __cvdev_vidioc_s_fmt_vid_cap_mplane(file, fh, f);
	mutex_unlock(&sc_vnode->mlock);
	return ret;
}

static int cvdev_vidioc_try_fmt_vid_cap_mplane(struct file *file, void *fh, struct v4l2_format *f)
{
	return 0;
}

static int cvdev_vidioc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	struct csi_out_path_param *out_path_param = (struct csi_out_path_param*)(&a->parm.raw_data[sizeof(struct v4l2_captureparm)]);
	struct ccic_ctrl *ccic_ctrl = sc_vnode->ccic_dev->ctrl;
	struct device *dev = &vnode->dev;
	struct ccic_dma *ccic_dma = get_ccic_dma();
	unsigned int path_id = sc_vnode->idx % PATH_NUM_PER_DEV;
	int ret = 0;

	if (out_path_param->mode_param.mode >= CSI_WORK_MODE_MAX) {
		dev_err(dev, "%s(%s) invalid csi work mode %u\n", __func__, sc_vnode->name,
				out_path_param->mode_param.mode);
		return -EINVAL;
	}
	if (out_path_param->dma_channel >= MAX_CCIC_DMA_CNT) {
		dev_err(dev, "%s(%s) invalid dma channel %u\n", __func__, sc_vnode->name,
				out_path_param->dma_channel);
		return -EINVAL;
	}
	ret = ccic_ctrl->ops->config_csi2_mode(ccic_ctrl, out_path_param->mode_param.mode,
										out_path_param->mode_param.dt_filter_en);
	if (ret < 0) {
		dev_err(dev, "%s(%s) config csi work mode %u failed\n", __func__, sc_vnode->name,
				out_path_param->mode_param.mode);
		return ret;
	}
	if (out_path_param->mode_param.mode == CSI_WORK_MODE_VC) {
		ret = ccic_ctrl->ops->config_csi_path_vc(ccic_ctrl, path_id, out_path_param->vc);
		if (ret < 0) {
			dev_err(dev, "%s(%s) config vc %u failed\n", __func__, sc_vnode->name,
					out_path_param->vc);
			return ret;
		}
		if (out_path_param->mode_param.dt_filter_en) {
			ret = ccic_ctrl->ops->config_csi_path_dt_filter(ccic_ctrl, path_id,
							out_path_param->dt_filter0_en,
							out_path_param->filter0_pattern,
							out_path_param->dt_filter1_en,
							out_path_param->filter1_pattern);
			if (ret < 0) {
				dev_err(dev, "%s(%s) config dt filters failed\n", __func__, sc_vnode->name);
				return ret;
			}
		}
	}
	ret = ccic_ctrl->ops->config_csi2_mbus(ccic_ctrl,
							out_path_param->phy_param.lane_num,
							out_path_param->phy_param.mipi_mbps);
	if (ret < 0) {
		dev_err(dev, "%s(%s) config mbus lanes %u mipi_mbps %u failed\n", __func__, sc_vnode->name,
				out_path_param->phy_param.lane_num,
				out_path_param->phy_param.mipi_mbps);
		return ret;
	}
	ret = ccic_dma_ch_src_sel(ccic_dma, out_path_param->dma_channel, sc_vnode->idx);
	if (ret < 0) {
		dev_err(dev, "%s(%s) attach dma ch %u failed\n", __func__, sc_vnode->name, out_path_param->dma_channel);
		return ret;
	}
	dev_info(dev,
			"%s mode %u dt_en %u vc %u filter0_en %u filter0 %u filter1_en %u filter1 %u lanes %u mipi_bps %u\n",
			sc_vnode->name, out_path_param->mode_param.mode, out_path_param->mode_param.dt_filter_en,
			out_path_param->vc, out_path_param->dt_filter0_en, out_path_param->filter0_pattern,
			out_path_param->dt_filter1_en, out_path_param->filter1_pattern,
			out_path_param->phy_param.lane_num, out_path_param->phy_param.mipi_mbps);
	dev_info(dev, "%s set dma ch %u\n", sc_vnode->name, out_path_param->dma_channel);
	sc_vnode->dma_ctx.dma_ch =  out_path_param->dma_channel;
	return 0;
}

static long cvdev_vidioc_default(struct file *file,
					void *fh,
					bool valid_prio,
					unsigned int cmd,
					void *arg)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	struct device *dev = &vnode->dev;
	struct csi_path_info *path_info = NULL;

	switch (cmd) {
	case CSI_VIDIOC_G_PATH_INFO:
		path_info = (struct csi_path_info*)arg;
		path_info->id = sc_vnode->idx;
		path_info->csi_id = sc_vnode->ccic_dev->index;
		path_info->path_id = sc_vnode->idx % PATH_NUM_PER_DEV;
		break;
	case CSI_VIDIOC_FLUSH_BUF:
		cvdev_flush_all_buffers(sc_vnode);
		break;
	default:
		dev_err(dev, "%s(%s) unknown ioctl cmd(%d)\n", __func__, sc_vnode->name, cmd);
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct v4l2_ioctl_ops ccic_v4l2_ioctl_ops = {
	/* VIDIOC_QUERYCAP handler */
	.vidioc_querycap = cvdev_vidioc_querycap,
	/* VIDIOC_ENUM_FMT handlers */
	/* .vidioc_enum_fmt_vid_cap = cvdev_vidioc_enum_fmt_vid_cap_mplane, */
	/* VIDIOC_G_FMT handlers */
	.vidioc_g_fmt_vid_cap_mplane = cvdev_vidioc_g_fmt_vid_cap_mplane,
	/* VIDIOC_S_FMT handlers */
	.vidioc_s_fmt_vid_cap_mplane = cvdev_vidioc_s_fmt_vid_cap_mplane,
	/* VIDIOC_TRY_FMT handlers */
	.vidioc_try_fmt_vid_cap_mplane = cvdev_vidioc_try_fmt_vid_cap_mplane,
	/* Buffer handlers */
	.vidioc_reqbufs = cvdev_vidioc_reqbufs,
	.vidioc_querybuf = cvdev_vidioc_querybuf,
	.vidioc_qbuf = cvdev_vidioc_qbuf,
	.vidioc_expbuf = cvdev_vidioc_expbuf,
	.vidioc_dqbuf = cvdev_vidioc_dqbuf,
	.vidioc_create_bufs = cvdev_vidioc_create_bufs,
	.vidioc_prepare_buf = cvdev_vidioc_prepare_buf,
	.vidioc_streamon = cvdev_vidioc_streamon,
	.vidioc_streamoff = cvdev_vidioc_streamoff,
	/* int (*vidioc_s_parm)(struct file *file, void *fh, struct v4l2_streamparm *a); */
	.vidioc_s_parm = cvdev_vidioc_s_parm,
	.vidioc_default = cvdev_vidioc_default,
};

static int cvdev_open(struct file *file)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	struct ccic_ctrl *ccic_ctrl = sc_vnode->ccic_dev->ctrl;
	struct v4l2_fh *fh;

	if (atomic_inc_return(&sc_vnode->ref_cnt) != 1) {
		pr_err("vnode(%s - %s) was already openned.\n", sc_vnode->name, video_device_node_name(vnode));
		atomic_dec(&sc_vnode->ref_cnt);
		return -EBUSY;
	}

	/* fix v4l2_fh allocation failure issue */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (!fh) {
		atomic_dec(&sc_vnode->ref_cnt);
		return -ENOMEM;
	}

	v4l2_fh_init(fh, vnode);
	v4l2_fh_add(fh, file);
	file->private_data = fh;

	ccic_ctrl->ops->clk_enable(ccic_ctrl, 1);
	ccic_ctrl->ops->irq_mask(ccic_ctrl, 1);
	pr_debug("open vnode(%s - %s).", sc_vnode->name, video_device_node_name(vnode));
	return 0;
}

static void __cvdev_close(struct ccic_vnode *sc_vnode)
{
	unsigned long flags = 0;

	pr_debug("%s(%s) enter", __func__, sc_vnode->name);
	pr_debug("%s(%s) queued_buf_cnt=%d busy_buf_cnt=%d.", __func__, sc_vnode->name, atomic_read(&sc_vnode->queued_buf_cnt), atomic_read(&sc_vnode->busy_buf_cnt));
	spin_lock_irqsave(&sc_vnode->waitq_head.lock, flags);
	sc_vnode->in_streamoff = 1;
	wait_event_interruptible_locked_irq(sc_vnode->waitq_head, !sc_vnode->in_tasklet && !sc_vnode->in_irq);
	spin_unlock_irqrestore(&sc_vnode->waitq_head.lock, flags);
	pr_debug("%s tasklet clean", sc_vnode->name);
	mutex_lock(&sc_vnode->mlock);
	pr_debug("%s cancel all buffers", sc_vnode->name);
	cvdev_cancel_all_buffers(sc_vnode);
	pr_debug("%s queue release", sc_vnode->name);
	vb2_queue_release(&sc_vnode->buf_queue);
	sc_vnode->buf_queue.owner = NULL;
	sc_vnode->is_streaming = 0;
	mutex_unlock(&sc_vnode->mlock);
	spin_lock_irqsave(&sc_vnode->waitq_head.lock, flags);
	sc_vnode->in_streamoff = 0;
	spin_unlock_irqrestore(&sc_vnode->waitq_head.lock, flags);
	reinit_completion(&sc_vnode->flush_complete);
	sc_vnode->wait_done_flush = 0;

	pr_debug("%s(%s) leave", __func__, sc_vnode->name);
}

static int cvdev_close(struct file *file)
{
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);
	struct ccic_ctrl *ccic_ctrl = sc_vnode->ccic_dev->ctrl;

	if (atomic_dec_and_test(&sc_vnode->ref_cnt)) {
		__cvdev_close(sc_vnode);
		ccic_ctrl->ops->clk_enable(ccic_ctrl, 0);
	}

	return v4l2_fh_release(file);
}

static __poll_t cvdev_poll(struct file *file, struct poll_table_struct *wait)
{
	__poll_t ret;
	struct video_device *vnode = video_devdata(file);
	struct ccic_vnode *sc_vnode = container_of(vnode, struct ccic_vnode, vnode);

	ret = vb2_poll(&sc_vnode->buf_queue, file, wait);

	return ret;
}

static struct v4l2_file_operations ccic_file_operations = {
	.owner = THIS_MODULE,
	.poll = cvdev_poll,
	.unlocked_ioctl = video_ioctl2,
	.open = cvdev_open,
	.release = cvdev_close,
};

static void cvdev_release(struct video_device *vdev)
{
	struct ccic_vnode *sc_vnode = container_of(vdev, struct ccic_vnode, vnode);

	pr_debug("%s(%s %s) enter.", __func__, sc_vnode->name, video_device_node_name(&sc_vnode->vnode));
	mutex_destroy(&sc_vnode->mlock);
}
/*
static void cvdev_block_release(struct ccic_block *b)
{
	struct ccic_vnode *sc_vnode = container_of(b, struct ccic_vnode, ac_block);

	pr_debug("%s(%s %s) enter.", __func__, sc_vnode->name, video_device_node_name(&sc_vnode->vnode));
	vb2_queue_release(&sc_vnode->buf_queue);
	video_unregister_device(&sc_vnode->vnode);
}
*/

void cvdev_destroy_vnode(struct ccic_vnode *sc_vnode)
{
	video_unregister_device(&sc_vnode->vnode);
}

struct ccic_vnode* cvdev_create_vnode(const char *name,
							unsigned int idx,
							struct v4l2_device *v4l2_dev,
							struct device *alloc_dev,
							struct ccic_dev *ccic_dev,
							void (*dma_tasklet_handler)(unsigned long),
							unsigned int min_buffers_needed)
{
	int ret = 0, i = 0;
	struct ccic_vnode *sc_vnode = NULL;
	struct ccic_dma_context *dma_ctx = NULL;
	struct ccic_dma_work_struct *ccic_dma_work = NULL;

	if (NULL == name || NULL == v4l2_dev || NULL == alloc_dev || NULL == ccic_dev) {
		pr_err("%s invalid arguments.\n", __func__);
		return NULL;
	}
	sc_vnode = devm_kzalloc(alloc_dev, sizeof(*sc_vnode), GFP_KERNEL);
	if (NULL == sc_vnode) {
		pr_err("%s failed to alloc mem for ccic_vnode(%s).\n", __func__, name);
		return NULL;
	}
	dma_ctx = &sc_vnode->dma_ctx;
	dma_ctx->sc_vnode = sc_vnode;
	dma_ctx->dma_ch = MAX_CCIC_DMA_CNT;
	INIT_LIST_HEAD(&dma_ctx->dma_work_idle_list);
	INIT_LIST_HEAD(&dma_ctx->dma_work_busy_list);
	spin_lock_init(&dma_ctx->slock);
	for (i = 0; i < CCIC_DMA_WORK_MAX_CNT; i++) {
		ccic_dma_work = devm_kzalloc(alloc_dev, sizeof(*ccic_dma_work), GFP_KERNEL);
		if (!ccic_dma_work) {
			dev_err(alloc_dev, "%s not enough mem\n", __func__);
			return NULL;
		}
		tasklet_init(&ccic_dma_work->dma_tasklet, dma_tasklet_handler, (unsigned long)ccic_dma_work);
		INIT_LIST_HEAD(&ccic_dma_work->idle_list_entry);
		INIT_LIST_HEAD(&ccic_dma_work->busy_list_entry);
		ccic_dma_work->sc_vnode = sc_vnode;
		list_add(&ccic_dma_work->idle_list_entry, &dma_ctx->dma_work_idle_list);
	}
	sc_vnode->csi2vc = CCIC_CSI2VC_MAIN;
	sc_vnode->src_sel = CCIC_DMA_SEL_LOCAL_MAIN;
	sc_vnode->lane_num = 1;
	sc_vnode->in_streamoff = 0;
	sc_vnode->in_irq = 0;
	sc_vnode->in_tasklet = 0;
	INIT_LIST_HEAD(&sc_vnode->queued_list);
	INIT_LIST_HEAD(&sc_vnode->busy_list);
	atomic_set(&sc_vnode->queued_buf_cnt, 0);
	atomic_set(&sc_vnode->busy_buf_cnt, 0);
	spin_lock_init(&sc_vnode->slock);
	init_completion(&sc_vnode->flush_complete);
	sc_vnode->wait_done_flush = 0;
	mutex_init(&sc_vnode->mlock);
	init_waitqueue_head(&sc_vnode->waitq_head);
	sc_vnode->idx = idx;
	sc_vnode->buf_queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC | V4L2_BUF_FLAG_TSTAMP_SRC_SOE;
	sc_vnode->buf_queue.buf_struct_size = sizeof(struct ccic_vbuffer);
	sc_vnode->buf_queue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	sc_vnode->buf_queue.io_modes = VB2_DMABUF;
	sc_vnode->buf_queue.ops = &ccic_vb2_ops;
#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
	sc_vnode->buf_queue.mem_ops = &vb2_dma_sg_memops;
#else
	sc_vnode->buf_queue.mem_ops = &vb2_dma_contig_memops;
#endif
	/* sc_vnode->buf_queue.min_buffers_needed = min_buffers_needed; */
	sc_vnode->buf_queue.dev = alloc_dev;
	ret = vb2_queue_init(&sc_vnode->buf_queue);
	if (ret) {
		pr_err("%s vb2_queue_init failed for ccic_vnode(%s).\n", __func__, name);
		goto queue_init_fail;
	}

	strncpy(sc_vnode->vnode.name, name, 32);
	strncpy(sc_vnode->name, name, 32);
	sc_vnode->ccic_dev = ccic_dev;
	sc_vnode->vnode.queue = &sc_vnode->buf_queue;
	sc_vnode->vnode.fops = &ccic_file_operations;
	sc_vnode->vnode.ioctl_ops = &ccic_v4l2_ioctl_ops;
	sc_vnode->vnode.release = cvdev_release;
	sc_vnode->vnode.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE_MPLANE;
	sc_vnode->vnode.v4l2_dev = v4l2_dev;
	set_bit(V4L2_FL_USES_V4L2_FH, &sc_vnode->vnode.flags);
	ret = __video_register_device(&sc_vnode->vnode, VFL_TYPE_VIDEO, -1, 1, THIS_MODULE);
	if (ret) {
		pr_err("%s video dev register failed for ccic_vnode(%s).\n", __func__, name);
		goto vdev_register_fail;
	}
	ccic_dev->vnode = sc_vnode;
	pr_debug("create vnode(%s - %s) successfully.", name, video_device_node_name(&sc_vnode->vnode));
	return sc_vnode;
vdev_register_fail:
	vb2_queue_release(&sc_vnode->buf_queue);
queue_init_fail:
	devm_kfree(alloc_dev, sc_vnode);
	return NULL;
}

int __cvdev_dq_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	*sc_vb = list_first_entry_or_null(&sc_vnode->queued_list, struct ccic_vbuffer, list_entry);
	if (NULL == *sc_vb)
		return -1;
	list_del_init(&(*sc_vb)->list_entry);
	atomic_dec(&sc_vnode->queued_buf_cnt);
	return 0;
}

int __cvdev_q_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer *sc_vb)
{
	list_add_tail(&sc_vb->list_entry, &sc_vnode->queued_list);
	atomic_inc(&sc_vnode->queued_buf_cnt);
	return 0;
}

int cvdev_dq_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_dq_idle_vbuffer(sc_vnode, sc_vb);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	return ret;
}

int cvdev_q_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer *sc_vb)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_q_idle_vbuffer(sc_vnode, sc_vb);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);

	return ret;
}

int cvdev_pick_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	*sc_vb = list_first_entry_or_null(&sc_vnode->queued_list, struct ccic_vbuffer, list_entry);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	if (NULL == *sc_vb) {
		return -1;
	}
	return 0;
}

int __cvdev_pick_idle_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	*sc_vb = list_first_entry_or_null(&sc_vnode->queued_list, struct ccic_vbuffer, list_entry);
	if (NULL == *sc_vb) {
		return -1;
	}
	return 0;
}

int __cvdev_dq_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	*sc_vb = list_first_entry_or_null(&sc_vnode->busy_list, struct ccic_vbuffer, list_entry);
	if (NULL == *sc_vb)
		return -1;
	list_del_init(&(*sc_vb)->list_entry);
	atomic_dec(&sc_vnode->busy_buf_cnt);
	return 0;
}

int __cvdev_q_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer *sc_vb)
{
	list_add_tail(&sc_vb->list_entry, &sc_vnode->busy_list);
	atomic_inc(&sc_vnode->busy_buf_cnt);
	return 0;
}

int cvdev_dq_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_dq_busy_vbuffer(sc_vnode, sc_vb);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	return ret;
}

int cvdev_pick_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	*sc_vb = list_first_entry_or_null(&sc_vnode->busy_list, struct ccic_vbuffer, list_entry);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	if (NULL == *sc_vb)
		return -1;

	return 0;
}

int __cvdev_pick_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer **sc_vb)
{
	*sc_vb = list_first_entry_or_null(&sc_vnode->busy_list, struct ccic_vbuffer, list_entry);
	if (NULL == *sc_vb)
		return -1;

	return 0;
}

int cvdev_q_busy_vbuffer(struct ccic_vnode *sc_vnode, struct ccic_vbuffer *sc_vb)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_q_busy_vbuffer(sc_vnode, sc_vb);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	return ret;
}

int cvdev_export_ccic_vbuffer(struct ccic_vbuffer *sc_vb, int with_error)
{
	struct vb2_buffer *vb = &sc_vb->vb2_v4l2_buf.vb2_buf;
	if (with_error)
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
	else
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	return 0;
}

int __cvdev_busy_list_empty(struct ccic_vnode *sc_vnode)
{
	return list_empty(&sc_vnode->busy_list);
}

int cvdev_busy_list_empty(struct ccic_vnode *sc_vnode)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_busy_list_empty(sc_vnode);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	return ret;
}

int __cvdev_idle_list_empty(struct ccic_vnode *sc_vnode)
{
	return list_empty(&sc_vnode->queued_list);
}

int cvdev_idle_list_empty(struct ccic_vnode *sc_vnode)
{
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&sc_vnode->slock, flags);
	ret = __cvdev_idle_list_empty(sc_vnode);
	spin_unlock_irqrestore(&sc_vnode->slock, flags);
	return ret;
}
