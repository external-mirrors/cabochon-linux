// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: YT SHEN <yt.shen@mediatek.com>
 *
 * Major rework
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/aperture.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_legacy.h"

#define DRIVER_NAME "mediatek"
#define DRIVER_DESC "Mediatek SoC DRM"
#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0

static const struct drm_mode_config_helper_funcs mtk_drm_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static struct drm_framebuffer *
mtk_drm_mode_fb_create(struct drm_device *dev,
		       struct drm_file *file,
		       const struct drm_format_info *info,
		       const struct drm_mode_fb_cmd2 *cmd)
{
	if (info->num_planes != 1)
		return ERR_PTR(-EINVAL);

	return drm_gem_fb_create(dev, file, info, cmd);
}

static const struct drm_mode_config_funcs mtk_drm_mode_config_funcs = {
	.fb_create = mtk_drm_mode_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {
	.output_paths = mt2701_legacy_paths,
	.shadow_register = true,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt2712_mmsys_driver_data = {
	.output_paths = mt2712_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt7623_mmsys_driver_data = {
	.output_paths = mt7623_legacy_paths,
	.shadow_register = true,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt8167_mmsys_driver_data = {
	.output_paths = mt8167_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt8173_mmsys_driver_data = {
	.output_paths = mt8173_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt8183_mmsys_driver_data = {
	.output_paths = mt8183_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt8186_mmsys_driver_data = {
	.output_paths = mt8186_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_drm_route mt8188_mtk_ddp_main_routes[] = {
	{ 0, MTK_DISP_DP_INTF, 0 },
	{ 0, MTK_DISP_DSI, 0 },
};

static const struct mtk_mmsys_driver_data mt8188_vdosys0_driver_data = {
	.output_paths = mt8188_legacy_paths,
	.conn_routes = mt8188_mtk_ddp_main_routes,
	.num_conn_routes = ARRAY_SIZE(mt8188_mtk_ddp_main_routes),
	.mmsys_dev_num = 2,
	.max_width = 8191,
	.min_width = 1,
	.min_height = 1,
};

static const struct mtk_mmsys_driver_data mt8192_mmsys_driver_data = {
	.output_paths = mt8192_legacy_paths,
	.mmsys_dev_num = 1,
};

static const struct mtk_mmsys_driver_data mt8195_vdosys0_driver_data = {
	.output_paths = mt8195_vdo0_legacy_paths,
	.mmsys_dev_num = 2,
	.max_width = 8191,
	.min_width = 1,
	.min_height = 1,
};

static const struct mtk_mmsys_driver_data mt8195_vdosys1_driver_data = {
	.output_paths = mt8195_vdo1_legacy_paths,
	.mmsys_id = 1,
	.mmsys_dev_num = 2,
	.max_width = 8191,
	.min_width = 2, /* 2-pixel align when ethdr is bypassed */
	.min_height = 1,
};

static const struct mtk_mmsys_driver_data mt8365_mmsys_driver_data = {
	.mmsys_dev_num = 1,
};

static const struct of_device_id mtk_drm_of_ids[] = {
	{ .compatible = "mediatek,mt2701-mmsys",
	  .data = &mt2701_mmsys_driver_data},
	{ .compatible = "mediatek,mt7623-mmsys",
	  .data = &mt7623_mmsys_driver_data},
	{ .compatible = "mediatek,mt2712-mmsys",
	  .data = &mt2712_mmsys_driver_data},
	{ .compatible = "mediatek,mt8167-mmsys",
	  .data = &mt8167_mmsys_driver_data},
	{ .compatible = "mediatek,mt8173-mmsys",
	  .data = &mt8173_mmsys_driver_data},
	{ .compatible = "mediatek,mt8183-mmsys",
	  .data = &mt8183_mmsys_driver_data},
	{ .compatible = "mediatek,mt8186-mmsys",
	  .data = &mt8186_mmsys_driver_data},
	{ .compatible = "mediatek,mt8188-vdosys0",
	  .data = &mt8188_vdosys0_driver_data},
	{ .compatible = "mediatek,mt8188-vdosys1",
	  .data = &mt8195_vdosys1_driver_data},
	{ .compatible = "mediatek,mt8192-mmsys",
	  .data = &mt8192_mmsys_driver_data},
	{ .compatible = "mediatek,mt8195-mmsys",
	  .data = &mt8195_vdosys0_driver_data},
	{ .compatible = "mediatek,mt8195-vdosys0",
	  .data = &mt8195_vdosys0_driver_data},
	{ .compatible = "mediatek,mt8195-vdosys1",
	  .data = &mt8195_vdosys1_driver_data},
	{ .compatible = "mediatek,mt8365-mmsys",
	  .data = &mt8365_mmsys_driver_data},
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_drm_of_ids);

static int mtk_drm_match(struct device *dev, const void *data)
{
	if (!strncmp(dev_name(dev), "mediatek-drm", sizeof("mediatek-drm") - 1))
		return true;
	return false;
}

static bool mtk_drm_get_all_drm_priv(struct device *dev)
{
	struct mtk_drm_private *drm_priv = dev_get_drvdata(dev);
	struct mtk_drm_private **all_drm_priv;
	struct mtk_drm_private *temp_drm_priv;
	struct device_node *phandle = dev->parent->of_node;
	const struct of_device_id *of_id;
	struct device_node *node;
	struct device *drm_dev;
	unsigned int cnt = 0;
	bool all_privs_found;
	int i, j;

	dev_vdbg(dev, "Populating private data for all controllers on ID%u\n",
		 drm_priv->data->mmsys_id);

	/* Avoid variable length arrays and allocate an array of pointers to privs */
	all_drm_priv = kmalloc_array(drm_priv->data->mmsys_dev_num,
				     sizeof(struct mtk_drm_private *), GFP_KERNEL);
	if (!all_drm_priv)
		return -ENOMEM;

	for_each_child_of_node(phandle->parent, node) {
		struct platform_device *pdev;

		of_id = of_match_node(mtk_drm_of_ids, node);
		if (!of_id)
			continue;

		pdev = of_find_device_by_node(node);
		if (!pdev)
			continue;

		drm_dev = device_find_child(&pdev->dev, NULL, mtk_drm_match);
		put_device(&pdev->dev);
		if (!drm_dev)
			continue;

		temp_drm_priv = dev_get_drvdata(drm_dev);
		put_device(drm_dev);
		if (!temp_drm_priv)
			continue;

		/* Assign each probed controller's priv pointer to the array */
		all_drm_priv[temp_drm_priv->data->mmsys_id] = temp_drm_priv;

		if (temp_drm_priv->mtk_drm_bound)
			cnt++;

		if (cnt == drm_priv->data->mmsys_dev_num) {
			of_node_put(node);
			break;
		}
	}

	/*
	 * If all controller priv pointers were found, initialize them all so
	 * that every controller has pointers to the mtk_drm_private of all
	 * the others.
	 */
	all_privs_found = drm_priv->data->mmsys_dev_num == cnt;
	if (all_privs_found) {
		for (i = 0; i < cnt; i++)
			for (j = 0; j < cnt; j++)
				all_drm_priv[j]->all_drm_private[i] = all_drm_priv[i];
	}

	/* Done using the temporary array of pointers: free it now. */
	kfree(all_drm_priv);

	return all_privs_found;
}

static bool mtk_drm_find_mmsys_comp(struct mtk_drm_private *private,
				    enum mtk_ddp_comp_type type, u8 inst_id)
{
	const struct mtk_mmsys_driver_data *data = private->data;
	int i, j;

	for (i = 0; i < MAX_CRTC; i++) {
		const struct mtk_drm_path_definition *output_path = &data->output_paths[i];

		for (j = 0; j < output_path->len; j++) {
			if (output_path->comp[j].type != type ||
			    output_path->comp[j].inst_id != inst_id)
				continue;

			return true;
		}
	}

	if (data->num_conn_routes)
		for (i = 0; i < data->num_conn_routes; i++)
			if (data->conn_routes[i].route_ddp_type == type &&
			    data->conn_routes[i].route_ddp_inst_id == inst_id)
				return true;

	return false;
}

static struct mtk_drm_private *
mtk_drm_find_matching_controller(struct mtk_drm_private **all_drm_private,
				 struct device_node *target_node)
{
	for (int i = 0; i < all_drm_private[0]->data->mmsys_dev_num; i++) {
		struct mtk_drm_private *priv = all_drm_private[i];
		struct device_node *cur_mmsys_node = priv->mmsys_dev->of_node;

		if (target_node == cur_mmsys_node)
			return priv;
	}

	return NULL;
}

static void mtk_drm_set_path_orders(struct mtk_drm_private **all_drm_private)
{
	struct mtk_drm_private *private = all_drm_private[0];
	unsigned short i, j;

	/*
	 * If the SoC has multiple display controllers w/DirectLink architecture
	 * at this point all controllers have been registered and it is now safe
	 * to iterate through and setup the outputs order so that the data path
	 * follows the correct controllers sequence.
	 *
	 * At the end of this, the controllers are always ordered 0..N hence the
	 * data, for each different display controller output (as each support
	 * multiple different outputs as well) always travels through consecutive
	 * controller indices, like CTRLRx_OUTPUTy -> ... -> CTRLRx+n_OUTPUTy,
	 * where:
	 *
	 *  - The first one (0) is responsible for getting input frames from DRM
	 *    and dispatching to image processing HW(s) and/or next controller;
	 *  - The last one (N) is responsible for output to a physical display
	 *
	 * Note that one controller may also output to a different out-number of
	 * its consecutive, so a display controller-I/O sequence like
	 *
	 *    CTRLR0_OUTPUT0 -> CTRLR1_OUTPUT3 -> CTRLR3_OUTPUT2 (-> DISPLAY)
	 *          0        ->        1       ->       2
	 *
	 * should also considered as being valid since the hardware is capable of
	 * doing so, but this is a corner case that is currently not handled to
	 * simplify the implementation.
	 */
	for (i = 0; i < MAX_CRTC; i++) {
		unsigned short max_iterations = private->data->mmsys_dev_num;
		bool order_found;

		do {
			order_found = false;

			for (j = 0; j < private->data->mmsys_dev_num; j++) {
				const struct mtk_drm_path_definition *src_path;
				struct mtk_drm_private *cur_priv, *src_priv;
				struct mtk_drm_path_definition *cur_path;
				unsigned int src_order;

				cur_priv = all_drm_private[j];
				if (!cur_priv)
					continue;

				cur_path = &cur_priv->data->output_paths[i];

				if (!cur_path->len || !cur_path->input_controller)
					continue;

				src_priv = mtk_drm_find_matching_controller(all_drm_private,
								cur_path->input_controller);
				if (!src_priv)
					continue;

				/*
				 * Paths are per-output: set order for the new output
				 * by finding the order of the same output number of
				 * the previous controller and incrementing it by one
				 */
				src_path = &src_priv->data->output_paths[i];
				src_order = src_path->len ? src_path->order : 0;
				if (cur_path->order <= src_order) {
					cur_path->order = src_order + 1;

					/* Order found: check next CRTC now! */
					order_found = true;
					break;
				}
			}
		} while (!order_found && max_iterations--);
	}
}

static int mtk_drm_kms_init(struct drm_device *drm)
{
	struct mtk_drm_private *private = drm->dev_private;
	struct mtk_drm_private *priv_n;
	struct device *dma_dev = NULL;
	struct drm_crtc *crtc;
	int ret, i, j;

	if (drm_firmware_drivers_only())
		return -ENODEV;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 64;
	drm->mode_config.min_height = 64;

	/*
	 * set max width and height as default value(4096x4096).
	 * this value would be used to check framebuffer size limitation
	 * at drm_mode_addfb().
	 */
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &mtk_drm_mode_config_funcs;
	drm->mode_config.helper_private = &mtk_drm_mode_config_helpers;

	for (i = 0; i < private->data->mmsys_dev_num; i++) {
		drm->dev_private = private->all_drm_private[i];
		ret = component_bind_all(private->all_drm_private[i]->dev, drm);
		if (ret) {
			while (--i >= 0)
				component_unbind_all(private->all_drm_private[i]->dev, drm);
			return ret;
		}
	}

	/*
	 * Ensure internal panels are at the top of the connector list before
	 * crtc creation.
	 */
	drm_helper_move_panel_connectors_to_head(drm);

	/* Set controllers order for multi-controller architecture */
	mtk_drm_set_path_orders(private->all_drm_private);

	/*
	 * 1. We currently support two fixed data streams, each optional,
	 *    and each statically assigned to a crtc:
	 *    OVL0 -> COLOR0 -> AAL -> OD -> RDMA0 -> UFOE -> DSI0 ...
	 * 2. For multi mmsys architecture, crtc path data are located in
	 *    different drm private data structures. Loop through crtc index to
	 *    create crtc from the main path and then ext_path and finally the
	 *    third path.
	 */
	for (i = 0; i < MAX_CRTC; i++) {
		for (j = 0; j < private->data->mmsys_dev_num; j++) {
			priv_n = private->all_drm_private[j];

			if (priv_n->data->max_width)
				drm->mode_config.max_width = priv_n->data->max_width;

			if (priv_n->data->min_width)
				drm->mode_config.min_width = priv_n->data->min_width;

			if (priv_n->data->min_height)
				drm->mode_config.min_height = priv_n->data->min_height;

			if (!priv_n->data->output_paths[i].len)
				continue;

			ret = mtk_crtc_create(drm, i, j,
					      priv_n->data->conn_routes,
					      priv_n->data->num_conn_routes);

			if (ret)
				goto err_component_unbind;
		}
	}

	/* IGT will check if the cursor size is configured */
	drm->mode_config.cursor_width = 512;
	drm->mode_config.cursor_height = 512;

	/* Use OVL device for all DMA memory allocations */
	crtc = drm_crtc_from_index(drm, 0);
	if (crtc)
		dma_dev = mtk_crtc_dma_dev_get(crtc);
	if (!dma_dev) {
		ret = -ENODEV;
		dev_err(drm->dev, "Need at least one OVL device\n");
		goto err_component_unbind;
	}

	drm_dev_set_dma_dev(drm, dma_dev);

	/*
	 * Configure the DMA segment size to make sure we get contiguous IOVA
	 * when importing PRIME buffers.
	 */
	dma_set_max_seg_size(dma_dev, UINT_MAX);

	ret = drm_vblank_init(drm, MAX_CRTC);
	if (ret < 0)
		goto err_component_unbind;

	drm_kms_helper_poll_init(drm);
	drm_mode_config_reset(drm);

	return 0;

err_component_unbind:
	for (i = 0; i < private->data->mmsys_dev_num; i++)
		component_unbind_all(private->all_drm_private[i]->dev, drm);

	return ret;
}

static void mtk_drm_kms_deinit(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
	drm_atomic_helper_shutdown(drm);

	component_unbind_all(drm->dev, drm);
}

DEFINE_DRM_GEM_FOPS(mtk_drm_fops);

static const struct drm_driver mtk_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,

	.fops = &mtk_drm_fops,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

static int mtk_drm_bind(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct platform_device *pdev;
	struct drm_device *drm;
	int ret, i;

	pdev = of_find_device_by_node(private->mutex_node);
	if (!pdev) {
		dev_err(dev, "Waiting for disp-mutex device %pOF\n",
			private->mutex_node);
		of_node_put(private->mutex_node);
		return -EPROBE_DEFER;
	}

	private->mutex_dev = &pdev->dev;
	private->mtk_drm_bound = true;
	private->dev = dev;

	if (!mtk_drm_get_all_drm_priv(dev))
		return 0;

	drm = drm_dev_alloc(&mtk_drm_driver, dev);
	if (IS_ERR(drm)) {
		ret = PTR_ERR(drm);
		goto err_put_dev;
	}

	private->drm_master = true;
	drm->dev_private = private;
	for (i = 0; i < private->data->mmsys_dev_num; i++)
		private->all_drm_private[i]->drm = drm;

	ret = mtk_drm_kms_init(drm);
	if (ret < 0)
		goto err_free;

	ret = aperture_remove_all_conflicting_devices(DRIVER_NAME);
	if (ret < 0)
		dev_err(dev, "Error %d while removing conflicting aperture devices", ret);

	ret = drm_dev_register(drm, 0);
	if (ret < 0)
		goto err_deinit;

	drm_client_setup(drm, NULL);

	return 0;

err_deinit:
	mtk_drm_kms_deinit(drm);
err_free:
	private->drm = NULL;
	drm_dev_put(drm);
	for (i = 0; i < private->data->mmsys_dev_num; i++)
		private->all_drm_private[i]->drm = NULL;
err_put_dev:
	put_device(private->mutex_dev);
	return ret;
}

static void mtk_drm_unbind(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);

	/* for multi mmsys dev, unregister drm dev in mmsys master */
	if (private->drm_master) {
		drm_dev_unregister(private->drm);
		mtk_drm_kms_deinit(private->drm);
		drm_dev_put(private->drm);
		put_device(private->mutex_dev);
	}
	private->mtk_drm_bound = false;
	private->drm_master = false;
	private->drm = NULL;
}

static const struct component_master_ops mtk_drm_ops = {
	.bind		= mtk_drm_bind,
	.unbind		= mtk_drm_unbind,
};

static const struct of_device_id mtk_ddp_comp_dt_ids[] = {
	{ .compatible = "mediatek,mt8167-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8173-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8183-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8192-disp-aal",
	  .data = (void *)MTK_DISP_AAL},
	{ .compatible = "mediatek,mt8196-disp-blender",
	  .data = (void *)MTK_DISP_BLENDER },
	{ .compatible = "mediatek,mt8167-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt8183-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt8192-disp-ccorr",
	  .data = (void *)MTK_DISP_CCORR },
	{ .compatible = "mediatek,mt2701-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8167-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8173-disp-color",
	  .data = (void *)MTK_DISP_COLOR },
	{ .compatible = "mediatek,mt8167-disp-dither",
	  .data = (void *)MTK_DISP_DITHER },
	{ .compatible = "mediatek,mt8183-disp-dither",
	  .data = (void *)MTK_DISP_DITHER },
	{ .compatible = "mediatek,mt8195-disp-dsc",
	  .data = (void *)MTK_DISP_DSC },
	{ .compatible = "mediatek,mt8196-disp-exdma",
	  .data = (void *)MTK_DISP_EXDMA },
	{ .compatible = "mediatek,mt8167-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8173-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8183-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8195-disp-gamma",
	  .data = (void *)MTK_DISP_GAMMA, },
	{ .compatible = "mediatek,mt8195-disp-merge",
	  .data = (void *)MTK_DISP_MERGE },
	{ .compatible = "mediatek,mt2701-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt2712-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8167-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8173-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8183-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8186-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8188-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8192-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8195-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8365-disp-mutex",
	  .data = (void *)MTK_DISP_MUTEX },
	{ .compatible = "mediatek,mt8173-disp-od",
	  .data = (void *)MTK_DISP_OD },
	{ .compatible = "mediatek,mt8196-disp-outproc",
	  .data = (void *)MTK_DISP_OUTPROC },
	{ .compatible = "mediatek,mt2701-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8167-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8173-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8183-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8192-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8195-disp-ovl",
	  .data = (void *)MTK_DISP_OVL },
	{ .compatible = "mediatek,mt8183-disp-ovl-2l",
	  .data = (void *)MTK_DISP_OVL_2L },
	{ .compatible = "mediatek,mt8192-disp-ovl-2l",
	  .data = (void *)MTK_DISP_OVL_2L },
	{ .compatible = "mediatek,mt8192-disp-postmask",
	  .data = (void *)MTK_DISP_POSTMASK },
	{ .compatible = "mediatek,mt2701-disp-pwm",
	  .data = (void *)MTK_DISP_BLS },
	{ .compatible = "mediatek,mt8167-disp-pwm",
	  .data = (void *)MTK_DISP_PWM },
	{ .compatible = "mediatek,mt8173-disp-pwm",
	  .data = (void *)MTK_DISP_PWM },
	{ .compatible = "mediatek,mt2701-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8167-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8173-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8183-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8195-disp-rdma",
	  .data = (void *)MTK_DISP_RDMA },
	{ .compatible = "mediatek,mt8173-disp-ufoe",
	  .data = (void *)MTK_DISP_UFOE },
	{ .compatible = "mediatek,mt6893-disp-wdma",
	  .data = (void *)MTK_DISP_WDMA },
	{ .compatible = "mediatek,mt8173-disp-wdma",
	  .data = (void *)MTK_DISP_WDMA },
	{ .compatible = "mediatek,mt2701-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt8167-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8173-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt8183-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt8186-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt8188-dp-intf",
	  .data = (void *)MTK_DISP_DP_INTF },
	{ .compatible = "mediatek,mt8192-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt8195-dp-intf",
	  .data = (void *)MTK_DISP_DP_INTF },
	{ .compatible = "mediatek,mt8195-dpi",
	  .data = (void *)MTK_DISP_DPI },
	{ .compatible = "mediatek,mt2701-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8173-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8183-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8186-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8188-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8189-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8196-dsi",
	  .data = (void *)MTK_DISP_DSI },
	{ .compatible = "mediatek,mt8189-dp-dvo",
	  .data = (void *)MTK_DISP_DVO },
	{ .compatible = "mediatek,mt8189-edp-dvo",
	  .data = (void *)MTK_DISP_DVO },
	{ .compatible = "mediatek,mt8196-edp-dvo",
	  .data = (void *)MTK_DISP_DVO },
	{ }
};

static int mtk_drm_of_get_ddp_comp_type(struct device_node *node, enum mtk_ddp_comp_type *ctype)
{
	const struct of_device_id *of_id = of_match_node(mtk_ddp_comp_dt_ids, node);

	if (!of_id)
		return -EINVAL;

	*ctype = (enum mtk_ddp_comp_type)((uintptr_t)of_id->data);

	return 0;
}

/**
 * mtk_drm_of_get_ep_external_controller() - Get parent controller if external
 * @dev:         Device pointer to leading display controller
 * @ep_dev_node: OF Node pointer to a display controller sub-component hardware
 *               The caller is responsible for dropping the refcount.
 *
 * Return: External Display Controller (mmsys) device_node or NULL if the given
 *         sub-component resides in the same Display Controller as *dev.
 */
static struct device_node
*mtk_drm_of_get_ep_external_controller(struct device *dev,
				       struct device_node *ep_dev_node)
{
	struct device_node *leader_controller_node = dev->parent->of_node;
	struct device_node *ep_parent_node;

	ep_parent_node = of_get_parent(ep_dev_node);
	if (ep_parent_node == leader_controller_node) {
		of_node_put(ep_parent_node);
		return NULL;
	}

	return ep_parent_node;
}

static int mtk_drm_of_get_first_input(struct device *dev, struct device_node *node,
				      enum mtk_crtc_path crtc_endpoint,
				      struct mtk_drm_comp_definition *comp_def)
{
	struct device_node *ep_dev_node, *ep_in;
	enum mtk_ddp_comp_type comp_type;
	int inst_id, ret;

	ep_in = of_graph_get_endpoint_by_regs(node, 0, crtc_endpoint);
	if (!ep_in)
		return -ENOENT;

	ep_dev_node = of_graph_get_port_parent(ep_in);
	of_node_put(ep_in);
	if (!ep_dev_node)
		return -EINVAL;

	/*
	 * If the first input has no HW component specific driver go out with
	 * -ENOENT: depending on the SoC (for arch gen2), this may be expected.
	 */
	ret = mtk_drm_of_get_ddp_comp_type(ep_dev_node, &comp_type);
	of_node_put(ep_dev_node);
	if (ret)
		return -ENOENT;

	inst_id = mtk_ddp_comp_get_id(ep_dev_node, comp_type);
	if (inst_id < 0)
		return inst_id;

	/* All ok! Pass the Component ID to the caller. */
	comp_def->type = comp_type;
	comp_def->inst_id = inst_id;

	dev_dbg(dev, "Found first input component %pOF with ID=%u SubID=%u\n",
		ep_dev_node, comp_def->type, comp_def->inst_id);

	return 0;
}

/**
 * mtk_drm_of_get_ddp_ep_cid - Parse HW component connection information
 * @dev:          The mediatek-drm device
 * @node:         The device node of the display controller component to parse
 * @output_port:  The number of the port, corresponding to an output, to parse
 * @crtc_endpoint:The number of the current endpoint corresponding to CRTC
 * @next:         Pointer to a struct device_node, used to pass the next node,
 *                corresponding to the next component's input, to the caller
 * @comp_def:     Pointer to the last, uninitialized, entry of the temporary
 *                structure array holding the Display Controller Path that is
 *                being built.
 * @controller_arch_v2: Check if DirectLink architecture or legacy VDO/MMSYS
 *
 * Return:
 * * %0        - Component connection parsed fully: the currently parsed Display
 *               Controller hardware component is interconnected with a next one
 * * %-ENOENT  - The component's remote endpoint was not found
 * * %-EINVAL  - Component information is not valid, hence not usable
 * * %-ENODEV  - The identified component is a valid connection, but its DT node
 *               is disabled, hence not usable
 * * %-EREMOTE - The component is interconnected with a next one residing in a
 *               different Display Controller, remote to the current one, hence
 *               cannot be added to the path of the current controller
 */
static int mtk_drm_of_get_ddp_ep_cid(struct device *dev, struct device_node *node,
				     int output_port, enum mtk_crtc_path crtc_endpoint,
				     struct device_node **next,
				     struct mtk_drm_comp_definition *comp_def,
				     bool controller_arch_v2)
{
	struct device_node *ep_dev_node, *ep_out;
	enum mtk_ddp_comp_type comp_type;
	int ret;

	ep_out = of_graph_get_endpoint_by_regs(node, output_port, crtc_endpoint);
	if (!ep_out)
		return -EINVAL;

	ep_dev_node = of_graph_get_remote_port_parent(ep_out);
	of_node_put(ep_out);
	if (!ep_dev_node)
		return -EINVAL;

	/*
	 * Pass the next node pointer regardless of failures in the later code
	 * so that if this function is called in a loop it will walk through all
	 * of the subsequent endpoints anyway.
	 */
	*next = ep_dev_node;

	if (controller_arch_v2) {
		struct device_node *rmt_ctrlr_node;

		rmt_ctrlr_node = mtk_drm_of_get_ep_external_controller(dev, ep_dev_node);
		if (rmt_ctrlr_node) {
			/* The device is from a different mmsys (remote from this one) */
			dev_dbg(dev, "Found connection to external mmsys %pOF\n",
				rmt_ctrlr_node);

			of_node_put(ep_dev_node);
			of_node_put(rmt_ctrlr_node);
			return -EREMOTE;
		}
	}

	if (!of_device_is_available(ep_dev_node))
		return -ENODEV;

	ret = mtk_drm_of_get_ddp_comp_type(ep_dev_node, &comp_type);
	if (ret) {
		if (mtk_ovl_adaptor_is_comp_present(ep_dev_node)) {
			comp_def->type = MTK_DISP_OVL_ADAPTOR;
			comp_def->inst_id = 0;

			return 0;
		}
		return ret;
	}

	ret = mtk_ddp_comp_get_id(ep_dev_node, comp_type);
	if (ret < 0)
		return ret;

	/* All ok! Pass the Component ID to the caller. */
	comp_def->type = comp_type;
	comp_def->inst_id = ret;

	dev_vdbg(dev, "Found component %pOF with Type:%u, HW Instance:%u\n",
		 ep_dev_node, comp_def->type, comp_def->inst_id);

	return 0;
}

/**
 * mtk_drm_of_ddp_path_build_one - Build a Display HW Pipeline for a CRTC Path
 * @dev:          The mediatek-drm device, corresponding to leading controller
 *                instance of the current display HW pipeline
 * @node:         The device node containing the first port/endpoint
 * @cpath:        CRTC Path relative to a VDO or MMSYS, also used as
 *                number of the initial endpoint for this CRTC path
 * @out_path:     Pointer to the structure that will contain the new pipeline
 * @controller_arch_v2: Check if DirectLink architecture or legacy VDO/MMSYS
 *
 * MediaTek SoCs can use different DDP hardware pipelines (or paths) depending
 * on the board-specific desired display configuration; this function walks
 * through all of the output endpoints starting from a VDO or MMSYS hardware
 * instance and builds the right pipeline as specified in device trees.
 *
 * Return:
 * * %0       - Display HW Pipeline successfully built and validated
 * * %-ENOENT - Display pipeline was not specified in device tree
 * * %-EINVAL - Display pipeline built but validation failed
 * * %-ENOMEM - Failure to allocate pipeline array to pass to the caller
 */
static int mtk_drm_of_ddp_path_build_one(struct device *dev, struct device_node *node,
					 enum mtk_crtc_path cpath,
					 struct mtk_drm_path_definition *out_path,
					 bool controller_arch_v2)
{
	struct mtk_drm_comp_definition temp_path[MTK_DISP_CONTROLLER_MAX_COMP_PER_PATH];
	struct device_node *next = NULL, *prev;
	bool ovl_adaptor_comp_added = false;
	unsigned short int idx = 0;
	size_t final_comp_sz;
	u8 temp_order;
	int ret;

	dev_vdbg(dev, "Building DDP Path for CRTC%d\n", cpath);

	if (controller_arch_v2) {
		/* Check if the starting input is already a usable component */
		ret = mtk_drm_of_get_first_input(dev, node, cpath, &temp_path[idx]);
		if (ret == 0) {
			idx++;
		} else if (ret != -ENOENT) {
			dev_err(dev, "Cannot parse first input HW component: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Get the first remote for the temp_path array: for leader controllers
	 * this will be an output, while for follower controllers this will be
	 * an input from a DirectLink connection coming from either a leader or
	 * a follower display controller.
	 *
	 * In case this is an input from any remote (leader/follower) controller
	 * this will return EREMOTE and a different port (expressing a relay of
	 * a crossbar) will be used to continue building the path.
	 */
	ret = mtk_drm_of_get_ddp_ep_cid(dev, node, 0, cpath, &next,
					&temp_path[idx], controller_arch_v2);
	if (ret == -EREMOTE) {
		out_path->input_controller = mtk_drm_of_get_ep_external_controller(dev, next);

		/*
		 * Any follower controller gets the order set to 1 to avoid
		 * iterating once again later when the actual full ordering
		 * is calculated.
		 */
		temp_order = 1;
		dev_dbg(dev, "Got external mmsys %pOF\n", out_path->input_controller);

		ret = mtk_drm_of_get_ddp_ep_cid(dev, node, 2, cpath, &next,
						&temp_path[idx], controller_arch_v2);
	} else {
		/* A leader controller gets, of course, its order set to 0 */
		temp_order = 0;
	}

	if (ret) {
		if (next && temp_path[idx].type == MTK_DISP_OVL_ADAPTOR) {
			dev_dbg(dev, "Adding OVL Adaptor for %pOF\n", next);
			ovl_adaptor_comp_added = true;
		} else {
			if (next)
				dev_err(dev, "Invalid component %pOF\n", next);
			else
				dev_err(dev,
					"Cannot find first endpoint for path %d on %pOF\n",
					cpath, node);

			return ret;
		}
	}
	idx++;

	/*
	 * Walk through port outputs until we reach the last valid mediatek-drm component.
	 * To be valid, this must end with an "invalid" component that is a display node.
	 */
	do {
		prev = next;
		ret = mtk_drm_of_get_ddp_ep_cid(dev, next, 1, cpath, &next,
						&temp_path[idx], controller_arch_v2);
		if (ret == -EREMOTE)
			ret = mtk_drm_of_get_ddp_ep_cid(dev, prev, 3, cpath, &next,
							&temp_path[idx], controller_arch_v2);
		of_node_put(prev);
		if (ret) {
			dev_vdbg(dev, "Invalid comp reached with result %d\n", ret);
			of_node_put(next);
			break;
		}

		/*
		 * If this is an OVL adaptor exclusive component and one of those
		 * was already added, don't add another instance of the generic
		 * DDP_COMPONENT_OVL_ADAPTOR, as this is used only to decide whether
		 * to probe that component master driver of which only one instance
		 * is needed and possible.
		 */
		if (temp_path[idx].type == MTK_DISP_OVL_ADAPTOR) {
			if (!ovl_adaptor_comp_added)
				ovl_adaptor_comp_added = true;
			else
				idx--;
		}
	} while (++idx < MTK_DISP_CONTROLLER_MAX_COMP_PER_PATH);

	/*
	 * The device component might not be enabled: in that case, don't
	 * check the last entry and just report that the device is missing.
	 */
	if (ret == -ENODEV)
		return ret;

	/* If the last entry is not a final display output, the configuration is wrong */
	switch (temp_path[idx - 1].type) {
	case MTK_DISP_DP_INTF:
	case MTK_DISP_DPI:
	case MTK_DISP_DSI:
	case MTK_DISP_DVO:
		break;
	default:
		dev_err(dev, "Invalid display hw pipeline. Last component: %u-%u (ret=%d)\n",
			temp_path[idx - 1].type, temp_path[idx - 1].inst_id, ret);
		return -EINVAL;
	}

	/* Pipeline built! */
	out_path->len = idx;
	final_comp_sz = out_path->len * sizeof(out_path->comp[0]);
	out_path->comp = devm_kmemdup(dev, temp_path, final_comp_sz, GFP_KERNEL);
	if (!out_path->comp)
		return -ENOMEM;

	/*
	 * Anything that is not the primary controller gets order set to 1:
	 * this is done to avoid iterating once again later when the actual
	 * full controllers ordering is calculated.
	 */
	out_path->order = temp_order;

	dev_dbg(dev, "Display HW Pipeline built with %d components.\n", idx);

	return 0;
}

static bool mtk_drm_of_ddp_is_arch_v2(struct device_node *cur_mmsys_node)
{
	for_each_child_of_node_scoped(cur_mmsys_node, mmsys_child)
		if (of_property_present(mmsys_child, "compatible"))
			return true;

	return false;
}

static int mtk_drm_of_ddp_path_build(struct device *dev, struct device_node *node,
				     struct mtk_mmsys_driver_data *data)
{
	struct mtk_drm_path_definition *output_paths;
	struct device_node *ep_node;
	struct of_endpoint of_ep;
	bool output_present[MAX_CRTC] = { false };
	u8 num_outputs_present = 0;
	u8 num_outputs_skipped = 0;
	bool controller_arch_v2;
	int i, ret;

	controller_arch_v2 = mtk_drm_of_ddp_is_arch_v2(dev->parent->of_node);

	dev_dbg(dev, "Building Display Controller v%d Path starting from %pOF\n",
		controller_arch_v2 ? 2 : 1, node);

	for_each_endpoint_of_node(node, ep_node) {
		ret = of_graph_parse_endpoint(ep_node, &of_ep);
		if (ret) {
			dev_err_probe(dev, ret, "Cannot parse endpoint\n");
			break;
		}

		if (of_ep.id >= MAX_CRTC) {
			ret = dev_err_probe(dev, -EINVAL,
					    "Invalid endpoint%u number\n", of_ep.port);
			break;
		}

		output_present[of_ep.id] = true;
		num_outputs_present++;
	}
	if (ret == 0 && num_outputs_present == 0)
		ret = dev_err_probe(dev, -ENXIO, "No display outputs found.\n");

	if (ret) {
		of_node_put(ep_node);
		return ret;
	}

	/* This can be optimized after making CRTC numbers dynamic */
	output_paths = devm_kcalloc(dev, MAX_CRTC, sizeof(*output_paths), GFP_KERNEL);
	if (!output_paths)
		return -ENOMEM;

	for (i = 0; i < MAX_CRTC; i++) {
		if (!output_present[i])
			continue;

		ret = mtk_drm_of_ddp_path_build_one(dev, node, i, &output_paths[i],
						    controller_arch_v2);
		/*
		 * For -ENODEV, this could mean that the device is not yet registered,
		 * but that may be just because of a probe deferral, so it is possible
		 * to continue building the path as such failures are properly handled
		 * later when enabling outputs.
		 * For -ENOENT, it means that the devicetree declares a partial output
		 * which is - of course - not okay, but failing entirely is a bit too
		 * much: do a print to advertise invalid outputs, but fail probing
		 * only if there is no valid output at all.
		 */
		if (ret && ret != -ENODEV) {
			if (ret == -ENOENT) {
				dev_dbg(dev, "Skipping invalid output for CRTC%u\n", i);
				num_outputs_skipped++;
			} else {
				dev_err(dev, "Pipeline build failure on CRTC%u\n", i);
				return ret;
			}
		}
	}

	if (num_outputs_skipped == num_outputs_present) {
		dev_err(dev, "No valid display output found!\n");
		return -ENOENT;
	}

	data->output_paths = output_paths;

	return 0;
}

static int mtk_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *phandle = dev->parent->of_node;
	const struct of_device_id *of_id;
	struct mtk_drm_private *private;
	struct mtk_mmsys_driver_data *mtk_drm_data;
	struct device_node *node;
	struct component_match *match = NULL;
	int ret;

	private = devm_kzalloc(dev, sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	private->mmsys_dev = dev->parent;
	if (!private->mmsys_dev) {
		dev_err(dev, "Failed to get MMSYS device\n");
		return -ENODEV;
	}

	of_id = of_match_node(mtk_drm_of_ids, phandle);
	if (!of_id)
		return -ENODEV;

	mtk_drm_data = (struct mtk_mmsys_driver_data *)of_id->data;
	if (!mtk_drm_data)
		return -EINVAL;

	hash_init(private->hlist.ddp_list);

	/* Try to build the display pipeline from devicetree graphs */
	if (of_graph_is_present(phandle)) {
		dev_dbg(dev, "Building display pipeline for MMSYS %u\n",
			mtk_drm_data->mmsys_id);
		private->data = devm_kmemdup(dev, mtk_drm_data,
					     sizeof(*mtk_drm_data), GFP_KERNEL);
		if (!private->data)
			return -ENOMEM;

		ret = mtk_drm_of_ddp_path_build(dev, phandle, private->data);
		if (ret)
			return ret;
	} else {
		/* No devicetree graphs support: go with hardcoded paths if present */
		dev_dbg(dev, "Using hardcoded paths for MMSYS %u\n", mtk_drm_data->mmsys_id);
		private->data = mtk_drm_data;
	}

	private->all_drm_private = devm_kmalloc_array(dev, private->data->mmsys_dev_num,
						      sizeof(*private->all_drm_private),
						      GFP_KERNEL);
	if (!private->all_drm_private)
		return -ENOMEM;

	/* Iterate over sibling DISP function blocks */
	for_each_child_of_node(phandle->parent, node) {
		enum mtk_ddp_comp_type comp_type;
		u8 comp_inst_id;

		ret = mtk_drm_of_get_ddp_comp_type(node, &comp_type);
		if (ret)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled component %pOF\n",
				node);
			continue;
		}

		if (comp_type == MTK_DISP_MUTEX) {
			int id;

			id = of_alias_get_id(node, "mutex");
			if (id < 0 || id == private->data->mmsys_id) {
				private->mutex_node = of_node_get(node);
				dev_dbg(dev, "get mutex for mmsys %d", private->data->mmsys_id);
			}
			continue;
		}

		comp_inst_id = mtk_ddp_comp_get_id(node, comp_type);
		if (comp_inst_id < 0) {
			dev_warn(dev, "Skipping unknown component %pOF\n",
				 node);
			continue;
		}

		if (!mtk_drm_find_mmsys_comp(private, comp_type, comp_inst_id))
			continue;

		/*
		 * Currently only the AAL, CCORR, COLOR, GAMMA, MERGE, OVL, RDMA, DSI, and DPI
		 * blocks have separate component platform drivers and initialize their own
		 * DDP component structure. The others are initialized here.
		 */
		if (!mtk_ddp_comp_is_internal_comp(comp_type) &&
		    !mtk_ovl_adaptor_is_comp_present(node)) {
			dev_info(dev, "Adding component match for %pOF\n",
				 node);
			drm_of_component_match_add(dev, &match, component_compare_of,
						   node);
		}

		ret = mtk_ddp_comp_init(dev, node, &private->hlist,
					private->data->mmsys_id,
					comp_type, comp_inst_id);
		if (ret) {
			of_node_put(node);
			goto err_node;
		}
	}

	if (!private->mutex_node) {
		dev_err(dev, "Failed to find disp-mutex node\n");
		ret = -ENODEV;
		goto err_node;
	}

	/* If mtk-mutex is not a trigger source, this is an old devicetree */
	if (!of_property_present(private->mutex_node, "#trigger-source-cells")) {
		ret = mtk_drm_legacy_inject_mutex_trig_ids(&private->hlist, private->mutex_node);
		if (ret)
			return ret;
	}

	/* Bringup ovl_adaptor */
	if (mtk_drm_find_mmsys_comp(private, MTK_DISP_OVL_ADAPTOR, 0))
		mtk_drm_legacy_ovl_adaptor_probe(dev, private, &match);

	pm_runtime_enable(dev);

	platform_set_drvdata(pdev, private);

	ret = component_master_add_with_match(dev, &mtk_drm_ops, match);
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_disable(dev);
err_node:
	of_node_put(private->mutex_node);
	return ret;
}

static void mtk_drm_remove(struct platform_device *pdev)
{
	struct mtk_drm_private *private = platform_get_drvdata(pdev);

	component_master_del(&pdev->dev, &mtk_drm_ops);
	pm_runtime_disable(&pdev->dev);
	of_node_put(private->mutex_node);
}

static void mtk_drm_shutdown(struct platform_device *pdev)
{
	struct mtk_drm_private *private = platform_get_drvdata(pdev);

	drm_atomic_helper_shutdown(private->drm);
}

static int mtk_drm_sys_prepare(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;

	if (private->drm_master)
		return drm_mode_config_helper_suspend(drm);
	else
		return 0;
}

static void mtk_drm_sys_complete(struct device *dev)
{
	struct mtk_drm_private *private = dev_get_drvdata(dev);
	struct drm_device *drm = private->drm;
	int ret = 0;

	if (private->drm_master)
		ret = drm_mode_config_helper_resume(drm);
	if (ret)
		dev_err(dev, "Failed to resume\n");
}

static const struct dev_pm_ops mtk_drm_pm_ops = {
	.prepare = mtk_drm_sys_prepare,
	.complete = mtk_drm_sys_complete,
};

static struct platform_driver mtk_drm_platform_driver = {
	.probe	= mtk_drm_probe,
	.remove = mtk_drm_remove,
	.shutdown = mtk_drm_shutdown,
	.driver	= {
		.name	= "mediatek-drm",
		.pm     = &mtk_drm_pm_ops,
	},
};

static struct platform_driver * const mtk_drm_drivers[] = {
	&mtk_disp_aal_driver,
	&mtk_disp_blender_driver,
	&mtk_disp_ccorr_driver,
	&mtk_disp_color_driver,
	&mtk_disp_dsc_driver,
	&mtk_disp_exdma_driver,
	&mtk_disp_gamma_driver,
	&mtk_disp_merge_driver,
	&mtk_disp_outproc_driver,
	&mtk_disp_ovl_adaptor_driver,
	&mtk_disp_ovl_driver,
	&mtk_disp_rdma_driver,
	&mtk_disp_wdma_driver,
	&mtk_dpi_driver,
	&mtk_dvo_driver,
	&mtk_drm_platform_driver,
	&mtk_dsi_driver,
	&mtk_ethdr_driver,
	&mtk_mdp_rdma_driver,
	&mtk_padding_driver,
};

static int __init mtk_drm_init(void)
{
	return platform_register_drivers(mtk_drm_drivers,
					 ARRAY_SIZE(mtk_drm_drivers));
}

static void __exit mtk_drm_exit(void)
{
	platform_unregister_drivers(mtk_drm_drivers,
				    ARRAY_SIZE(mtk_drm_drivers));
}

module_init(mtk_drm_init);
module_exit(mtk_drm_exit);

MODULE_AUTHOR("YT SHEN <yt.shen@mediatek.com>");
MODULE_DESCRIPTION("Mediatek SoC DRM driver");
MODULE_IMPORT_NS("MTK_MMSYS");
MODULE_IMPORT_NS("MTK_MUTEX");
MODULE_LICENSE("GPL v2");
