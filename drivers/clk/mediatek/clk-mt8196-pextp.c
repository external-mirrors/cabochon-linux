// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>
#include <dt-bindings/reset/mediatek,mt8196-resets.h>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"
#include "reset.h"

#define MT8196_PEXTP_RST0_SET_OFFSET	0x8
#define MT8196_REG_PEXTP_CON		0x20
#define MT8196_REG_PEXTP_CLKREQ_CTRL	0x7c

static const struct mtk_gate_regs pext_cg_regs = {
	.set_ofs = 0x18,
	.clr_ofs = 0x1c,
	.sta_ofs = 0x14,
};

static const char * const pext_lp_parents[] = {
	"clk26m",
	"clk32k"
};

#define GATE_PEXT_SR(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &pext_cg_regs, _shift,	\
		       &mtk_clk_gate_ops_setclr, CLK_SET_RATE_PARENT)

#define GATE_PEXT(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &pext_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define MUX_GATE_PEXT(_id, _name, _parents, _mux_reg, _shift, _width,	\
		      _gate_reg, _gate)					\
	{								\
		.id = _id,						\
		.name = _name,						\
		.mux_reg = _mux_reg,					\
		.mux_shift = _shift,					\
		.mux_width = _width,					\
		.gate_reg = _gate_reg,					\
		.gate_shift = _gate,					\
		.divider_shift = -1,					\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
	}

static const struct mtk_gate pext_clks[] = {
	GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0_TL, "pext_pm0_tl", "tl", 0),
	GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0_REF, "pext_pm0_ref", "clk26m", 1),
	GATE_PEXT(CLK_PEXT_PEXTP_PHY_P0_MCU_BUS, "pext_pp0_mcu_bus", "clk26m", 6),
	GATE_PEXT(CLK_PEXT_PEXTP_PHY_P0_PEXTP_REF, "pext_pp0_pextp_ref", "clk26m", 7),
	GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0_AXI_250, "pext_pm0_axi_250", "ufs_pexpt0_mem_sub", 12),
	GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0_AHB_APB, "pext_pm0_ahb_apb", "ufs_pextp0_axi", 13),
	GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0_PL_P, "pext_pm0_pl_p", "clk26m", 14),
	GATE_PEXT_SR(CLK_PEXT_PEXTP_VLP_AO_P0_LP, "pext_pextp_vlp_ao_p0_lp", "pext_p0p1_lp", 19),
};

static const struct mtk_composite pext_composites[] = {
	MUX_GATE_PEXT(CLK_PEXT_PEXTP_MAC_P0P1_LOWPOWER, "pext_p0p1_lp", pext_lp_parents,
		      MT8196_REG_PEXTP_CON, 0, 1, MT8196_REG_PEXTP_CLKREQ_CTRL, 0),
	MUX_GATE_PEXT(CLK_PEXT_PEXTP_MAC_P2_LOWPOWER, "pext_p2_lp", pext_lp_parents,
		      MT8196_REG_PEXTP_CON, 3, 4, 0, -1)
};

static u16 pext_rst_ofs[] = { MT8196_PEXTP_RST0_SET_OFFSET };

static u16 pext_rst_idx_map[] = {
	[MT8196_PEXTP0_RST0_PCIE0_MAC] = 0,
	[MT8196_PEXTP0_RST0_PCIE0_PHY] = 1,
};

static const struct mtk_clk_rst_desc pext_rst_desc = {
	.version = MTK_RST_SET_CLR,
	.rst_bank_ofs = pext_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(pext_rst_ofs),
	.rst_idx_map = pext_rst_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(pext_rst_idx_map),
};

static const struct mtk_clk_desc pext_mcd = {
	.clks = pext_clks,
	.num_clks = ARRAY_SIZE(pext_clks),
	.composite_clks = pext_composites,
	.num_composite_clks = ARRAY_SIZE(pext_composites),
	.rst_desc = &pext_rst_desc,
};

static const struct mtk_gate pext1_clks[] = {
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P1_TL, "pext1_pm1_tl", "tl_p1", 0),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P1_REF, "pext1_pm1_ref", "clk26m", 1),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P2_TL, "pext1_pm2_tl", "tl_p2", 2),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P2_REF, "pext1_pm2_ref", "clk26m", 3),
	GATE_PEXT(CLK_PEXT1_PEXTP_PHY_P1_MCU_BUS, "pext1_pp1_mcu_bus", "clk26m", 8),
	GATE_PEXT(CLK_PEXT1_PEXTP_PHY_P1_PEXTP_REF, "pext1_pp1_pextp_ref", "clk26m", 9),
	GATE_PEXT(CLK_PEXT1_PEXTP_PHY_P2_MCU_BUS, "pext1_pp2_mcu_bus", "clk26m", 10),
	GATE_PEXT(CLK_PEXT1_PEXTP_PHY_P2_PEXTP_REF, "pext1_pp2_pextp_ref", "clk26m", 11),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P1_AXI_250, "pext1_pm1_axi_250",
		   "pextp1_usb_axi", 16),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P1_AHB_APB, "pext1_pm1_ahb_apb",
		   "pextp1_usb_mem_sub", 17),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P1_PL_P, "pext1_pm1_pl_p", "clk26m", 18),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P2_AXI_250, "pext1_pm2_axi_250",
		   "pextp1_usb_axi", 19),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P2_AHB_APB, "pext1_pm2_ahb_apb",
		   "pextp1_usb_mem_sub", 20),
	GATE_PEXT(CLK_PEXT1_PEXTP_MAC_P2_PL_P, "pext1_pm2_pl_p", "clk26m", 21),
	GATE_PEXT_SR(CLK_PEXT1_PEXTP_VLP_AO_P1_LP, "pext1_pextp_vlp_ao_p1_lp", "pext_p0p1_lp", 26),
	GATE_PEXT_SR(CLK_PEXT1_PEXTP_VLP_AO_P2_LP, "pext1_pextp_vlp_ao_p2_lp", "pext_p2_lp", 27),
};

static u16 pext1_rst_idx_map[] = {
	[MT8196_PEXTP1_RST0_PCIE1_MAC] = 0,
	[MT8196_PEXTP1_RST0_PCIE1_PHY] = 1,
	[MT8196_PEXTP1_RST0_PCIE2_MAC] = 8,
	[MT8196_PEXTP1_RST0_PCIE2_PHY] = 9,
};

static const struct mtk_clk_rst_desc pext1_rst_desc = {
	.version = MTK_RST_SET_CLR,
	.rst_bank_ofs = pext_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(pext_rst_ofs),
	.rst_idx_map = pext1_rst_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(pext1_rst_idx_map),
};

static const struct mtk_clk_desc pext1_mcd = {
	.clks = pext1_clks,
	.num_clks = ARRAY_SIZE(pext1_clks),
	.rst_desc = &pext1_rst_desc,
};

static const struct of_device_id of_match_clk_mt8196_pextp[] = {
	{ .compatible = "mediatek,mt8196-pextp0cfg-ao", .data = &pext_mcd },
	{ .compatible = "mediatek,mt8196-pextp1cfg-ao", .data = &pext1_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_pextp);

static struct platform_driver clk_mt8196_pextp_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-pextp",
		.of_match_table = of_match_clk_mt8196_pextp,
	},
};

module_platform_driver(clk_mt8196_pextp_drv);
MODULE_DESCRIPTION("MediaTek MT8196 PCIe transmit phy clocks driver");
MODULE_LICENSE("GPL");
