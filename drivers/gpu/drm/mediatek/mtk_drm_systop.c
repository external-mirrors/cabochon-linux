// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek Display/Overlay Controller System - Top configuration driver
 *
 * Copyright (c) 2025 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

/* GCE_EVENT_SEL */
#define SYS_CFG_GCE_EVENT_GCE_M_EN		BIT(0)
#define SYS_CFG_GCE_EVENT_GCE_D_EN		BIT(1)

/* GCE_FRAME_DONE_SEL */
#define GCE_FRAME_DONE_SEL_DVO			25
#define GCE_FRAME_DONE_SEL_DSI1			29
#define GCE_FRAME_DONE_SEL_DSI0			31
#define GCE_FRAME_DONE_SEL_DP_INTF0		41
#define DISP_SYS_GCE_EVENT_SEL			0x408

/* CB_CON and BYPASS_MUX_SHADOW */
#define TOP_BYPASS_MUX_SHADOW			BIT(0)
#define DISP_CROSSBAR_BYPASS_MUX_SHADOW		GENMASK(23, 16)
#define OVL_CROSSBAR_BYPASS_MUX_SHADOW		GENMASK(24, 17)

/**
 * enum mtk_drm_sys_top_regidx - Register index for DispSYS/OVLSYS Top
 * @SYS_CFG_INT_MERGE:            Interrupt merge control
 * @SYS_CFG_GCE_EVENT_SEL:        Global Command Engine Event Dispatcher config
 * @SYS_CFG_GCE_FRAME_DONE_SEL:   Global Command Engine Frame Done Event selector
 * @SYS_CFG_BYPASS_MUX_SHADOW:    Global MUX Shadowing bypass selector
 * @SYS_CFG_CB_CON:               Crossbar TOP Configuration register
 * @SYS_CFG_REG_MAX:              Number of registers in index
 */
enum mtk_drm_sys_top_regidx {
	SYS_CFG_INT_MERGE,
	SYS_CFG_GCE_EVENT_SEL,
	SYS_CFG_GCE_FRAME_DONE_SEL,
	SYS_CFG_BYPASS_MUX_SHADOW,
	SYS_CFG_CB_CON,
	SYS_CFG_REG_MAX
};

static const u16 mtk_drm_ovlsys_top_regs_mt8196[SYS_CFG_REG_MAX] = {
	[SYS_CFG_INT_MERGE] = 0x8,
	[SYS_CFG_GCE_EVENT_SEL] = 0x408,
	[SYS_CFG_GCE_FRAME_DONE_SEL] = 0x94,
	[SYS_CFG_BYPASS_MUX_SHADOW] = 0xca0,
	[SYS_CFG_CB_CON] = 0xcac,
};

static const u16 mtk_drm_dispsys0_top_regs_mt8196[SYS_CFG_REG_MAX] = {
	[SYS_CFG_GCE_EVENT_SEL] = 0x408,
	[SYS_CFG_GCE_FRAME_DONE_SEL] = 0x410,
	[SYS_CFG_BYPASS_MUX_SHADOW] = 0xc30,
};

static const u16 mtk_drm_dispsys1_top_regs_mt8196[SYS_CFG_REG_MAX] = {
	[SYS_CFG_INT_MERGE] = 0x8,
	[SYS_CFG_GCE_EVENT_SEL] = 0x408,
	[SYS_CFG_GCE_FRAME_DONE_SEL] = 0x94,
	[SYS_CFG_BYPASS_MUX_SHADOW] = 0xcf8,
};

struct mtk_drm_sys_top_pdata {
	const u16 *regs;
	const u8 *frame_done_sel;
	u8 num_frame_done_sel;
};

struct mtk_drm_sys_top {
	const struct mtk_drm_sys_top_pdata *pdata;
	void __iomem *base;
	struct clk_bulk_data *clks;
	unsigned short num_clks;
	unsigned short enable_count;
};

static const struct of_device_id mtk_drm_sys_top_of_ids[];

static void mtk_sys_top_update_bits(struct mtk_drm_sys_top *priv, u16 offset,
				    u32 mask, u32 val)
{
	u32 tmp = readl(priv->base + offset);

	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, priv->base + offset);
}

static void mtk_sys_top_config(struct device *dev)
{
	struct mtk_drm_sys_top *priv = dev_get_drvdata(dev);
	const struct mtk_drm_sys_top_pdata *pdata = priv->pdata;
	const u16 *regs = pdata->regs;

	/* Disable interrupt merging for all sources */
	if (regs[SYS_CFG_INT_MERGE] > 0)
		writel(0, priv->base + regs[SYS_CFG_INT_MERGE]);

	/* Enable dispatching events to both GCE-M and GCE-D mailboxes */
	mtk_sys_top_update_bits(priv, regs[SYS_CFG_GCE_EVENT_SEL],
				SYS_CFG_GCE_EVENT_GCE_M_EN | SYS_CFG_GCE_EVENT_GCE_D_EN,
				SYS_CFG_GCE_EVENT_GCE_M_EN | SYS_CFG_GCE_EVENT_GCE_D_EN);
	/*
	 * Set path selection double register buffering off.
	 *
	 * OVL and DISP subsystems are slightly different in this regard and
	 * the first has one more register (CB_CON) which DISP does not have
	 * at least yet, but it may appear in later display controllers.
	 *
	 * Distinguishing between register shadow disablement strategies by
	 * checking if this is OVLSYS or DISPSYS would work, but allow more
	 * flexibility by checking if the CB_CON register exists and decide
	 * the strategy based on that.
	 */
	if (regs[SYS_CFG_CB_CON] && regs[SYS_CFG_BYPASS_MUX_SHADOW]) {
		/* Top bypass in the mux shadow register, crossbar bypass in cb_con */
		mtk_sys_top_update_bits(priv, regs[SYS_CFG_BYPASS_MUX_SHADOW],
					TOP_BYPASS_MUX_SHADOW, TOP_BYPASS_MUX_SHADOW);

		mtk_sys_top_update_bits(priv, regs[SYS_CFG_CB_CON],
					OVL_CROSSBAR_BYPASS_MUX_SHADOW,
					OVL_CROSSBAR_BYPASS_MUX_SHADOW);
	} else if (regs[SYS_CFG_BYPASS_MUX_SHADOW]) {
		/* Top and Crossbar bypass all in a single register */
		mtk_sys_top_update_bits(priv, regs[SYS_CFG_BYPASS_MUX_SHADOW],
					TOP_BYPASS_MUX_SHADOW | DISP_CROSSBAR_BYPASS_MUX_SHADOW,
					TOP_BYPASS_MUX_SHADOW | DISP_CROSSBAR_BYPASS_MUX_SHADOW);
	}

	if (pdata->frame_done_sel) {
		for (int i = 0; i < pdata->num_frame_done_sel; i++) {
			u16 reg_off = regs[SYS_CFG_GCE_FRAME_DONE_SEL] + (i * 4);

			writel(pdata->frame_done_sel[i], priv->base + reg_off);
		}
	}
}

