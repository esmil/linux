// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include "../dpu/dpu_saturn.h"
#include "../dpu/saturn_fbcmem.h"
#include "../../selftests/test-drm_modeset_common.h"

static uint dpu_id = 1;
module_param(dpu_id, uint, 0644);

struct raw_layer_fbcmem_size_test {
	u32 drm_4cc_fmt;
	bool rot_90_or_270;
	u32 plane_crop_width;
	u32 output_mem_size;
	u32 expected_mem_size;
};

struct afbc_layer_fbcmem_size_test {
	u8 rdma_work_mode;
	u32 drm_4cc_fmt;
	u32 crop_start_x;
	u32 crop_start_y;
	u32 crop_width;
	u32 crop_height;
	u32 fbc_block_size;
	bool rot_90_or_270;
	u8 min_lines;
	u32 output_mem_size;
	u32 expected_mem_size;
};

struct spacemit_hw_device hwdev_res[] = {
	{//SATURN
		.fbcmem_sizes = saturn_fbcmem_sizes,
	},
	{//SATURN_LE
		.fbcmem_sizes = saturn_le_fbcmem_sizes,
	}
};

struct single_rdma_test {
	struct spacemit_crtc_rdma dpu_rdma;
	u32 expect_start;
	u32 expect_size;
	bool expect_map;
};

struct dpu_rdma_test {
	int expect_result;
	struct single_rdma_test *dpu_rdmas;
};

struct concrete_rdma_test {
	size_t test_counter;
	struct dpu_rdma_test *rdmas_test;
};

struct single_rdma_test le_rdma_test[][4] = { //4 rdmas
	{//test 0: odd rdma not use sec fbcmem, use all the primary fbc mem left
		{ //rdma0
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 100, }
			},
			.expect_start = 0,
			.expect_size = 100,
			.expect_map = true,
		},
		{ //rdma1
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 40, }
			},
			.expect_start = 100,
			.expect_size = 1292,
			.expect_map = false,
		},
		{ //rdma2
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 200, }
			},
			.expect_start = 0,
			.expect_size = 200,
			.expect_map = true,
		},
		{ //rdma3
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 60, }
			},
			.expect_start = 200,
			.expect_size = 916,
			.expect_map = false,
		},
	},
	{//test 1: rdma1 use primary and second fbc mem
		{ //rdma0
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 1024, }
			},
			.expect_start = 0,
			.expect_size = 1024,
			.expect_map = true,
		},
		{ //rdma1
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 1024, }
			},
			.expect_start = 1024,
			.expect_size = 1024,
			.expect_map = true,
		},
		{ //rdma2
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 0, }
			},
			.expect_start = 0,
			.expect_size = 0,
			.expect_map = false,
		},
		{ //rdma3
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 100, }
			},
			.expect_start = 656,
			.expect_size = 460, //use all the left
			.expect_map = false,
		},
	},
	{//test 2: rdma3 use fbcmem0
		{ //rdma0
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 0, }
			},
			.expect_start = 0,
			.expect_size = 0,
			.expect_map = false,
		},
		{ //rdma1
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 0, }
			},
			.expect_start = 0,
			.expect_size = 0,
			.expect_map = false,
		},
		{ //rdma2
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 0,
			.expect_size = 600,
			.expect_map = true,
		},
		{ //rdma3
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 600,
			.expect_size = 1908, //use all the left
			.expect_map = true,
		},
	},
	{//test 3: error case, rdma3 use fbcmem0 which has been used
		{ //rdma0
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 100, }
			},
			.expect_start = 0,
			.expect_size = 100,
			.expect_map = 0,
		},
		{ //rdma1
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 0, }
			},
			.expect_start = 0,
			.expect_size = 0,
			.expect_map = false,
		},
		{ //rdma2
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 0,
			.expect_size = 600,
			.expect_map = true,
		},
		{ //rdma3
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 600,
			.expect_size = 1908, //use fbcmem0 but rdma0 use already
			.expect_map = true,
		},
	},
	{//test 4: error case: rdma1 and rdma2 both use fbcmem1
		{ //rdma0
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 0,
			.expect_size = 600,
			.expect_map = true,
		},
		{ //rdma1
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 800, }
			},
			.expect_start = 600,
			.expect_size = 800,
			.expect_map = true,
		},
		{ //rdma2
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 600, }
			},
			.expect_start = 0,
			.expect_size = 600,
			.expect_map = true,
		},
		{ //rdma3
			.dpu_rdma = {
				.mode = 0,
				.fbcmem = { .size = 0, }
			},
			.expect_start = 0,
			.expect_size = 0, //use fbcmem0 but rdma0 use already
			.expect_map = false,
		},
	},
};

