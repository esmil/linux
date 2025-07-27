#ifndef __SPACEMIT_MAILBOX_H__
#define __SPACEMIT_MAILBOX_H__

#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/spinlock.h>

#define SPACEMIT_NUM_CHANNELS	4

/* mailbox register description */
typedef union mbox_msg {
	u32 val;
	struct {
		u32 msg:32;
	} bits;
} mbox_msg_t;

typedef union mbox_fifo_status {
	u32 val;
	struct {
		u32 is_full:1;
		u32 is_empty:1;
		u32 reserved0:26;
		u32 writefull:1;
		u32 readempty:1;
		u32 reserved1:2;
	} bits;
} mbox_fifo_status_t;


typedef union mbox_msg_status {
	u32 val;
	struct {
		u32 num_msg:4;
		u32 reserved0:24;
		u32 writefull:1;
		u32 readempty:1;
		u32 reserved1:2;
	} bits;
} mbox_msg_status_t;

typedef union mbox_irq_status {
	u32 val;
	struct {
		u32 new_msg0_status:1;
		u32 not_msg0_full:1;
		u32 new_msg1_status:1;
		u32 not_msg1_full:1;
		u32 new_msg2_status:1;
		u32 not_msg2_full:1;
		u32 new_msg3_status:1;
		u32 not_msg3_full:1;
		u32 reserved:24;
	} bits;
} mbox_irq_status_t;

typedef union mbox_irq_status_clr {
	u32 val;
	struct {
		u32 new_msg0_clr:1;
		u32 not_msg0_full_clr:1;
		u32 new_msg1_clr:1;
		u32 not_msg1_full_clr:1;
		u32 new_msg2_clr:1;
		u32 not_msg2_full_clr:1;
		u32 new_msg3_clr:1;
		u32 not_msg3_full_clr:1;
		u32 reserved:24;
	} bits;
} mbox_irq_status_clr_t;

typedef union mbox_irq_enable_set {
	u32 val;
	struct {
		u32 new_msg0_irq_en:1;
		u32 not_msg0_full_irq_en:1;
		u32 new_msg1_irq_en:1;
		u32 not_msg1_full_irq_en:1;
		u32 new_msg2_irq_en:1;
		u32 not_msg2_full_irq_en:1;
		u32 new_msg3_irq_en:1;
		u32 not_msg3_full_irq_en:1;
		u32 reserved:24;
	} bits;
} mbox_irq_enable_set_t;

typedef union mbox_irq_enable_clr {
	u32 val;
	struct {
		u32 new_msg0_irq_clr:1;
		u32 not_msg0_full_irq_clr:1;
		u32 new_msg1_irq_clr:1;
		u32 not_msg1_full_irq_clr:1;
		u32 new_msg2_irq_clr:1;
		u32 not_msg2_full_irq_clr:1;
		u32 new_msg3_irq_clr:1;
		u32 not_msg3_full_irq_clr:1;
		u32 reserved:24;
	} bits;
} mbox_irq_enable_clr_t;

typedef struct mbox_irqthresh {
	u32 val;
	struct {
		u32 mbox0_new_msg_thresh:4;
		u32 mbox0_not_full_thresh:4;
		u32 mbox1_new_msg_thresh:4;
		u32 mbox1_not_full_thresh:4;
		u32 mbox2_new_msg_thresh:4;
		u32 mbox2_not_full_thresh:4;
		u32 mbox3_new_msg_thresh:4;
		u32 mbox3_not_full_thresh:4;
	} bits;
} mbox_irqthresh_t;

typedef struct mbox_thresh {
	mbox_irqthresh_t thresh0;
	u32 reserved[3];
} mbox_thresh_t;

typedef struct mbox_irq {
	mbox_irq_status_t irq_status;
	mbox_irq_status_clr_t irq_status_clr;
	mbox_irq_enable_set_t irq_en_set;
	mbox_irq_enable_clr_t irq_en_clr;
} mbox_irq_t;

typedef struct mbox_reg_desc {
	u32 mbox_version; /* 0x00 */
	u32 reseved0[3]; /* 0x04 ~ 0x0c */
	u32 mbox_sysconfig; /* 0x10 */
	u32 reserved1[11]; /* 0x14 ~ 0x3c */
	mbox_msg_t mbox_msg[4]; /* 0x40 ~ 0x4c */
	u32 reserved2[12]; /* 0x50 ~ 0x7c */
	mbox_fifo_status_t fifo_status[4]; /* 0x80 ~ 0x8c */
	u32 reserved3[12]; /* 0x90 ~ 0xbc */
	mbox_msg_status_t msg_status[4]; /* 0xc0 ~ 0xcc */
	u32 reserved4[12]; /* 0xd0 ~ 0xfc */
	mbox_irq_t mbox_irq[2]; /* 0x100 ~ 0x11c */
	u32 reserved5[24]; /* 0x120 ~ 0x17c */
	mbox_thresh_t mbox_thresh[2]; /* 0x180 */
} mbox_reg_desc_t;

struct spacemit_mailbox {
	struct mbox_controller controller;
	mbox_reg_desc_t *regs;
	spinlock_t lock;
	bool ap_communicate;
};

#define USER0_MBOX_OFFSET ((mbox->ap_communicate) ? 1 : 0)
#define USER1_MBOX_OFFSET ((mbox->ap_communicate) ? 0 : 1)

#endif /* __SPACEMIT_MAILBOX_H__ */
