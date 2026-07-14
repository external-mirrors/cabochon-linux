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

#include <linux/component.h>
#include <linux/of.h>

#include "mtk_drm_drv.h"
#include "mtk_drm_legacy.h"

#define MT2701_MUTEX_MOD_DISP_OVL		3
#define MT2701_MUTEX_MOD_DISP_WDMA		6
#define MT2701_MUTEX_MOD_DISP_COLOR		7
#define MT2701_MUTEX_MOD_DISP_BLS		9
#define MT2701_MUTEX_MOD_DISP_RDMA0		10
#define MT2701_MUTEX_MOD_DISP_RDMA1		12

#define MT2712_MUTEX_MOD_DISP_PWM2		10
#define MT2712_MUTEX_MOD_DISP_OVL0		11
#define MT2712_MUTEX_MOD_DISP_OVL1		12
#define MT2712_MUTEX_MOD_DISP_RDMA0		13
#define MT2712_MUTEX_MOD_DISP_RDMA1		14
#define MT2712_MUTEX_MOD_DISP_RDMA2		15
#define MT2712_MUTEX_MOD_DISP_WDMA0		16
#define MT2712_MUTEX_MOD_DISP_WDMA1		17
#define MT2712_MUTEX_MOD_DISP_COLOR0		18
#define MT2712_MUTEX_MOD_DISP_COLOR1		19
#define MT2712_MUTEX_MOD_DISP_AAL0		20
#define MT2712_MUTEX_MOD_DISP_UFOE		22
#define MT2712_MUTEX_MOD_DISP_PWM0		23
#define MT2712_MUTEX_MOD_DISP_PWM1		24
#define MT2712_MUTEX_MOD_DISP_OD0		25
#define MT2712_MUTEX_MOD2_DISP_AAL1		33
#define MT2712_MUTEX_MOD2_DISP_OD1		34

#define MT6893_MUTEX_MOD_DISP_OVL0		0
#define MT6893_MUTEX_MOD_DISP_OVL0_2L		1
#define MT6893_MUTEX_MOD_DISP_RDMA0		2
#define MT6893_MUTEX_MOD_DISP_WDMA0		3
#define MT6893_MUTEX_MOD_DISP_COLOR0		4
#define MT6893_MUTEX_MOD_DISP_CCORR0		5
#define MT6893_MUTEX_MOD_DISP_AAL0		6
#define MT6893_MUTEX_MOD_DISP_GAMMA0		7
#define MT6893_MUTEX_MOD_DISP_DITHER0		8
#define MT6893_MUTEX_MOD_DISP_DSI0		9
#define MT6893_MUTEX_MOD_DISP_PWM0		11
#define MT6893_MUTEX_MOD_DISP_OVL1		12
#define MT6893_MUTEX_MOD_DISP_OVL1_2L		13
#define MT6893_MUTEX_MOD_DISP_RDMA1		14
#define MT6893_MUTEX_MOD_DISP_WDMA1		15
#define MT6893_MUTEX_MOD_DISP_COLOR1		16
#define MT6893_MUTEX_MOD_DISP_AAL1		18
#define MT6893_MUTEX_MOD_DISP_DITHER1		20
#define MT6893_MUTEX_MOD_DISP_DSI1		21
#define MT6893_MUTEX_MOD_DISP_OVL2		23
#define MT6893_MUTEX_MOD_DISP_POSTMASK0		25
#define MT6893_MUTEX_MOD_DISP_MERGE0		27
#define MT6893_MUTEX_MOD_DISP_MERGE1		28
#define MT6893_MUTEX_MOD_DISP_DSC0		29
#define MT6893_MUTEX_MOD_DISP_DP		31
#define MT6893_MUTEX_MOD_DISP_RDMA4		34

#define MT8167_MUTEX_MOD_DISP_PWM		1
#define MT8167_MUTEX_MOD_DISP_OVL0		6
#define MT8167_MUTEX_MOD_DISP_OVL1		7
#define MT8167_MUTEX_MOD_DISP_RDMA0		8
#define MT8167_MUTEX_MOD_DISP_RDMA1		9
#define MT8167_MUTEX_MOD_DISP_WDMA0		10
#define MT8167_MUTEX_MOD_DISP_CCORR		11
#define MT8167_MUTEX_MOD_DISP_COLOR		12
#define MT8167_MUTEX_MOD_DISP_AAL		13
#define MT8167_MUTEX_MOD_DISP_GAMMA		14
#define MT8167_MUTEX_MOD_DISP_DITHER		15
#define MT8167_MUTEX_MOD_DISP_UFOE		16