struct dpu_rdma_test saturn_le_rdma_tests[] = { //4 rdmas
	{//test 0: odd rdma not use sec fbcmem, use all the primary fbc mem left
		.expect_result = 0,
		.dpu_rdmas = le_rdma_test[0],
	},
	{//test 1: rdma1 use primary and second fbc mem
		.expect_result = 0,
		.dpu_rdmas = le_rdma_test[1],
	},
	{//test 2: rdma3 use fbcmem0
		.expect_result = 0,
		.dpu_rdmas = le_rdma_test[2],
	},
	{//test 3: error case
		.expect_result = -1,
		.dpu_rdmas = le_rdma_test[3],
	},
	{//test 3: error case
		.expect_result = -1,
		.dpu_rdmas = le_rdma_test[4],
	},
};

struct concrete_rdma_test concrete_rdma_tests[] = {
	{ //SATURN
		.rdmas_test = NULL,
		.test_counter = 0,
	},
	{ //SATURN_LE
		.rdmas_test = (struct dpu_rdma_test *)saturn_le_rdma_tests,
		.test_counter = ARRAY_SIZE(saturn_le_rdma_tests),
	},
};

int execute_rdma_fbcmem_cal(struct spacemit_hw_device *hw_dev, struct single_rdma_test *dpu_rdmas, size_t rdma_num)
{
	int ret = 0;
	size_t index = 0;

	struct spacemit_crtc_rdma *dpu_rdmas_ptr = kzalloc(
		sizeof(struct spacemit_crtc_rdma) * rdma_num, GFP_KERNEL);

	if (dpu_rdmas_ptr == NULL) {
		DRM_ERROR("in %s, error for not enough memory left\n", __func__);
		return -ENOMEM;
	}

	memset(dpu_rdmas_ptr, 0, sizeof(struct spacemit_crtc_rdma) * rdma_num);
	for (index = 0; index < rdma_num; index++) { //copy param
		memcpy(&(dpu_rdmas_ptr[index]), &(dpu_rdmas[index].dpu_rdma), sizeof(struct spacemit_crtc_rdma));
	}
	ret = saturn_adjust_rdma_fbcmem(hw_dev, dpu_rdmas_ptr);
	for (index = 0; index < rdma_num; index++) { //copy result back
		memcpy(&(dpu_rdmas[index].dpu_rdma), &(dpu_rdmas_ptr[index]), sizeof(struct spacemit_crtc_rdma));
	}

	kfree(dpu_rdmas_ptr);

	return ret;
}

static struct raw_layer_fbcmem_size_test raw_layer_tests[] = {
	{
		.drm_4cc_fmt = DRM_FORMAT_RGBA8888,
		.rot_90_or_270 = true,
		.plane_crop_width = 1920,
		.expected_mem_size = 1024, //checked
	},
	{
		.drm_4cc_fmt = DRM_FORMAT_NV12,
		.rot_90_or_270 = true,
		.plane_crop_width = 1920,
		.expected_mem_size = 1024, //checked
	},
	{
		.drm_4cc_fmt = DRM_FORMAT_ABGR8888,
		.rot_90_or_270 = false,
		.plane_crop_width = 720,
		.expected_mem_size = 90, //checked
	}
};

static struct afbc_layer_fbcmem_size_test afbc_layer_tests[] = {
	{
		.rdma_work_mode = 0,
		.drm_4cc_fmt = DRM_FORMAT_NV12,
		.crop_start_x = 0,
		.crop_start_y = 0,
		.crop_width = 1920,
		.crop_height = 1080,
		.fbc_block_size = 0,
		.rot_90_or_270 = false,
		.min_lines = 4,
		.expected_mem_size = 480, //checked
	},
	{
		.rdma_work_mode = 0,
		.drm_4cc_fmt = DRM_FORMAT_NV12,
		.crop_start_x = 0,
		.crop_start_y = 0,
		.crop_width = 1920,
		.crop_height = 1080,
		.fbc_block_size = 0,
		.rot_90_or_270 = true,
		.min_lines = 4,
		.expected_mem_size = 288, //checked
	},
};

static int execute_raw_fbcmem_size_cal(struct raw_layer_fbcmem_size_test *test)
{
	int ret = get_raw_data_plane_rdma_mem_size(test->drm_4cc_fmt,
		test->rot_90_or_270, test->plane_crop_width, &(test->output_mem_size));

	return ret;
}

static int execute_afbc_fbcmem_size_cal(struct afbc_layer_fbcmem_size_test *test)
{
	int ret = get_afbc_data_plane_min_rdma_mem_size(test->rdma_work_mode,
			test->drm_4cc_fmt, test->crop_start_x, test->crop_start_y,
			test->crop_width, test->crop_height, test->fbc_block_size,
			test->rot_90_or_270, test->min_lines, &(test->output_mem_size));
	return ret;
}

