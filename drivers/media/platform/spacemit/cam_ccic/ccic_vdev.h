// SPDX-License-Identifier: GPL-2.0
/*
 * ccic_vdev.h - video device functions
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_VDEV_H_
#define _CCIC_VDEV_H_
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>
#include <linux/notifier.h>
#include "ccic_drv.h"
#include "ccic_dma.h"

#define CCIC_DMA_WORK_MAX_CNT (16)

struct ccic_vbuffer;
struct ccic_vnode;

struct ccic_dma_context {
	struct list_head dma_work_idle_list;
	struct list_head dma_work_busy_list;
	spinlock_t slock;
	struct ccic_vnode *sc_vnode;
	unsigned int dma_ch;
};

struct ccic_vnode {
	struct video_device vnode;
	char name[32];
	struct vb2_queue buf_queue;
	struct list_head queued_list;
	struct list_head busy_list;
	struct ccic_dma_context dma_ctx;
	atomic_t queued_buf_cnt;
	atomic_t busy_buf_cnt;
	atomic_t ref_cnt;
	spinlock_t slock;
	unsigned char wait_done_flush;
	struct completion flush_complete;
	struct mutex mlock;
	struct v4l2_format cur_fmt;
	struct wait_queue_head waitq_head;
	int in_streamoff;
	int in_tasklet;
	int in_irq;
	int is_streaming;
	unsigned int idx;
	unsigned int total_frm;
	unsigned int sw_err_frm;
	unsigned int hw_err_frm;
	unsigned int ok_frm;
	unsigned int planes_offset[VB2_MAX_FRAME][VB2_MAX_PLANES];
	unsigned int v4l2_buf_flags[VB2_MAX_FRAME];
	struct ccic_dev *ccic_dev;
	int csi2vc;
	int src_sel;
	int lane_num;
	int mipi_m_bps;
	int csi_src; /*csi1 can select dphy0 or dphy1, csi2 can select dphy2 or dphy3 (only for larkM) */
	int ccic_mode;
	int ch_mode;
	unsigned int main_vc;
	unsigned int sub_vc;
	uint64_t frame_id;
	void *usr_data;
};

struct ccic_dma_work_struct {
	struct tasklet_struct dma_tasklet;
	struct list_head idle_list_entry;
	struct list_head busy_list_entry;
	unsigned int irq_status;
	struct ccic_vnode *sc_vnode;
};

#define BUF_FLAG_SOF_TOUCH (1 << 0)
#define BUF_FLAG_DONE_TOUCH (1 << 1)
#define BUF_FLAG_HW_ERR (1 << 2)
#define BUF_FLAG_SW_ERR (1 << 3)
#define BUF_FLAG_TIMESTAMPED (1 << 4)
#define BUF_FLAG_CCIC_TOUCH (1 << 5)

#define BUF_RESERVED_DATA_LEN (32)
struct ccic_vbuffer {
	struct vb2_v4l2_buffer vb2_v4l2_buf;
	struct list_head list_entry;
	unsigned int reset_flag;
	unsigned int flags;
	struct ccic_vnode *sc_vnode;
	unsigned char reserved[BUF_RESERVED_DATA_LEN];
};

#define vb2_buffer_to_ccic_vbuffer(vb) ((struct ccic_vbuffer *)(vb))

#define CAM_ALIGN(a, b)                                   \
	({                                                \
		unsigned int ___tmp1 = (a);               \
		unsigned int ___tmp2 = (b);               \
		unsigned int ___tmp3 = ___tmp1 % ___tmp2; \
		___tmp1 /= ___tmp2;                       \
		if (___tmp3)                              \
			___tmp1++;                        \
		___tmp1 *= ___tmp2;                       \
		___tmp1;                                  \
	})

#define is_vnode_streaming(vnode) ((vnode)->buf_queue.streaming)