#define MT8173_MUTEX_MOD_DISP_OVL0		11
#define MT8173_MUTEX_MOD_DISP_OVL1		12
#define MT8173_MUTEX_MOD_DISP_RDMA0		13
#define MT8173_MUTEX_MOD_DISP_RDMA1		14
#define MT8173_MUTEX_MOD_DISP_RDMA2		15
#define MT8173_MUTEX_MOD_DISP_WDMA0		16
#define MT8173_MUTEX_MOD_DISP_WDMA1		17
#define MT8173_MUTEX_MOD_DISP_COLOR0		18
#define MT8173_MUTEX_MOD_DISP_COLOR1		19
#define MT8173_MUTEX_MOD_DISP_AAL		20
#define MT8173_MUTEX_MOD_DISP_GAMMA		21
#define MT8173_MUTEX_MOD_DISP_UFOE		22
#define MT8173_MUTEX_MOD_DISP_PWM0		23
#define MT8173_MUTEX_MOD_DISP_PWM1		24
#define MT8173_MUTEX_MOD_DISP_OD		25

#define MT8183_MUTEX_MOD_DISP_RDMA0		0
#define MT8183_MUTEX_MOD_DISP_RDMA1		1
#define MT8183_MUTEX_MOD_DISP_OVL0		9
#define MT8183_MUTEX_MOD_DISP_OVL0_2L		10
#define MT8183_MUTEX_MOD_DISP_OVL1_2L		11
#define MT8183_MUTEX_MOD_DISP_WDMA0		12
#define MT8183_MUTEX_MOD_DISP_COLOR0		13
#define MT8183_MUTEX_MOD_DISP_CCORR0		14
#define MT8183_MUTEX_MOD_DISP_AAL0		15
#define MT8183_MUTEX_MOD_DISP_GAMMA0		16
#define MT8183_MUTEX_MOD_DISP_DITHER0		17

#define MT8186_MUTEX_MOD_DISP_OVL0		0
#define MT8186_MUTEX_MOD_DISP_OVL0_2L		1
#define MT8186_MUTEX_MOD_DISP_RDMA0		2
#define MT8186_MUTEX_MOD_DISP_COLOR0		4
#define MT8186_MUTEX_MOD_DISP_CCORR0		5
#define MT8186_MUTEX_MOD_DISP_AAL0		7
#define MT8186_MUTEX_MOD_DISP_GAMMA0		8
#define MT8186_MUTEX_MOD_DISP_POSTMASK0		9
#define MT8186_MUTEX_MOD_DISP_DITHER0		10
#define MT8186_MUTEX_MOD_DISP_RDMA1		17

#define MT8188_MUTEX_MOD_DISP_OVL0		0
#define MT8188_MUTEX_MOD_DISP_WDMA0		1
#define MT8188_MUTEX_MOD_DISP_RDMA0		2
#define MT8188_MUTEX_MOD_DISP_COLOR0		3
#define MT8188_MUTEX_MOD_DISP_CCORR0		4
#define MT8188_MUTEX_MOD_DISP_AAL0		5
#define MT8188_MUTEX_MOD_DISP_GAMMA0		6
#define MT8188_MUTEX_MOD_DISP_DITHER0		7
#define MT8188_MUTEX_MOD_DISP_DSI0		8
#define MT8188_MUTEX_MOD_DISP_DSC_WRAP0_CORE0	9
#define MT8188_MUTEX_MOD_DISP_VPP_MERGE		20
#define MT8188_MUTEX_MOD_DISP_DP_INTF0		21
#define MT8188_MUTEX_MOD_DISP_POSTMASK0		24
#define MT8188_MUTEX_MOD2_DISP_PWM0		33

#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA0	0
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA1	1
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA2	2
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA3	3
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA4	4
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA5	5
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA6	6
#define MT8188_MUTEX_MOD_DISP1_MDP_RDMA7	7
#define MT8188_MUTEX_MOD_DISP1_PADDING0		8
#define MT8188_MUTEX_MOD_DISP1_PADDING1		9
#define MT8188_MUTEX_MOD_DISP1_PADDING2		10
#define MT8188_MUTEX_MOD_DISP1_PADDING3		11
#define MT8188_MUTEX_MOD_DISP1_PADDING4		12
#define MT8188_MUTEX_MOD_DISP1_PADDING5		13
#define MT8188_MUTEX_MOD_DISP1_PADDING6		14
#define MT8188_MUTEX_MOD_DISP1_PADDING7		15
#define MT8188_MUTEX_MOD_DISP1_VPP_MERGE0	20
#define MT8188_MUTEX_MOD_DISP1_VPP_MERGE1	21
#define MT8188_MUTEX_MOD_DISP1_VPP_MERGE2	22
#define MT8188_MUTEX_MOD_DISP1_VPP_MERGE3	23
#define MT8188_MUTEX_MOD_DISP1_VPP_MERGE4	24
#define MT8188_MUTEX_MOD_DISP1_DISP_MIXER	30
#define MT8188_MUTEX_MOD_DISP1_DPI1		38
#define MT8188_MUTEX_MOD_DISP1_DP_INTF1		39

