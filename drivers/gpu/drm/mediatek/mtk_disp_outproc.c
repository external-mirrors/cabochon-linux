// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Nancy Lin <nancy.lin@mediatek.com>
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

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
#include "mtk_drm_drv.h"

#define DISP_REG_OVL_OUTPROC_INTEN			0x004
#define DISP_REG_OVL_OUTPROC_INTSTA			0x008
#  define OVL_OUTPROC_FME_CPL_INTEN			BIT(1)

#define DISP_REG_OVL_OUTPROC_DATAPATH_CON		0x010
#  define OVL_OUTPROC_DATAPATH_CON_OUTPUT_CLAMP		BIT(26)

#define DISP_REG_OVL_OUTPROC_EN				0x020
#  define OVL_OUTPROC_OVL_EN				BIT(0)

#define DISP_REG_OVL_OUTPROC_RST			0x024
#  define OVL_OUTPROC_RST				BIT(0)

#define DISP_REG_OVL_OUTPROC_SHADOW_CTRL		0x028
#  define OVL_OUTPROC_BYPASS_SHADOW			BIT(2)

#define DISP_REG_OVL_OUTPROC_ROI_SIZE			0x030

struct mtk_disp_outproc {
	void __iomem		*regs;
	struct clk		*clk;
	void			(*vblank_cb)(void *data);
	void			*vblank_cb_data;
	int			irq;
	struct cmdq_client_reg	cmdq_reg;
};

void mtk_outproc_register_vblank_cb(struct device *dev, void (*vblank_cb)(void *),
				    void *vblank_cb_data)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	priv->vblank_cb = vblank_cb;
	priv->vblank_cb_data = vblank_cb_data;
}

void mtk_outproc_unregister_vblank_cb(struct device *dev)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	priv->vblank_cb = NULL;
	priv->vblank_cb_data = NULL;
}

void mtk_outproc_enable_vblank(struct device *dev)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	writel(0x0, priv->regs + DISP_REG_OVL_OUTPROC_INTSTA);
	writel(OVL_OUTPROC_FME_CPL_INTEN, priv->regs + DISP_REG_OVL_OUTPROC_INTEN);
}

void mtk_outproc_disable_vblank(struct device *dev)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	writel(0x0, priv->regs + DISP_REG_OVL_OUTPROC_INTEN);
}

static irqreturn_t mtk_outproc_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_outproc *priv = dev_id;
	u32 val;

	val = readl(priv->regs + DISP_REG_OVL_OUTPROC_INTSTA);
	if (val == 0)
		return IRQ_NONE;

	writel(0, priv->regs + DISP_REG_OVL_OUTPROC_INTSTA);

	if (priv->vblank_cb)
		priv->vblank_cb(priv->vblank_cb_data);

	return IRQ_HANDLED;
}

void mtk_outproc_config(struct mtk_ddp_comp *comp, unsigned int w,
			unsigned int h, unsigned int vrefresh,
			unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(comp->dev);

	writel(((h & GENMASK(15, 0)) << 16) | (w & GENMASK(15, 0)),
	       priv->regs + DISP_REG_OVL_OUTPROC_ROI_SIZE);

	writel(OVL_OUTPROC_DATAPATH_CON_OUTPUT_CLAMP,
	       priv->regs + DISP_REG_OVL_OUTPROC_DATAPATH_CON);
}

void mtk_outproc_start(struct device *dev)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);
	u32 val;

	writel(OVL_OUTPROC_RST, priv->regs + DISP_REG_OVL_OUTPROC_RST);
	writel(0, priv->regs + DISP_REG_OVL_OUTPROC_RST);

	val = readl(priv->regs + DISP_REG_OVL_OUTPROC_SHADOW_CTRL);
	val |= OVL_OUTPROC_BYPASS_SHADOW;
	writel(val, priv->regs + DISP_REG_OVL_OUTPROC_SHADOW_CTRL);

	writel(OVL_OUTPROC_OVL_EN, priv->regs + DISP_REG_OVL_OUTPROC_EN);
}

void mtk_outproc_stop(struct device *dev)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	/* Don't reset at stop time to avoid vblank timeouts */
	writel(0, priv->regs + DISP_REG_OVL_OUTPROC_EN);
}

int mtk_outproc_clk_enable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(comp->dev);

	return clk_prepare_enable(priv->clk);
}

void mtk_outproc_clk_disable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(comp->dev);

	clk_disable_unprepare(priv->clk);
}

static int mtk_outproc_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	if (priv->irq)
		enable_irq(priv->irq);

	return 0;
}

static void mtk_outproc_unbind(struct device *dev, struct device *master, void *data)
{
	struct mtk_disp_outproc *priv = dev_get_drvdata(dev);

	if (priv->irq)
		disable_irq(priv->irq);
}

static const struct component_ops mtk_disp_outproc_component_ops = {
	.bind	= mtk_outproc_bind,
	.unbind = mtk_outproc_unbind,
};

static int mtk_disp_outproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_outproc *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return dev_err_probe(dev, PTR_ERR(priv->regs), "Cannot get reg resource\n");

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Cannot get clocks\n");

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "No mediatek,gce-client-reg\n");
#endif

	priv->irq = platform_get_irq_optional(pdev, 0);
	if (priv->irq > 0) {
		ret = devm_request_irq(dev, priv->irq, mtk_outproc_irq_handler,
				       IRQF_NO_AUTOEN, dev_name(dev), priv);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to request irq\n");
	}

	platform_set_drvdata(pdev, priv);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_disp_outproc_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	return ret;
}

static void mtk_disp_outproc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_outproc_component_ops);
}

static const struct of_device_id mtk_disp_outproc_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-disp-outproc" },
	{ /* sentinel */},
};

MODULE_DEVICE_TABLE(of, mtk_disp_outproc_driver_dt_match);

struct platform_driver mtk_disp_outproc_driver = {
	.probe		= mtk_disp_outproc_probe,
	.remove		= mtk_disp_outproc_remove,
	.driver		= {
		.name	= "mediatek-disp-outproc",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_disp_outproc_driver_dt_match,
	},
};

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Display Controller Output Processing Driver");
MODULE_LICENSE("GPL");