int mtk_sys_top_clk_enable(struct mtk_ddp_comp *comp)
{
	struct mtk_drm_sys_top *priv = dev_get_drvdata(comp->dev);

	return clk_bulk_prepare_enable(priv->num_clks, priv->clks);
}

void mtk_sys_top_clk_disable(struct mtk_ddp_comp *comp)
{
	struct mtk_drm_sys_top *priv = dev_get_drvdata(comp->dev);

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
}

void mtk_drm_sys_top_start(struct device *dev)
{
	struct mtk_drm_sys_top *priv = dev_get_drvdata(dev);

	/* Configure the TOP registers at startup time */
	mtk_sys_top_config(dev);

	priv->enable_count++;
}

void mtk_drm_sys_top_stop(struct device *dev)
{
	struct mtk_drm_sys_top *priv = dev_get_drvdata(dev);

	if (!priv->enable_count) {
		dev_err(dev, "FIXME: requested to stop an already offline controller!\n");
		return;
	}

	/*
	 * Disabling clocks won't necessarily make this IP to lose register
	 * contents as that happens only when cutting power, not just clocks,
	 * to the entire controller TOP. Yet, this saves some power.
	 */
	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	priv->enable_count--;
}

static int mtk_drm_sys_top_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_drm_sys_top_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_drm_sys_top_component_ops = {
	.bind	= mtk_drm_sys_top_bind,
	.unbind = mtk_drm_sys_top_unbind,
};

static const u8 mtk_drm_dispsys1_frame_done_mt8196[] = {
	GCE_FRAME_DONE_SEL_DVO, GCE_FRAME_DONE_SEL_DP_INTF0,
	GCE_FRAME_DONE_SEL_DSI0, GCE_FRAME_DONE_SEL_DSI1
};

static const struct mtk_drm_sys_top_pdata mtk_drm_sys_top_disp0_mt8196 = {
	.regs = mtk_drm_dispsys0_top_regs_mt8196,
};

static const struct mtk_drm_sys_top_pdata mtk_drm_sys_top_disp1_mt8196 = {
	.regs = mtk_drm_dispsys1_top_regs_mt8196,
	.frame_done_sel = mtk_drm_dispsys1_frame_done_mt8196,
	.num_frame_done_sel = ARRAY_SIZE(mtk_drm_dispsys1_frame_done_mt8196),
};

static const struct mtk_drm_sys_top_pdata mtk_drm_sys_top_ovl_mt8196 = {
	.regs = mtk_drm_ovlsys_top_regs_mt8196,
};

static const struct of_device_id mtk_drm_sys_top_of_ids[] = {
	{ .compatible = "mediatek,mt8196-dispsys0", .data = &mtk_drm_sys_top_disp0_mt8196 },
	{ .compatible = "mediatek,mt8196-dispsys1", .data = &mtk_drm_sys_top_disp1_mt8196 },
	{ .compatible = "mediatek,mt8196-ovlsys0", .data = &mtk_drm_sys_top_ovl_mt8196 },
	{ .compatible = "mediatek,mt8196-ovlsys1", .data = &mtk_drm_sys_top_ovl_mt8196 },
	{ /* sentinel */ }
};

static int mtk_drm_sys_top_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct device *mmsys_dev;
	struct mtk_drm_sys_top *priv;
	int ret;

	mmsys_dev = dev->parent->parent;
	of_id = of_match_node(mtk_drm_sys_top_of_ids, mmsys_dev->of_node);
	if (!of_id)
		return -ENOENT;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = of_iomap(mmsys_dev->of_node, 0);
	if (IS_ERR(priv->base))
		return dev_err_probe(dev, PTR_ERR(priv->base), "Cannot get reg\n");

	ret = devm_clk_bulk_get_all(mmsys_dev, &priv->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot get clocks\n");

	priv->num_clks = ret;
	priv->pdata = of_id->data;
	platform_set_drvdata(pdev, priv);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_drm_sys_top_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	return 0;
}

struct platform_driver mtk_drm_sys_top_driver = {
	.probe		= mtk_drm_sys_top_probe,
	.driver		= {
		.name	= "mediatek-drm-sys-top",
		.owner	= THIS_MODULE,
	},
};

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek Display/Overlay System Configuration Driver");
MODULE_LICENSE("GPL");