#define MT8192_MUTEX_MOD_DISP_OVL0		0
#define MT8192_MUTEX_MOD_DISP_OVL0_2L		1
#define MT8192_MUTEX_MOD_DISP_RDMA0		2
#define MT8192_MUTEX_MOD_DISP_COLOR0		4
#define MT8192_MUTEX_MOD_DISP_CCORR0		5
#define MT8192_MUTEX_MOD_DISP_AAL0		6
#define MT8192_MUTEX_MOD_DISP_GAMMA0		7
#define MT8192_MUTEX_MOD_DISP_POSTMASK0		8
#define MT8192_MUTEX_MOD_DISP_DITHER0		9
#define MT8192_MUTEX_MOD_DISP_OVL2_2L		16
#define MT8192_MUTEX_MOD_DISP_RDMA4		17

#define MT8195_MUTEX_MOD_DISP_OVL0		0
#define MT8195_MUTEX_MOD_DISP_WDMA0		1
#define MT8195_MUTEX_MOD_DISP_RDMA0		2
#define MT8195_MUTEX_MOD_DISP_COLOR0		3
#define MT8195_MUTEX_MOD_DISP_CCORR0		4
#define MT8195_MUTEX_MOD_DISP_AAL0		5
#define MT8195_MUTEX_MOD_DISP_GAMMA0		6
#define MT8195_MUTEX_MOD_DISP_DITHER0		7
#define MT8195_MUTEX_MOD_DISP_DSI0		8
#define MT8195_MUTEX_MOD_DISP_DSC_WRAP0_CORE0	9
#define MT8195_MUTEX_MOD_DISP_VPP_MERGE		20
#define MT8195_MUTEX_MOD_DISP_DP_INTF0		21
#define MT8195_MUTEX_MOD_DISP_PWM0		27

#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA0	0
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA1	1
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA2	2
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA3	3
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA4	4
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA5	5
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA6	6
#define MT8195_MUTEX_MOD_DISP1_MDP_RDMA7	7
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE0	8
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE1	9
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE2	10
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE3	11
#define MT8195_MUTEX_MOD_DISP1_VPP_MERGE4	12
#define MT8195_MUTEX_MOD_DISP1_DISP_MIXER	18
#define MT8195_MUTEX_MOD_DISP1_DPI0		25
#define MT8195_MUTEX_MOD_DISP1_DPI1		26
#define MT8195_MUTEX_MOD_DISP1_DP_INTF0		27

#define MT8365_MUTEX_MOD_DISP_OVL0		7
#define MT8365_MUTEX_MOD_DISP_OVL0_2L		8
#define MT8365_MUTEX_MOD_DISP_RDMA0		9
#define MT8365_MUTEX_MOD_DISP_RDMA1		10
#define MT8365_MUTEX_MOD_DISP_WDMA0		11
#define MT8365_MUTEX_MOD_DISP_COLOR0		12
#define MT8365_MUTEX_MOD_DISP_CCORR		13
#define MT8365_MUTEX_MOD_DISP_AAL		14
#define MT8365_MUTEX_MOD_DISP_GAMMA		15
#define MT8365_MUTEX_MOD_DISP_DITHER		16
#define MT8365_MUTEX_MOD_DISP_DSI0		17
#define MT8365_MUTEX_MOD_DISP_PWM0		20
#define MT8365_MUTEX_MOD_DISP_DPI0		22

struct mtk_drm_legacy_mtx_pairs {
	struct mtk_drm_comp_definition comp;
	u8 mtx_trig_id;
};

struct mtk_drm_legacy_mtx_data {
	const struct mtk_drm_legacy_mtx_pairs *pairs;
	u8 num_pairs;
};

static const struct mtk_drm_comp_definition mt2701_mtk_ddp_main[] = {
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_BLS, 0 },
	{ MTK_DISP_DSI, 0 },
};

static const struct mtk_drm_comp_definition mt2701_mtk_ddp_ext[] = {
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DPI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_OD, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_DPI, 0 },
	{ MTK_DISP_PWM, 0 },
};

static const struct mtk_drm_comp_definition mt2712_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL, 1 },
	{ MTK_DISP_COLOR, 1 },
	{ MTK_DISP_AAL, 1 },
	{ MTK_DISP_OD, 1 },
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DPI, 1 },
	{ MTK_DISP_PWM, 1 },
};

static const struct mtk_drm_comp_definition mt2712_mtk_ddp_third[] = {
	{ MTK_DISP_RDMA, 2 },
	{ MTK_DISP_DSI, 3 },
	{ MTK_DISP_PWM, 2 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_BLS, 0 },
	{ MTK_DISP_DPI, 0 },
};

static const struct mtk_drm_comp_definition mt7623_mtk_ddp_ext[] = {
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DSI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_DITHER, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_DSI, 0 },
};

struct mtk_drm_path_definition mt8167_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8167_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8167_mtk_ddp_main),
	},
};

static const struct mtk_drm_comp_definition mt8173_mtk_ddp_main[] = {
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_OD, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_UFOE, 0 },
	{ MTK_DISP_DSI, 0 },
	{ MTK_DISP_PWM, 0 },
};

static const struct mtk_drm_comp_definition mt8173_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL, 1 },
	{ MTK_DISP_COLOR, 1 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DPI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_OVL_2L, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_DITHER, 0 },
	{ MTK_DISP_DSI, 0 },
};

