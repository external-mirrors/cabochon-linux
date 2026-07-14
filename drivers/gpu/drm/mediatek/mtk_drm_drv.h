/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_DRV_H
#define MTK_DRM_DRV_H

#include <linux/io.h>
#include "mtk_ddp_comp.h"

#define MAX_CONNECTOR	2

enum mtk_crtc_path {
	CRTC_MAIN,
	CRTC_EXT,
	CRTC_THIRD,
	MAX_CRTC,
};

struct device;
struct device_node;
struct drm_crtc;
struct drm_device;
struct drm_fb_helper;
struct drm_property;
struct regmap;

struct mtk_drm_route {
	const unsigned int crtc_id;
	const enum mtk_ddp_comp_type route_ddp_type;
	const u8 route_ddp_inst_id;
};

struct mtk_drm_comp_definition {
	enum mtk_ddp_comp_type type;
	u8 inst_id;
};

struct mtk_drm_path_definition {
	const struct mtk_drm_comp_definition *comp;
	u8 len;
};

struct mtk_mmsys_driver_data {
	struct mtk_drm_path_definition *output_paths;
	u8 num_output_paths;
	const struct mtk_drm_route *conn_routes;
	unsigned int num_conn_routes;

	bool shadow_register;
	unsigned int mmsys_id;
	unsigned int mmsys_dev_num;

	u16 max_width;
	u16 min_width;
	u16 min_height;
};

struct mtk_drm_private {
	struct drm_device *drm;
	bool mtk_drm_bound;
	bool drm_master;
	struct device *dev;
	struct device_node *mutex_node;
	struct device *mutex_dev;
	struct device *mmsys_dev;
	struct mtk_drm_comp_list hlist;
	struct mtk_mmsys_driver_data *data;
	struct drm_atomic_commit *suspend_state;
	unsigned int mbox_index;
	struct mtk_drm_private **all_drm_private;
};

extern struct platform_driver mtk_disp_aal_driver;
extern struct platform_driver mtk_disp_blender_driver;
extern struct platform_driver mtk_disp_ccorr_driver;
extern struct platform_driver mtk_disp_color_driver;
extern struct platform_driver mtk_disp_dsc_driver;
extern struct platform_driver mtk_disp_gamma_driver;
extern struct platform_driver mtk_disp_merge_driver;
extern struct platform_driver mtk_disp_ovl_adaptor_driver;
extern struct platform_driver mtk_disp_ovl_driver;
extern struct platform_driver mtk_disp_rdma_driver;
extern struct platform_driver mtk_disp_wdma_driver;
extern struct platform_driver mtk_dpi_driver;
extern struct platform_driver mtk_dsi_driver;
extern struct platform_driver mtk_dvo_driver;
extern struct platform_driver mtk_ethdr_driver;
extern struct platform_driver mtk_mdp_rdma_driver;
extern struct platform_driver mtk_padding_driver;
#endif /* MTK_DRM_DRV_H */
