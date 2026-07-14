// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compatibility layer for legacy mediatek-drm
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 *
 * All (or most of) MediaTek SoCs released since around year 2014 onwards
 * have got a new Multimedia System (and Display Controller) architecture,
 * featuring an extreme flexibility on the connection of all of the various
 * multimedia-related hardware components (or sub-IPs).
 *
 * Many different boards based on those SoCs are using different displays,
 * different outputs, hence wildly different display pipelines: for this,
 * a solution based on a devicetree graph (OF Graph) was chosen for setting
 * up the correct pipeline for each device.
 *
 * However, removing the hardcoded display controller paths would break all
 * of the devices using the new display driver on an old devicetree.
 *
 * This compatibility layer makes possible to keep the display controller
 * functionality working when a board/device:
 *  - Uses an old devicetree with no OF Graph; and
 *  - Uses a new kernel with the new mediatek-drm graph-based pipeline
 *    building code.
 *
 *                            ** WARNING **
 * This exists only to avoid ABI breakages and no new SoC should ever be
 * added to this file.
 */

#include "mtk_drm_drv.h"
#include "mtk_drm_legacy.h"

static const struct mtk_drm_comp_definition mt2701_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_BLS },
	{ DDP_COMPONENT_DSI0 },
};

static const struct mtk_drm_comp_definition mt2701_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DPI0 },
};

struct mtk_drm_path_definition mt2701_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt2701_mtk_ddp_main,
		.len = ARRAY_SIZE(mt2701_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt2701_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt2701_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt2712_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_OD0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_DPI0 },
	{ DDP_COMPONENT_PWM0 },
};

static const struct mtk_drm_comp_definition mt2712_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_OVL1 },
	{ DDP_COMPONENT_COLOR1 },
	{ DDP_COMPONENT_AAL1 },
	{ DDP_COMPONENT_OD1 },
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DPI1 },
	{ DDP_COMPONENT_PWM1 },
};

static const struct mtk_drm_comp_definition mt2712_mtk_ddp_third[] = {
	{ DDP_COMPONENT_RDMA2 },
	{ DDP_COMPONENT_DSI3 },
	{ DDP_COMPONENT_PWM2 },
};

struct mtk_drm_path_definition mt2712_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt2712_mtk_ddp_main,
		.len = ARRAY_SIZE(mt2712_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt2712_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt2712_mtk_ddp_ext),
	},
	[CRTC_THIRD] = {
		.comp = mt2712_mtk_ddp_third,
		.len = ARRAY_SIZE(mt2712_mtk_ddp_third),
	},
};

static const struct mtk_drm_comp_definition mt7623_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_BLS },
	{ DDP_COMPONENT_DPI0 },
};

static const struct mtk_drm_comp_definition mt7623_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DSI0 },
};

struct mtk_drm_path_definition mt7623_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt7623_mtk_ddp_main,
		.len = ARRAY_SIZE(mt7623_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt7623_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt7623_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt8167_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_DITHER0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_DSI0 },
};

struct mtk_drm_path_definition mt8167_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8167_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8167_mtk_ddp_main),
	},
};

static const struct mtk_drm_comp_definition mt8173_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_OD0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_UFOE },
	{ DDP_COMPONENT_DSI0 },
	{ DDP_COMPONENT_PWM0 },
};

static const struct mtk_drm_comp_definition mt8173_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_OVL1 },
	{ DDP_COMPONENT_COLOR1 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DPI0 },
};

struct mtk_drm_path_definition mt8173_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8173_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8173_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt8173_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt8173_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt8183_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_OVL_2L0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_DITHER0 },
	{ DDP_COMPONENT_DSI0 },
};

static const struct mtk_drm_comp_definition mt8183_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_OVL_2L1 },
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DPI0 },
};

struct mtk_drm_path_definition mt8183_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8183_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8183_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt8183_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt8183_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt8186_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_POSTMASK0 },
	{ DDP_COMPONENT_DITHER0 },
	{ DDP_COMPONENT_DSI0 },
};

static const struct mtk_drm_comp_definition mt8186_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_OVL_2L0 },
	{ DDP_COMPONENT_RDMA1 },
	{ DDP_COMPONENT_DPI0 },
};

struct mtk_drm_path_definition mt8186_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8186_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8186_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt8186_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt8186_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt8188_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_POSTMASK0 },
	{ DDP_COMPONENT_DITHER0 },
};

struct mtk_drm_path_definition mt8188_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8188_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8188_mtk_ddp_main),
	},
};

static const struct mtk_drm_comp_definition mt8192_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_OVL_2L0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_POSTMASK0 },
	{ DDP_COMPONENT_DITHER0 },
	{ DDP_COMPONENT_DSI0 },
};

static const struct mtk_drm_comp_definition mt8192_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_OVL_2L2 },
	{ DDP_COMPONENT_RDMA4 },
	{ DDP_COMPONENT_DPI0 },
};

struct mtk_drm_path_definition mt8192_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8192_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8192_mtk_ddp_main),
	},
	[CRTC_EXT] = {
		.comp = mt8192_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt8192_mtk_ddp_ext),
	},
};

static const struct mtk_drm_comp_definition mt8195_mtk_ddp_main[] = {
	{ DDP_COMPONENT_OVL0 },
	{ DDP_COMPONENT_RDMA0 },
	{ DDP_COMPONENT_COLOR0 },
	{ DDP_COMPONENT_CCORR },
	{ DDP_COMPONENT_AAL0 },
	{ DDP_COMPONENT_GAMMA },
	{ DDP_COMPONENT_DITHER0 },
	{ DDP_COMPONENT_DSC0 },
	{ DDP_COMPONENT_MERGE0 },
	{ DDP_COMPONENT_DP_INTF0 },
};

static const struct mtk_drm_comp_definition mt8195_mtk_ddp_ext[] = {
	{ DDP_COMPONENT_DRM_OVL_ADAPTOR },
	{ DDP_COMPONENT_MERGE5 },
	{ DDP_COMPONENT_DP_INTF1 },
};

struct mtk_drm_path_definition mt8195_vdo0_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8195_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8195_mtk_ddp_main),
	},
};

struct mtk_drm_path_definition mt8195_vdo1_legacy_paths[MAX_CRTC] = {
	[CRTC_EXT] = {
		.comp = mt8195_mtk_ddp_ext,
		.len = ARRAY_SIZE(mt8195_mtk_ddp_ext),
	},
};