static const struct mtk_drm_comp_definition mt8183_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL_2L, 1 },
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DPI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_POSTMASK, 0 },
	{ MTK_DISP_DITHER, 0 },
	{ MTK_DISP_DSI, 0 },
};

static const struct mtk_drm_comp_definition mt8186_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL_2L, 0 },
	{ MTK_DISP_RDMA, 1 },
	{ MTK_DISP_DPI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_POSTMASK, 0 },
	{ MTK_DISP_DITHER, 0 },
};

struct mtk_drm_path_definition mt8188_legacy_paths[MAX_CRTC] = {
	[CRTC_MAIN] = {
		.comp = mt8188_mtk_ddp_main,
		.len = ARRAY_SIZE(mt8188_mtk_ddp_main),
	},
};

static const struct mtk_drm_comp_definition mt8192_mtk_ddp_main[] = {
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_OVL_2L, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_POSTMASK, 0 },
	{ MTK_DISP_DITHER, 0 },
	{ MTK_DISP_DSI, 0 },
};

static const struct mtk_drm_comp_definition mt8192_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL_2L, 2 },
	{ MTK_DISP_RDMA, 4 },
	{ MTK_DISP_DPI, 0 },
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
	{ MTK_DISP_OVL, 0 },
	{ MTK_DISP_RDMA, 0 },
	{ MTK_DISP_COLOR, 0 },
	{ MTK_DISP_CCORR, 0 },
	{ MTK_DISP_AAL, 0 },
	{ MTK_DISP_GAMMA, 0 },
	{ MTK_DISP_DITHER, 0 },
	{ MTK_DISP_DSC, 0 },
	{ MTK_DISP_MERGE, 0 },
	{ MTK_DISP_DP_INTF, 0 },
};

static const struct mtk_drm_comp_definition mt8195_mtk_ddp_ext[] = {
	{ MTK_DISP_OVL_ADAPTOR, 0 },
	{ MTK_DISP_MERGE, 5 },
	{ MTK_DISP_DP_INTF, 1 },
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

static const struct mtk_drm_legacy_mtx_pairs mt2701_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_BLS, 0 }, MT2701_MUTEX_MOD_DISP_BLS },
	{ { MTK_DISP_COLOR, 0 }, MT2701_MUTEX_MOD_DISP_COLOR },
	{ { MTK_DISP_OVL, 0 }, MT2701_MUTEX_MOD_DISP_OVL },
	{ { MTK_DISP_RDMA, 0 }, MT2701_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT2701_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_WDMA, 0 }, MT2701_MUTEX_MOD_DISP_WDMA },
};

const struct mtk_drm_legacy_mtx_data mt2701_legacy_mtx_data = {
	.pairs = mt2701_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt2701_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt2712_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT2712_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_AAL, 1 }, MT2712_MUTEX_MOD2_DISP_AAL1 },
	{ { MTK_DISP_COLOR, 0 }, MT2712_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_COLOR, 1 }, MT2712_MUTEX_MOD_DISP_COLOR1 },
	{ { MTK_DISP_OD, 0 }, MT2712_MUTEX_MOD_DISP_OD0 },
	{ { MTK_DISP_OD, 1 }, MT2712_MUTEX_MOD2_DISP_OD1 },
	{ { MTK_DISP_OVL, 0 }, MT2712_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL, 1 }, MT2712_MUTEX_MOD_DISP_OVL1 },
	{ { MTK_DISP_PWM, 0 }, MT2712_MUTEX_MOD_DISP_PWM0 },
	{ { MTK_DISP_PWM, 1 }, MT2712_MUTEX_MOD_DISP_PWM1 },
	{ { MTK_DISP_PWM, 2 }, MT2712_MUTEX_MOD_DISP_PWM2 },
	{ { MTK_DISP_RDMA, 0 }, MT2712_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT2712_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_RDMA, 2 }, MT2712_MUTEX_MOD_DISP_RDMA2 },
	{ { MTK_DISP_UFOE, 0 }, MT2712_MUTEX_MOD_DISP_UFOE },
	{ { MTK_DISP_WDMA, 0 }, MT2712_MUTEX_MOD_DISP_WDMA0 },
	{ { MTK_DISP_WDMA, 1 }, MT2712_MUTEX_MOD_DISP_WDMA1 },
};

