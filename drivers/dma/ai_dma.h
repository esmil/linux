// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit hdmac driver support
 *
 * Copyright (c) 2025 SPACEMIT, Co. Ltd.
 */

struct ai_dmac {
	struct device *dev;
	void __iomem *base;
	int irq;
	int id;

	struct clk *clk;
	struct reset_control *resets;

	unsigned int src_width;
	unsigned int dest_width;
	unsigned int max_length;
	unsigned int address_align_mask;
	unsigned int length_align_mask;
};

enum aipack_element {
	ELEMENT_SIZE_BIT4,
	ELEMENT_SIZE_BIT8,
	ELEMENT_SIZE_BIT16,
};

enum dma_req_status {
	DMA_REQ_FREE = 0,
	DMA_REQ_SUBMIT = 1,
	DMA_REQ_PROCESS = 2,
	DMA_REQ_DONE = 3,
};

struct ai_pack_param {
	bool sgdg;
	bool pack;
	bool transpose;
	bool pad;
	enum aipack_element ele_size;
	u32 pad_value;
	u32 m_size;
	u32 k_size;
	u32 mr_size;
	u32 kr_size;
	u32 mp_size;
	u32 kp_size;
};

struct req_addr_info {
	void *req_list;
	dma_addr_t req_addr;
};

struct req_node {
	struct req_addr_info *info;
	struct list_head list;
};

struct aipack_param {
	size_t k_size;
	size_t m_size;
	size_t kr_size;
	size_t mr_size;
	bool pack;
	bool transpose;
	bool pad;
	unsigned int pad_value;
	enum aipack_element ele_size;
};

struct dma_transfer_param {
	void *src;
	void *dst;
	pid_t pid;
	bool is_sgdg;
	size_t size;
	struct aipack_param ai_param;
};

struct aidma_req {
	struct dma_transfer_param params;
	enum dma_req_status status;
} __attribute__((aligned(64)));

struct axi_dmac_info {
	struct ai_dmac *dma;
	int work_id;
	bool work;
	void *req;
};

#define AXI_DMAC_NUM	8
#define AIDMA_MAX_REQ	32
extern struct axi_dmac_info aidma_info[AXI_DMAC_NUM];
void start_transfer(void);

int ai_dmac_pack_start(struct ai_dmac *c, struct ai_pack_param *param,
		       dma_addr_t dma_dst, dma_addr_t dma_src);

int ai_dmac_memcpy(struct ai_dmac *c, dma_addr_t dma_dst,
		   dma_addr_t dma_src, size_t len);

int ai_dmac_memcpy_by_2d(struct ai_dmac *c, dma_addr_t dma_dst, dma_addr_t dma_src, size_t len);
