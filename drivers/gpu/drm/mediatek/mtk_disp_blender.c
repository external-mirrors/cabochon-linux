// SPDX-License-Identifier: GPL-2.0-only
/*
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

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_disp_ovl.h"
#include "mtk_drm_drv.h"

#define DISP_REG_OVL_BLD_DATAPATH_CON		0x010
#  define OVL_BLD_BGCLR_IN_SEL			BIT(0)
#  define OVL_BLD_BGCLR_OUT_TO_PROC		BIT(4)
#  define OVL_BLD_BGCLR_OUT_TO_NEXT_LAYER	BIT(5)

#define DISP_REG_OVL_BLD_EN			0x020
#  define OVL_BLD_EN				BIT(0)
#  define OVL_BLD_FORCE_RELAY_MODE		BIT(4)
#  define OVL_BLD_RELAY_MODE			BIT(5)

#define DISP_REG_OVL_BLD_RST			0x024
#  define OVL_BLD_RST				BIT(0)

#define DISP_REG_OVL_BLD_SHADOW_CTRL		0x028
#  define OVL_BLD_BYPASS_SHADOW			BIT(2)

#define DISP_REG_OVL_BLD_ROI_SIZE		0x030
#define DISP_REG_OVL_BLD_L_EN			0x040
#  define OVL_BLD_L_EN				BIT(0)

#define DISP_REG_OVL_BLD_OFFSET			0x044
#define DISP_REG_OVL_BLD_SRC_SIZE		0x048
#define DISP_REG_OVL_BLD_L0_CLRFMT		0x050
#  define OVL_BLD_CON_FLD_CLRFMT		GENMASK(3, 0)
#  define OVL_BLD_CON_CLRFMT_MAN		BIT(4)
#  define OVL_BLD_CON_FLD_CLRFMT_NB		GENMASK(9, 8)
#  define OVL_BLD_CON_CLRFMT_NB_10_BIT		BIT(8)
#  define OVL_BLD_CON_BYTE_SWAP			BIT(16)
#  define OVL_BLD_CON_RGB_SWAP			BIT(17)

#define DISP_REG_OVL_BLD_BGCLR_CLR		0x104
#define DISP_REG_OVL_BLD_L_CON2			0x200
#  define OVL_BLD_L_ALPHA			GENMASK(7, 0)
#  define OVL_BLD_L_ALPHA_EN			BIT(12)

#define DISP_REG_OVL_BLD_L0_ALPHA_SEL		0x208
#  define OVL_BLD_L0_CONST			BIT(24)

#define DISP_REG_OVL_BLD_L0_CLR			0x20c
#  define OVL_BLD_BGCLR_BLACK			0xff000000
#  define OVL_BLD_BGCLR_RED			0xffff0000

#define BLENDER_MAX_SIZE 8191

struct mtk_disp_blender {
	void __iomem *regs;
	struct clk *clk;
	struct cmdq_client_reg cmdq_reg;
};

static void mtk_blender_unset_input_bgclr(struct mtk_disp_blender *priv)
{
	u32 val;

	val = readl(priv->regs + DISP_REG_OVL_BLD_DATAPATH_CON);
	val &= ~OVL_BLD_BGCLR_IN_SEL;
	writel(val, priv->regs + DISP_REG_OVL_BLD_DATAPATH_CON);
}

void mtk_blender_layer_config(struct device *dev, unsigned int idx,
			      struct mtk_plane_state *state,
			      struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	u32 alpha, blend_mode, clrfmt, ignore_pixel_alpha;

	/*
	 * Deselect IN from OVL Background Color if this is the first Blender
	 * to implicitly select input from exDMA instead.
	 */
	if (idx == 0)
		mtk_blender_unset_input_bgclr(priv);

	if (!pending->enable || pending->height == 0 || pending->width == 0 ||
	    pending->x > BLENDER_MAX_SIZE || pending->y > BLENDER_MAX_SIZE) {
		mtk_ddp_write(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_BLD_L_EN);
		return;
	}

	mtk_ddp_write(cmdq_pkt,
		      ((pending->height & GENMASK(15, 0)) << 16) |
		       (pending->width & GENMASK(15, 0)),
		      &priv->cmdq_reg, priv->regs, DISP_REG_OVL_BLD_SRC_SIZE);
	mtk_ddp_write(cmdq_pkt,
		      ((pending->y & GENMASK(15, 0)) << 16) | (pending->x & GENMASK(15, 0)),
		      &priv->cmdq_reg, priv->regs, DISP_REG_OVL_BLD_OFFSET);

	blend_mode = mtk_ovl_get_blend_mode(state, MTK_OVL_SUPPORT_BLEND_MODES);
	clrfmt = mtk_ovl_fmt_convert(pending->format, blend_mode, true, false, 0,
				     OVL_BLD_CON_CLRFMT_MAN, OVL_BLD_CON_BYTE_SWAP,
				     OVL_BLD_CON_RGB_SWAP);
	clrfmt |= mtk_ovl_is_10bit_rgb(pending->format) ? OVL_BLD_CON_CLRFMT_NB_10_BIT : 0;
	mtk_ddp_write_mask(cmdq_pkt, clrfmt, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_BLD_L0_CLRFMT, OVL_BLD_CON_CLRFMT_MAN |
			   OVL_BLD_CON_RGB_SWAP |  OVL_BLD_CON_BYTE_SWAP |
			   OVL_BLD_CON_FLD_CLRFMT | OVL_BLD_CON_FLD_CLRFMT_NB);

	if (mtk_ovl_is_ignore_pixel_alpha(state, blend_mode))
		ignore_pixel_alpha = OVL_BLD_L0_CONST;
	else
		ignore_pixel_alpha = 0;

	mtk_ddp_write_mask(cmdq_pkt, ignore_pixel_alpha, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_BLD_L0_ALPHA_SEL, OVL_BLD_L0_CONST);

	alpha = FIELD_PREP(OVL_BLD_L_ALPHA, (state->base.alpha >> 8));
	alpha |= OVL_BLD_L_ALPHA_EN;
	mtk_ddp_write_mask(cmdq_pkt, alpha, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_BLD_L_CON2, OVL_BLD_L_ALPHA_EN | OVL_BLD_L_ALPHA);

	mtk_ddp_write(cmdq_pkt, OVL_BLD_L_EN, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_BLD_L_EN);
}