const struct mtk_drm_legacy_mtx_data mt2712_legacy_mtx_data = {
	.pairs = mt2712_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt2712_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt6893_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT6893_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_AAL, 1 }, MT6893_MUTEX_MOD_DISP_AAL1 },
	{ { MTK_DISP_CCORR, 0 }, MT6893_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_COLOR, 0 }, MT6893_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_COLOR, 1 }, MT6893_MUTEX_MOD_DISP_COLOR1 },
	{ { MTK_DISP_DITHER, 0 }, MT6893_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_DITHER, 1 }, MT6893_MUTEX_MOD_DISP_DITHER1 },
	{ { MTK_DISP_DP_INTF, 0 }, MT6893_MUTEX_MOD_DISP_DP },
	{ { MTK_DISP_DSC, 0 }, MT6893_MUTEX_MOD_DISP_DSC0 },
	{ { MTK_DISP_DSI, 0 }, MT6893_MUTEX_MOD_DISP_DSI0 },
	{ { MTK_DISP_DSI, 1 }, MT6893_MUTEX_MOD_DISP_DSI1 },
	{ { MTK_DISP_GAMMA, 0 }, MT6893_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_MERGE, 1 }, MT6893_MUTEX_MOD_DISP_MERGE1 },
	{ { MTK_DISP_OVL, 0 }, MT6893_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL, 1 }, MT6893_MUTEX_MOD_DISP_OVL1 },
	{ { MTK_DISP_OVL_2L, 0 }, MT6893_MUTEX_MOD_DISP_OVL0_2L },
	{ { MTK_DISP_OVL_2L, 1 }, MT6893_MUTEX_MOD_DISP_OVL1_2L },
	{ { MTK_DISP_OVL_2L, 2 }, MT6893_MUTEX_MOD_DISP_OVL2 },
	{ { MTK_DISP_POSTMASK, 0 }, MT6893_MUTEX_MOD_DISP_POSTMASK0 },
	{ { MTK_DISP_PWM, 0 }, MT6893_MUTEX_MOD_DISP_PWM0 },
	{ { MTK_DISP_RDMA, 0 }, MT6893_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT6893_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_RDMA, 4 }, MT6893_MUTEX_MOD_DISP_RDMA4 },
	{ { MTK_DISP_WDMA, 0 }, MT6893_MUTEX_MOD_DISP_WDMA0 },
	{ { MTK_DISP_WDMA, 1 }, MT6893_MUTEX_MOD_DISP_WDMA1 },
};

const struct mtk_drm_legacy_mtx_data mt6893_legacy_mtx_data = {
	.pairs = mt6893_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt6893_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8167_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8167_MUTEX_MOD_DISP_AAL },
	{ { MTK_DISP_CCORR, 0 }, MT8167_MUTEX_MOD_DISP_CCORR },
	{ { MTK_DISP_COLOR, 0 }, MT8167_MUTEX_MOD_DISP_COLOR },
	{ { MTK_DISP_DITHER, 0 }, MT8167_MUTEX_MOD_DISP_DITHER },
	{ { MTK_DISP_GAMMA, 0 }, MT8167_MUTEX_MOD_DISP_GAMMA },
	{ { MTK_DISP_OVL, 0 }, MT8167_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL, 1 }, MT8167_MUTEX_MOD_DISP_OVL1 },
	{ { MTK_DISP_PWM, 0 }, MT8167_MUTEX_MOD_DISP_PWM },
	{ { MTK_DISP_RDMA, 0 }, MT8167_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT8167_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_UFOE, 0 }, MT8167_MUTEX_MOD_DISP_UFOE },
	{ { MTK_DISP_WDMA, 0 }, MT8167_MUTEX_MOD_DISP_WDMA0 },
};

const struct mtk_drm_legacy_mtx_data mt8167_legacy_mtx_data = {
	.pairs = mt8167_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8167_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8173_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8173_MUTEX_MOD_DISP_AAL },
	{ { MTK_DISP_COLOR, 0 }, MT8173_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_COLOR, 1 }, MT8173_MUTEX_MOD_DISP_COLOR1 },
	{ { MTK_DISP_GAMMA, 0 }, MT8173_MUTEX_MOD_DISP_GAMMA },
	{ { MTK_DISP_OD, 0 }, MT8173_MUTEX_MOD_DISP_OD },
	{ { MTK_DISP_OVL, 0 }, MT8173_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL, 1 }, MT8173_MUTEX_MOD_DISP_OVL1 },
	{ { MTK_DISP_PWM, 0 }, MT8173_MUTEX_MOD_DISP_PWM0 },
	{ { MTK_DISP_PWM, 1 }, MT8173_MUTEX_MOD_DISP_PWM1 },
	{ { MTK_DISP_RDMA, 0 }, MT8173_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT8173_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_RDMA, 2 }, MT8173_MUTEX_MOD_DISP_RDMA2 },
	{ { MTK_DISP_UFOE, 0 }, MT8173_MUTEX_MOD_DISP_UFOE },
	{ { MTK_DISP_WDMA, 0 }, MT8173_MUTEX_MOD_DISP_WDMA0 },
	{ { MTK_DISP_WDMA, 1 }, MT8173_MUTEX_MOD_DISP_WDMA1 },
};

const struct mtk_drm_legacy_mtx_data mt8173_legacy_mtx_data = {
	.pairs = mt8173_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8173_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8183_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8183_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_CCORR, 0 }, MT8183_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_COLOR, 0 }, MT8183_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_DITHER, 0 }, MT8183_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8183_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_OVL, 0 }, MT8183_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL_2L, 0 }, MT8183_MUTEX_MOD_DISP_OVL0_2L },
	{ { MTK_DISP_OVL_2L, 1 }, MT8183_MUTEX_MOD_DISP_OVL1_2L },
	{ { MTK_DISP_RDMA, 0 }, MT8183_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT8183_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_WDMA, 0 }, MT8183_MUTEX_MOD_DISP_WDMA0 },
};