#ifdef CONFIG_SPACEMIT_K3_CCIC_IOMMU
dma_addr_t vb2_buf_paddr(struct vb2_buffer *vb, unsigned int plane_no);
#else
static inline dma_addr_t vb2_buf_paddr(struct vb2_buffer *vb,
				       unsigned int plane_no)
{
	unsigned int offset = 0;
	dma_addr_t paddr = 0;
	struct ccic_vbuffer *sc_vb = vb2_buffer_to_ccic_vbuffer(vb);
	struct ccic_vnode *sc_vnode = sc_vb->sc_vnode;
	dma_addr_t *dma_addr = (dma_addr_t *)vb2_plane_cookie(vb, plane_no);

	BUG_ON(!sc_vnode);
	offset = sc_vnode->planes_offset[vb->index][plane_no];
	paddr = *dma_addr + offset;
	return paddr;
}
#endif

static inline void ccic_update_dma_addr(struct ccic_vnode *sc_vnode,
					struct ccic_vbuffer *sc_vbuf,
					unsigned int offset)
{
	dma_addr_t p0 = 0;
	struct ccic_dma *ccic_dma = get_ccic_dma();
	struct vb2_buffer *vb2_buf = &(sc_vbuf->vb2_v4l2_buf.vb2_buf);

	p0 = vb2_buf_paddr(vb2_buf, 0) + offset;
	ccic_dma_ch_set_addr(ccic_dma, sc_vnode->dma_ctx.dma_ch, p0);
}

static inline void *sc_vnode_get_usrdata(struct ccic_vnode *sc_vnode)
{
	return sc_vnode->usr_data;
}

static inline struct ccic_vbuffer *to_ccic_vbuffer(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *vb2_v4l2_buf = to_vb2_v4l2_buffer(vb2);
	return container_of(vb2_v4l2_buf, struct ccic_vbuffer, vb2_v4l2_buf);
}

struct ccic_vnode *
cvdev_create_vnode(const char *name, unsigned int idx,
		   struct v4l2_device *v4l2_dev, struct device *alloc_dev,
		   struct ccic_dev *ccic_dev,
		   void (*dma_tasklet_handler)(unsigned long),
		   unsigned int min_buffers_needed);
void cvdev_destroy_vnode(struct ccic_vnode *sc_vnode);

int cvdev_busy_list_empty(struct ccic_vnode *sc_vnode);
int __cvdev_busy_list_empty(struct ccic_vnode *sc_vnode);
int cvdev_idle_list_empty(struct ccic_vnode *sc_vnode);
int __cvdev_idle_list_empty(struct ccic_vnode *sc_vnode);
int cvdev_dq_idle_vbuffer(struct ccic_vnode *sc_vnode,
			  struct ccic_vbuffer **sc_vb);
int cvdev_pick_idle_vbuffer(struct ccic_vnode *sc_vnode,
			    struct ccic_vbuffer **sc_vb);
int __cvdev_pick_idle_vbuffer(struct ccic_vnode *sc_vnode,
			      struct ccic_vbuffer **sc_vb);
int cvdev_q_idle_vbuffer(struct ccic_vnode *sc_vnode,
			 struct ccic_vbuffer *sc_vb);
int __cvdev_dq_idle_vbuffer(struct ccic_vnode *sc_vnode,
			    struct ccic_vbuffer **sc_vb);
int __cvdev_q_idle_vbuffer(struct ccic_vnode *sc_vnode,
			   struct ccic_vbuffer *sc_vb);
int cvdev_dq_busy_vbuffer(struct ccic_vnode *sc_vnode,
			  struct ccic_vbuffer **sc_vb);
int cvdev_pick_busy_vbuffer(struct ccic_vnode *sc_vnode,
			    struct ccic_vbuffer **sc_vb);
int __cvdev_pick_busy_vbuffer(struct ccic_vnode *sc_vnode,
			      struct ccic_vbuffer **sc_vb);
int cvdev_q_busy_vbuffer(struct ccic_vnode *sc_vnode,
			 struct ccic_vbuffer *sc_vb);
int __cvdev_dq_busy_vbuffer(struct ccic_vnode *sc_vnode,
			    struct ccic_vbuffer **sc_vb);
int __cvdev_q_busy_vbuffer(struct ccic_vnode *sc_vnode,
			   struct ccic_vbuffer *sc_vb);
int cvdev_export_ccic_vbuffer(struct ccic_vbuffer *sc_vb, int with_error);
void cvdev_fill_v4l2_format(struct v4l2_format *f);
#endif