unsigned int mtk_blender_layerstage_nr(struct device *dev)
{
	return 1;
}

void mtk_blender_start(struct device *dev)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(dev);
	u32 val;

	/* Bypass shadow registers and enable */
	val = readl(priv->regs + DISP_REG_OVL_BLD_SHADOW_CTRL);
	val |= OVL_BLD_BYPASS_SHADOW;
	writel(val, priv->regs + DISP_REG_OVL_BLD_SHADOW_CTRL);

	val = readl(priv->regs + DISP_REG_OVL_BLD_EN);
	val |= OVL_BLD_EN;
	writel(val, priv->regs + DISP_REG_OVL_BLD_EN);
}

void mtk_blender_stop(struct device *dev)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(dev);
	u32 val;

	/* Disable and reset */
	val = readl(priv->regs + DISP_REG_OVL_BLD_EN);
	val = val & ~OVL_BLD_EN;
	writel(val, priv->regs + DISP_REG_OVL_BLD_EN);

	val = readl(priv->regs + DISP_REG_OVL_BLD_RST);
	val |= OVL_BLD_RST;
	writel(val, priv->regs + DISP_REG_OVL_BLD_RST);

	val = readl(priv->regs + DISP_REG_OVL_BLD_RST);
	val = val & ~OVL_BLD_RST;
	writel(val, priv->regs + DISP_REG_OVL_BLD_RST);
}

void mtk_blender_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(comp->dev);
	u32 val;

	/* Set ROI for this Blender */
	val = ((h & GENMASK(15, 0)) << 16) | (w & GENMASK(15, 0));
	writel(val, priv->regs + DISP_REG_OVL_BLD_ROI_SIZE);

	/*
	 * Set both background color and constant layer color to Opaque Black
	 * (ARGB) for eventual Alpha Blending to be effective
	 */
	writel(OVL_BLD_BGCLR_BLACK, priv->regs + DISP_REG_OVL_BLD_BGCLR_CLR);
	writel(OVL_BLD_BGCLR_BLACK, priv->regs + DISP_REG_OVL_BLD_L0_CLR);
}

void mtk_blender_connect(struct mtk_ddp_comp *comp, struct device *mmsys_dev,
			      struct mtk_ddp_comp *next)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(comp->dev);
	const u32 mask = OVL_BLD_BGCLR_OUT_TO_PROC |
			 OVL_BLD_BGCLR_OUT_TO_NEXT_LAYER |
			 OVL_BLD_BGCLR_IN_SEL;
	u32 data_path = OVL_BLD_BGCLR_IN_SEL;
	u32 val;

	/* Usually the next component is either another OUTPROC or an exDMA */
	if (next->type == MTK_DISP_OUTPROC)
		data_path |= OVL_BLD_BGCLR_OUT_TO_PROC;
	else
		data_path |= OVL_BLD_BGCLR_OUT_TO_NEXT_LAYER;

	val = readl(priv->regs + DISP_REG_OVL_BLD_DATAPATH_CON);
	val = (val & ~mask) | data_path;
	writel(val, priv->regs + DISP_REG_OVL_BLD_DATAPATH_CON);

	mtk_mmsys_hw_connect(mmsys_dev,
			     comp->type, comp->inst_id, next->type, next->inst_id);
}

int mtk_blender_clk_enable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(comp->dev);

	return clk_prepare_enable(priv->clk);
}

void mtk_blender_clk_disable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_blender *priv = dev_get_drvdata(comp->dev);

	clk_disable_unprepare(priv->clk);
}

u32 mtk_blender_get_blend_modes(struct device *dev)
{
	return MTK_OVL_SUPPORT_BLEND_MODES;
}

static int mtk_blender_bind(struct device *dev, struct device *master,
				 void *data)
{
	return 0;
}

static void mtk_blender_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops mtk_disp_blender_component_ops = {
	.bind	= mtk_blender_bind,
	.unbind = mtk_blender_unbind,
};

static int mtk_blender_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_blender *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs), "Cannot get reg\n");

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Cannot get clocks\n");

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "No mediatek,gce-client-reg\n");
#endif
	platform_set_drvdata(pdev, priv);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_disp_blender_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	return 0;
}

static void mtk_blender_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_blender_component_ops);
}

static const struct of_device_id mtk_disp_blender_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-disp-blender" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_disp_blender_driver_dt_match);

struct platform_driver mtk_disp_blender_driver = {
	.probe		= mtk_blender_probe,
	.remove		= mtk_blender_remove,
	.driver		= {
		.name	= "mediatek-disp-blender",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_blender_driver_dt_match,
	},
};

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Display Controller Layer Blender Driver");
MODULE_LICENSE("GPL");