const struct mtk_drm_legacy_mtx_data mt8183_legacy_mtx_data = {
	.pairs = mt8183_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8183_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8186_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8186_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_CCORR, 0 }, MT8186_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_COLOR, 0 }, MT8186_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_DITHER, 0 }, MT8186_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8186_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_OVL, 0 }, MT8186_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL_2L, 0 }, MT8186_MUTEX_MOD_DISP_OVL0_2L },
	{ { MTK_DISP_POSTMASK, 0 }, MT8186_MUTEX_MOD_DISP_POSTMASK0 },
	{ { MTK_DISP_RDMA, 0 }, MT8186_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT8186_MUTEX_MOD_DISP_RDMA1 },
};

const struct mtk_drm_legacy_mtx_data mt8186_legacy_mtx_data = {
	.pairs = mt8186_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8186_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8188_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_OVL, 0 }, MT8188_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_WDMA, 0 }, MT8188_MUTEX_MOD_DISP_WDMA0 },
	{ { MTK_DISP_RDMA, 0 }, MT8188_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_COLOR, 0 }, MT8188_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_CCORR, 0 }, MT8188_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_AAL, 0 }, MT8188_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8188_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_POSTMASK, 0 }, MT8188_MUTEX_MOD_DISP_POSTMASK0 },
	{ { MTK_DISP_DITHER, 0 }, MT8188_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_MERGE, 0 }, MT8188_MUTEX_MOD_DISP_VPP_MERGE },
	{ { MTK_DISP_DSC, 0 }, MT8188_MUTEX_MOD_DISP_DSC_WRAP0_CORE0 },
	{ { MTK_DISP_DSI, 0 }, MT8188_MUTEX_MOD_DISP_DSI0 },
	{ { MTK_DISP_PWM, 0 }, MT8188_MUTEX_MOD2_DISP_PWM0 },
	{ { MTK_DISP_DP_INTF, 0 }, MT8188_MUTEX_MOD_DISP_DP_INTF0 },
	{ { MTK_DISP_DP_INTF, 1 }, MT8188_MUTEX_MOD_DISP1_DP_INTF1 },
	{ { MTK_DISP_DPI, 1 }, MT8188_MUTEX_MOD_DISP1_DPI1 },
	{ { MTK_DISP_ETHDR_MIXER, 0 }, MT8188_MUTEX_MOD_DISP1_DISP_MIXER },
	{ { MTK_DISP_MDP_RDMA, 0 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA0 },
	{ { MTK_DISP_MDP_RDMA, 1 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA1 },
	{ { MTK_DISP_MDP_RDMA, 2 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA2 },
	{ { MTK_DISP_MDP_RDMA, 3 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA3 },
	{ { MTK_DISP_MDP_RDMA, 4 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA4 },
	{ { MTK_DISP_MDP_RDMA, 5 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA5 },
	{ { MTK_DISP_MDP_RDMA, 6 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA6 },
	{ { MTK_DISP_MDP_RDMA, 7 }, MT8188_MUTEX_MOD_DISP1_MDP_RDMA7 },
	{ { MTK_DISP_PADDING, 0 }, MT8188_MUTEX_MOD_DISP1_PADDING0 },
	{ { MTK_DISP_PADDING, 1 }, MT8188_MUTEX_MOD_DISP1_PADDING1 },
	{ { MTK_DISP_PADDING, 2 }, MT8188_MUTEX_MOD_DISP1_PADDING2 },
	{ { MTK_DISP_PADDING, 3 }, MT8188_MUTEX_MOD_DISP1_PADDING3 },
	{ { MTK_DISP_PADDING, 4 }, MT8188_MUTEX_MOD_DISP1_PADDING4 },
	{ { MTK_DISP_PADDING, 5 }, MT8188_MUTEX_MOD_DISP1_PADDING5 },
	{ { MTK_DISP_PADDING, 6 }, MT8188_MUTEX_MOD_DISP1_PADDING6 },
	{ { MTK_DISP_PADDING, 7 }, MT8188_MUTEX_MOD_DISP1_PADDING7 },
	{ { MTK_DISP_MERGE, 1 }, MT8188_MUTEX_MOD_DISP1_VPP_MERGE0 },
	{ { MTK_DISP_MERGE, 2 }, MT8188_MUTEX_MOD_DISP1_VPP_MERGE1 },
	{ { MTK_DISP_MERGE, 3 }, MT8188_MUTEX_MOD_DISP1_VPP_MERGE2 },
	{ { MTK_DISP_MERGE, 4 }, MT8188_MUTEX_MOD_DISP1_VPP_MERGE3 },
	{ { MTK_DISP_MERGE, 5 }, MT8188_MUTEX_MOD_DISP1_VPP_MERGE4 },
};

const struct mtk_drm_legacy_mtx_data mt8188_legacy_mtx_data = {
	.pairs = mt8188_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8188_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8192_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8192_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_CCORR, 0 }, MT8192_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_COLOR, 0 }, MT8192_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_DITHER, 0 }, MT8192_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8192_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_POSTMASK, 0 }, MT8192_MUTEX_MOD_DISP_POSTMASK0 },
	{ { MTK_DISP_OVL, 0 }, MT8192_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL_2L, 0 }, MT8192_MUTEX_MOD_DISP_OVL0_2L },
	{ { MTK_DISP_OVL_2L, 2 }, MT8192_MUTEX_MOD_DISP_OVL2_2L },
	{ { MTK_DISP_RDMA, 0 }, MT8192_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 4 }, MT8192_MUTEX_MOD_DISP_RDMA4 },
};