int spacemit_fbcmem_size_test(void)
{
	int ret = 0;
	size_t i = 0;
	size_t j = 0;
	struct dpu_rdma_test *temp = NULL;
	struct spacemit_hw_device *hw_device = NULL;
	struct concrete_rdma_test *test = NULL;

	for (i = 0; i < ARRAY_SIZE(raw_layer_tests); i++) {
		ret = execute_raw_fbcmem_size_cal(&raw_layer_tests[i]);
		FAIL(ret != 0, "fbcmem raw_layer_tests[%d] return %d but expect %d\n",
			i, ret, 0);
		FAIL(raw_layer_tests[i].output_mem_size !=
			raw_layer_tests[i].expected_mem_size,
			"fbcmem raw_layer_tests[%d] cal size = %d, expect size = %d\n",
			i, raw_layer_tests[i].expected_mem_size,
			raw_layer_tests[i].output_mem_size);
	}

	for (i = 0; i < ARRAY_SIZE(afbc_layer_tests); i++) {
		ret = execute_afbc_fbcmem_size_cal(&(afbc_layer_tests[i]));
		FAIL(ret != 0, "fbcmem afbc_layer_tests[%d] return %d but expect %d\n");
		FAIL(afbc_layer_tests[i].output_mem_size !=
			afbc_layer_tests[i].expected_mem_size,
			"fbcmem afbc_layer_tests[%d] cal size = %d, expect size = %d\n",
			i, afbc_layer_tests[i].output_mem_size,
			afbc_layer_tests[i].expected_mem_size);
	}

	FAIL(dpu_id >= SPACEMIT_DP_MAX_DEVICES, "dpu_id(%d) should < %d\n", dpu_id, SPACEMIT_DP_MAX_DEVICES);
	FAIL(dpu_id != 1, "only dpu_id = 1 is support now\n");
	hw_device = &(hwdev_res[dpu_id]);
	hw_device->rdma_nums = spacemit_dp_devices[dpu_id].rdma_nums;
	test = &concrete_rdma_tests[dpu_id];

	for (i = 0; i < test->test_counter; i++) {
		DRM_DEBUG("\ntest[%u/%u] start\n", i, test->test_counter);
		ret = execute_rdma_fbcmem_cal(hw_device, test->rdmas_test[i].dpu_rdmas, hw_device->rdma_nums);
		//printk("test_counter[%u/%u] test result = %d\n", i, test->test_counter, ret);
		temp = &(test->rdmas_test[i]);
		FAIL(ret != temp->expect_result,
			"rdma fbcmem test[%d] test failed, cal result = %d, expect = %d\n", ret, temp->expect_result);
		if (ret == 0) { //check details
			DRM_DEBUG("rmda[%u][%u] cal result = %d, fbcmem: start = %u, size = %u, map = %u\n",
				i, j, temp->expect_result, temp->dpu_rdmas[j].dpu_rdma.fbcmem.start,
				temp->dpu_rdmas[j].dpu_rdma.fbcmem.size, temp->dpu_rdmas[j].dpu_rdma.fbcmem.map);
			for (j = 0; j < hw_device->rdma_nums; j++) {
				FAIL(temp->dpu_rdmas[j].dpu_rdma.fbcmem.start != temp->dpu_rdmas[j].expect_start,
					"rdma fbcmem test[%d][%d] cal start = %u, expect start = %u\n",
					i, j, temp->dpu_rdmas[j].dpu_rdma.fbcmem.start, temp->dpu_rdmas[j].expect_start);
				FAIL(temp->dpu_rdmas[j].dpu_rdma.fbcmem.size != temp->dpu_rdmas[j].expect_size,
					"rdma fbcmem test[%d][%d] cal size = %u, expect size = %u\n",
					i, j, temp->dpu_rdmas[j].dpu_rdma.fbcmem.size, temp->dpu_rdmas[j].expect_size);
				FAIL(temp->dpu_rdmas[j].dpu_rdma.fbcmem.map != temp->dpu_rdmas[j].expect_map,
					"rdma fbcmem test[%d][%d] cal map = %u, expect_map = %u\n",
					i, j, temp->dpu_rdmas[j].dpu_rdma.fbcmem.map, temp->dpu_rdmas[j].expect_map);
			}
		}
	}

	return 0;
}

static int __init test_spacemit_fbcmem_size_init(void)
{
	int err = spacemit_fbcmem_size_test();

	return err >= 0 ? 0 : err;
}

static void __exit test_spacemit_fbcmem_size_exit(void)
{
}

module_init(test_spacemit_fbcmem_size_init);
module_exit(test_spacemit_fbcmem_size_exit);

MODULE_AUTHOR("SPACEMIT Corporation");
MODULE_LICENSE("GPL");

