// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Qiqi Wang <qiqi.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mediatek,mt8189-clk.h>

static const struct mtk_gate_regs gce_d_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_GCE_D(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &gce_d_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate gce_d_clks[] = {
	GATE_GCE_D(CLK_GCE_D_TOP, "gce_d_top", "mminfra_gce_d", 16),
};

static const struct mtk_clk_desc gce_d_mcd = {
	.clks = gce_d_clks,
	.num_clks = ARRAY_SIZE(gce_d_clks),
};

static const struct mtk_gate_regs gce_m_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_GCE_M(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &gce_m_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate gce_m_clks[] = {
	GATE_GCE_M(CLK_GCE_M_TOP, "gce_m_top", "mminfra_gce_m", 16),
};

static const struct mtk_clk_desc gce_m_mcd = {
	.clks = gce_m_clks,
	.num_clks = ARRAY_SIZE(gce_m_clks),
};

static const struct mtk_gate_regs mminfra_config0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs mminfra_config1_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

#define GATE_MMINFRA_CONFIG0(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mminfra_config0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_MMINFRA_CONFIG1(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &mminfra_config1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mminfra_config_clks[] = {
	/* MMINFRA_CONFIG0 */
	GATE_MMINFRA_CONFIG0(CLK_MMINFRA_GCE_D, "mminfra_gce_d", "mminfra_gce_26m", 0),
	GATE_MMINFRA_CONFIG0(CLK_MMINFRA_GCE_M, "mminfra_gce_m", "mminfra_gce_26m", 1),
	GATE_MMINFRA_CONFIG0(CLK_MMINFRA_SMI, "mminfra_smi", "mminfra_sel", 2),
	/* MMINFRA_CONFIG1 */
	GATE_MMINFRA_CONFIG1(CLK_MMINFRA_GCE_26M, "mminfra_gce_26m", "mminfra_sel", 17),
};

static const struct mtk_clk_desc mminfra_config_mcd = {
	.clks = mminfra_config_clks,
	.num_clks = ARRAY_SIZE(mminfra_config_clks),
};

static const struct of_device_id of_match_clk_mt8189_mmsys[] = {
	{ .compatible = "mediatek,mt8189-gce-d", .data = &gce_d_mcd },
	{ .compatible = "mediatek,mt8189-gce-m", .data = &gce_m_mcd },
	{ .compatible = "mediatek,mt8189-mm-infra", .data = &mminfra_config_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8189_mmsys);

static struct platform_driver clk_mt8189_mmsys_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8189-mmsys",
		.of_match_table = of_match_clk_mt8189_mmsys,
	},
};
module_platform_driver(clk_mt8189_mmsys_drv);

MODULE_DESCRIPTION("MediaTek MT8189 Multimedia Subsystem clocks driver");
MODULE_LICENSE("GPL");