const struct mtk_drm_legacy_mtx_data mt8192_legacy_mtx_data = {
	.pairs = mt8192_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8192_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8195_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_OVL, 0 }, MT8195_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_WDMA, 0 }, MT8195_MUTEX_MOD_DISP_WDMA0 },
	{ { MTK_DISP_RDMA, 0 }, MT8195_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_COLOR, 0 }, MT8195_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_CCORR, 0 }, MT8195_MUTEX_MOD_DISP_CCORR0 },
	{ { MTK_DISP_AAL, 0 }, MT8195_MUTEX_MOD_DISP_AAL0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8195_MUTEX_MOD_DISP_GAMMA0 },
	{ { MTK_DISP_DITHER, 0 }, MT8195_MUTEX_MOD_DISP_DITHER0 },
	{ { MTK_DISP_MERGE, 0 }, MT8195_MUTEX_MOD_DISP_VPP_MERGE },
	{ { MTK_DISP_DSC, 0 }, MT8195_MUTEX_MOD_DISP_DSC_WRAP0_CORE0 },
	{ { MTK_DISP_DSI, 0 }, MT8195_MUTEX_MOD_DISP_DSI0 },
	{ { MTK_DISP_PWM, 0 }, MT8195_MUTEX_MOD_DISP_PWM0 },
	{ { MTK_DISP_DP_INTF, 0 }, MT8195_MUTEX_MOD_DISP_DP_INTF0 },
	{ { MTK_DISP_MDP_RDMA, 0 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA0 },
	{ { MTK_DISP_MDP_RDMA, 1 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA1 },
	{ { MTK_DISP_MDP_RDMA, 2 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA2 },
	{ { MTK_DISP_MDP_RDMA, 3 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA3 },
	{ { MTK_DISP_MDP_RDMA, 4 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA4 },
	{ { MTK_DISP_MDP_RDMA, 5 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA5 },
	{ { MTK_DISP_MDP_RDMA, 6 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA6 },
	{ { MTK_DISP_MDP_RDMA, 7 }, MT8195_MUTEX_MOD_DISP1_MDP_RDMA7 },
	{ { MTK_DISP_MERGE, 1 }, MT8195_MUTEX_MOD_DISP1_VPP_MERGE0 },
	{ { MTK_DISP_MERGE, 2 }, MT8195_MUTEX_MOD_DISP1_VPP_MERGE1 },
	{ { MTK_DISP_MERGE, 3 }, MT8195_MUTEX_MOD_DISP1_VPP_MERGE2 },
	{ { MTK_DISP_MERGE, 4 }, MT8195_MUTEX_MOD_DISP1_VPP_MERGE3 },
	{ { MTK_DISP_ETHDR_MIXER, 0 }, MT8195_MUTEX_MOD_DISP1_DISP_MIXER },
	{ { MTK_DISP_MERGE, 5 }, MT8195_MUTEX_MOD_DISP1_VPP_MERGE4 },
	{ { MTK_DISP_DP_INTF, 1 }, MT8195_MUTEX_MOD_DISP1_DP_INTF0 },
};

const struct mtk_drm_legacy_mtx_data mt8195_legacy_mtx_data = {
	.pairs = mt8195_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8195_legacy_mtx_trig_ids)
};

static const struct mtk_drm_legacy_mtx_pairs mt8365_legacy_mtx_trig_ids[] = {
	{ { MTK_DISP_AAL, 0 }, MT8365_MUTEX_MOD_DISP_AAL },
	{ { MTK_DISP_CCORR, 0 }, MT8365_MUTEX_MOD_DISP_CCORR },
	{ { MTK_DISP_COLOR, 0 }, MT8365_MUTEX_MOD_DISP_COLOR0 },
	{ { MTK_DISP_DITHER, 0 }, MT8365_MUTEX_MOD_DISP_DITHER },
	{ { MTK_DISP_DPI, 0 }, MT8365_MUTEX_MOD_DISP_DPI0 },
	{ { MTK_DISP_DSI, 0 }, MT8365_MUTEX_MOD_DISP_DSI0 },
	{ { MTK_DISP_GAMMA, 0 }, MT8365_MUTEX_MOD_DISP_GAMMA },
	{ { MTK_DISP_OVL, 0 }, MT8365_MUTEX_MOD_DISP_OVL0 },
	{ { MTK_DISP_OVL_2L, 0 }, MT8365_MUTEX_MOD_DISP_OVL0_2L },
	{ { MTK_DISP_PWM, 0 }, MT8365_MUTEX_MOD_DISP_PWM0 },
	{ { MTK_DISP_RDMA, 0 }, MT8365_MUTEX_MOD_DISP_RDMA0 },
	{ { MTK_DISP_RDMA, 1 }, MT8365_MUTEX_MOD_DISP_RDMA1 },
	{ { MTK_DISP_WDMA, 0 }, MT8365_MUTEX_MOD_DISP_WDMA0 },
};

const struct mtk_drm_legacy_mtx_data mt8365_legacy_mtx_data = {
	.pairs = mt8365_legacy_mtx_trig_ids,
	.num_pairs = ARRAY_SIZE(mt8365_legacy_mtx_trig_ids)
};

static const struct of_device_id mtk_drm_legacy_mtk_mutex_match[] = {
	{ .compatible = "mediatek,mt2701-disp-mutex", .data = &mt2701_legacy_mtx_data },
	{ .compatible = "mediatek,mt2712-disp-mutex", .data = &mt2712_legacy_mtx_data },
	{ .compatible = "mediatek,mt6795-disp-mutex", .data = &mt8173_legacy_mtx_data },
	{ .compatible = "mediatek.mt6893-disp-mutex", .data = &mt6893_legacy_mtx_data },
	{ .compatible = "mediatek,mt8167-disp-mutex", .data = &mt8167_legacy_mtx_data },
	{ .compatible = "mediatek,mt8173-disp-mutex", .data = &mt8173_legacy_mtx_data },
	{ .compatible = "mediatek,mt8183-disp-mutex", .data = &mt8183_legacy_mtx_data },
	{ .compatible = "mediatek,mt8186-disp-mutex", .data = &mt8186_legacy_mtx_data },
	{ .compatible = "mediatek,mt8188-disp-mutex", .data = &mt8188_legacy_mtx_data },
	{ .compatible = "mediatek,mt8192-disp-mutex", .data = &mt8192_legacy_mtx_data },
	{ .compatible = "mediatek,mt8195-disp-mutex", .data = &mt8195_legacy_mtx_data },
	{ .compatible = "mediatek,mt8365-disp-mutex", .data = &mt8365_legacy_mtx_data },
	{ /* sentinel */ },
};

int mtk_drm_legacy_inject_mutex_trig_ids(struct mtk_drm_comp_list *hlist,
					 struct device_node *mutex_node)
{
	struct mtk_drm_legacy_mtx_data *data;
	const struct of_device_id *of_id;
	struct mtk_ddp_comp *ddp_comp;
	int i;

	of_id = of_match_node(mtk_drm_legacy_mtk_mutex_match, mutex_node);
	if (!of_id)
		return -ENODEV;

	data = (struct mtk_drm_legacy_mtx_data *)of_id->data;

	for (i = 0; i < data->num_pairs; i++) {
		const struct mtk_drm_comp_definition *comp = &data->pairs[i].comp;

		hash_for_each_possible(hlist->ddp_list, ddp_comp, lnode, comp->type)
			if (ddp_comp->inst_id == comp->inst_id)
				ddp_comp->mtx_trig_id = data->pairs[i].mtx_trig_id;
	}

	return 0;
}

u8 mtk_drm_legacy_get_ovl_adaptor_mutex_trig_id(enum mtk_ddp_comp_type ddp_type,
						u8 ddp_inst_id,
						struct device_node *mutex_node)
{
	struct mtk_drm_legacy_mtx_data *data;
	const struct of_device_id *of_id;
	int i;

	of_id = of_match_node(mtk_drm_legacy_mtk_mutex_match, mutex_node);
	if (!of_id)
		return 0;

	data = (struct mtk_drm_legacy_mtx_data *)of_id->data;

	for (i = 0; i < data->num_pairs; i++) {
		const struct mtk_drm_comp_definition *comp = &data->pairs[i].comp;

		if (ddp_type != comp->type || ddp_inst_id != comp->inst_id)
			continue;

		return data->pairs[i].mtx_trig_id;
	}

	return 0;
}

void mtk_drm_legacy_ovl_adaptor_probe(struct device *dev, struct mtk_drm_private *priv,
				      struct component_match **match)
{
	struct platform_device *ovl_adaptor;

	ovl_adaptor = platform_device_register_data(dev, "mediatek-disp-ovl-adaptor",
						    PLATFORM_DEVID_AUTO,
						    (void *)priv, sizeof(*priv));

	mtk_ddp_comp_init(&ovl_adaptor->dev, NULL, &priv->hlist, priv->data->mmsys_id,
			  MTK_DISP_OVL_ADAPTOR, 0);
	component_match_add(dev, match, component_compare_dev, &ovl_adaptor->dev);
}
