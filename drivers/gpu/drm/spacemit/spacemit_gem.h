/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_GEM_H_
#define _SPACEMIT_GEM_H_

#include <linux/scatterlist.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>
#include "spacemit_dmmu.h"

struct spacemit_gem_object {
	struct drm_gem_object base;
	struct sg_table *sgt;
	void *vaddr;
	int vmap_cnt;
	struct mutex vmap_lock;
	struct list_head ttb_entry;
};

static inline struct spacemit_gem_object *
to_spacemit_obj(struct drm_gem_object *gem_obj)
{
	return container_of(gem_obj, struct spacemit_gem_object, base);
}

struct drm_gem_object *
spacemit_gem_prime_import_sg_table(struct drm_device *drm, struct dma_buf_attachment *attch,
				  struct sg_table *sgt);
int spacemit_gem_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
			struct drm_mode_create_dumb *args);
int spacemit_gem_mmap(struct file *filp, struct vm_area_struct *vma);

#endif /* _SPACEMIT_GEM_H_ */
