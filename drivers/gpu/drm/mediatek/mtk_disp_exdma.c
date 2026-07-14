// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <drm/drm_blend.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_disp_drv.h"
#include "mtk_disp_ovl.h"
#include "mtk_drm_drv.h"

#define DISP_REG_OVL_EXDMA_EN_CON		0xc
#  define OVL_EXDMA_OP_8BIT_MODE			BIT(4)
#  define OVL_EXDMA_HG_FOVL_EXDMA_CK_ON			BIT(8)
#  define OVL_EXDMA_HF_FOVL_EXDMA_CK_ON			BIT(10)
#define DISP_REG_OVL_EXDMA_DATAPATH_CON		0x014
#  define OVL_EXDMA_DATAPATH_CON_LAYER_SMI_ID_EN	BIT(0)
#  define OVL_EXDMA_DATAPATH_CON_GCLAST_EN		BIT(24)
#  define OVL_EXDMA_DATAPATH_CON_HDR_GCLAST_EN		BIT(25)
#define DISP_REG_OVL_EXDMA_EN			0x020
#  define OVL_EXDMA_EN					BIT(0)
#  define OVL_EXDMA_FORCE_RELAY_MODE			BIT(4)
#define DISP_REG_OVL_EXDMA_RST			0x024
#  define OVL_EXDMA_RST					BIT(0)
#define DISP_REG_OVL_EXDMA_SRC_CON		0x02c
#  define OVL_EXDMA_FORCE_CONSTANT_LAYER		BIT(4)
#define DISP_REG_OVL_EXDMA_ROI_SIZE		0x030
#define DISP_REG_OVL_EXDMA_L0_EN		0x040
#  define OVL_EXDMA_L0_EN				BIT(0)
#  define OVL_EXDMA_L0_SRC				GENMASK(9, 8)
#  define OVL_EXDMA_L0_SRC_BLENDER			0
#  define OVL_EXDMA_L0_SRC_COLOR			1
#  define OVL_EXDMA_L0_SRC_UFOD				2
#  define OVL_EXDMA_L0_SRC_PQ				3
#define DISP_REG_OVL_EXDMA_L0_OFFSET		0x044
#define DISP_REG_OVL_EXDMA_SRC_SIZE		0x048
#define DISP_REG_OVL_EXDMA_L0_CLRFMT		0x050
#  define OVL_EXDMA_CON_FLD_CLRFMT			GENMASK(3, 0)
#  define OVL_EXDMA_CON_CLRFMT_MAN			BIT(4)
#  define OVL_EXDMA_CON_FLD_CLRFMT_NB			GENMASK(9, 8)
#  define OVL_EXDMA_CON_CLRFMT_NB_10_BIT		BIT(8)
#  define OVL_EXDMA_CON_BYTE_SWAP			BIT(16)
#  define OVL_EXDMA_CON_RGB_SWAP			BIT(17)
#define DISP_REG_OVL_EXDMA_RDMA0_CTRL		0x100
#  define OVL_EXDMA_RDMA0_EN				BIT(0)
#define DISP_REG_OVL_EXDMA_RDMA_BURST_CON1	0x1f4
#  define OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN		BIT(28)
#  define OVL_EXDMA_RDMA_BURST_CON1_DDR_EN		BIT(30)
#  define OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN		BIT(31)
#define DISP_REG_OVL_EXDMA_DUMMY_REG		0x200
#  define OVL_EXDMA_EXT_DDR_EN_OPT			BIT(2)
#  define OVL_EXDMA_FORCE_EXT_DDR_EN			BIT(3)
#define DISP_REG_OVL_EXDMA_GDRDY_PRD		0x208
#define DISP_REG_OVL_EXDMA_PITCH_MSB		0x2f0
#  define OVL_EXDMA_L0_SRC_PITCH_MSB_MASK		GENMASK(3, 0)
#define DISP_REG_OVL_EXDMA_PITCH		0x2f4
#  define OVL_EXDMA_L0_CONST_BLD			BIT(28)
#  define OVL_EXDMA_L0_SRC_PITCH_MASK			GENMASK(15, 0)
#define DISP_REG_OVL_EXDMA_L0_GUSER_EXT		0x2fc
#  define OVL_EXDMA_RDMA0_L0_VCSEL			BIT(5)
#  define OVL_EXDMA_RDMA0_HDR_L0_VCSEL			BIT(21)
#define DISP_REG_OVL_EXDMA_CON			0x300
#  define OVL_EXDMA_CON_FLD_INT_MTX_SEL			GENMASK(19, 16)
#  define OVL_EXDMA_CON_INT_MTX_BT601_TO_RGB		6
#  define OVL_EXDMA_CON_INT_MTX_BT709_TO_RGB		7
#  define OVL_EXDMA_CON_INT_MTX_EN			BIT(27)
#define DISP_REG_OVL_EXDMA_ADDR			0xf40
#define DISP_REG_OVL_EXDMA_MOUT			0xff0
#  define OVL_EXDMA_MOUT_OUT_DATA			BIT(0)
#  define OVL_EXDMA_MOUT_BGCLR_OUT			BIT(1)

#define OVL_EXDMA_MAX_SIZE			(8191)

struct mtk_disp_exdma {
	void __iomem *regs;
	struct clk *clk;
	struct cmdq_client_reg cmdq_reg;
};

static unsigned int mtk_exdma_color_convert(unsigned int color_encoding)
{
	switch (color_encoding) {
	default:
	case DRM_COLOR_YCBCR_BT709:
		return FIELD_PREP_CONST(OVL_EXDMA_CON_FLD_INT_MTX_SEL,
					OVL_EXDMA_CON_INT_MTX_BT709_TO_RGB);
	case DRM_COLOR_YCBCR_BT601:
		return FIELD_PREP_CONST(OVL_EXDMA_CON_FLD_INT_MTX_SEL,
					OVL_EXDMA_CON_INT_MTX_BT601_TO_RGB);
	}
}

void mtk_exdma_start(struct device *dev)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);
	const u32 val = OVL_EXDMA_DATAPATH_CON_LAYER_SMI_ID_EN |
			OVL_EXDMA_DATAPATH_CON_HDR_GCLAST_EN |
			OVL_EXDMA_DATAPATH_CON_GCLAST_EN;

	writel(val, priv->regs + DISP_REG_OVL_EXDMA_DATAPATH_CON);
	writel(OVL_EXDMA_EN, priv->regs + DISP_REG_OVL_EXDMA_EN);
}

void mtk_exdma_stop(struct device *dev)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);

	writel(0, priv->regs + DISP_REG_OVL_EXDMA_EN);
	writel(0, priv->regs + DISP_REG_OVL_EXDMA_DATAPATH_CON);
	writel(OVL_EXDMA_RST, priv->regs + DISP_REG_OVL_EXDMA_RST);
	writel(0, priv->regs + DISP_REG_OVL_EXDMA_RST);
}

void mtk_exdma_layer_config(struct device *dev, unsigned int idx,
				 struct mtk_plane_state *state,
				 struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(dev);
	struct mtk_plane_pending_state *pending = &state->pending;
	const struct drm_format_info *fmt_info = drm_format_info(pending->format);
	const u32 blend_mode = mtk_ovl_get_blend_mode(state, MTK_OVL_SUPPORT_BLEND_MODES);
	const bool csc_enable = (fmt_info) ? fmt_info->is_yuv : false;
	u32 val;

	if (!pending->enable || pending->height == 0 || pending->width == 0 ||
	    pending->x > OVL_EXDMA_MAX_SIZE || pending->y > OVL_EXDMA_MAX_SIZE) {
		mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
				   DISP_REG_OVL_EXDMA_RDMA0_CTRL, OVL_EXDMA_RDMA0_EN);
		mtk_ddp_write_mask(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
				   DISP_REG_OVL_EXDMA_L0_EN, OVL_EXDMA_L0_EN);
		return;
	}

	/* Set layer Width and Height */
	val = ((pending->height & GENMASK(15, 0)) << 16) | (pending->width & GENMASK(15, 0));
	mtk_ddp_write(cmdq_pkt, val, &priv->cmdq_reg,
		      priv->regs, DISP_REG_OVL_EXDMA_ROI_SIZE);

	mtk_ddp_write(cmdq_pkt, val, &priv->cmdq_reg,
		      priv->regs, DISP_REG_OVL_EXDMA_SRC_SIZE);

	/* The layer X/Y */
	mtk_ddp_write(cmdq_pkt,
		      ((pending->y & GENMASK(15, 0)) << 16) | (pending->x & GENMASK(15, 0)),
		      &priv->cmdq_reg, priv->regs, DISP_REG_OVL_EXDMA_L0_OFFSET);

	/* The memory address */
	mtk_ddp_write(cmdq_pkt, pending->addr, &priv->cmdq_reg,
		      priv->regs, DISP_REG_OVL_EXDMA_ADDR);

	/* ...and the pitch. */
	val = pending->pitch;
	if (mtk_ovl_is_ignore_pixel_alpha(state, blend_mode))
		val |= OVL_EXDMA_L0_CONST_BLD;

	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_EXDMA_PITCH,
			   OVL_EXDMA_L0_CONST_BLD | OVL_EXDMA_L0_SRC_PITCH_MASK);
	mtk_ddp_write_mask(cmdq_pkt, pending->pitch >> 16, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_PITCH_MSB, OVL_EXDMA_L0_SRC_PITCH_MSB_MASK);

	val = mtk_exdma_color_convert(pending->color_encoding);
	val |= csc_enable ? OVL_EXDMA_CON_INT_MTX_EN : 0;
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs, DISP_REG_OVL_EXDMA_CON,
			   OVL_EXDMA_CON_FLD_INT_MTX_SEL | OVL_EXDMA_CON_INT_MTX_EN);

	val = mtk_ovl_fmt_convert(pending->format, blend_mode, true, false, 0,
				  OVL_EXDMA_CON_CLRFMT_MAN, OVL_EXDMA_CON_BYTE_SWAP,
				  OVL_EXDMA_CON_RGB_SWAP);
	if (mtk_ovl_is_10bit_rgb(pending->format))
		val |= OVL_EXDMA_CON_CLRFMT_NB_10_BIT;
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_L0_CLRFMT,
			   OVL_EXDMA_CON_RGB_SWAP | OVL_EXDMA_CON_BYTE_SWAP |
			   OVL_EXDMA_CON_CLRFMT_MAN | OVL_EXDMA_CON_FLD_CLRFMT |
			   OVL_EXDMA_CON_FLD_CLRFMT_NB);

	/* Virtual Channel selection */
	val = OVL_EXDMA_RDMA0_L0_VCSEL;
	mtk_ddp_write_mask(cmdq_pkt, val, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_L0_GUSER_EXT, val);

	mtk_ddp_write_mask(cmdq_pkt, OVL_EXDMA_RDMA0_EN, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_RDMA0_CTRL, OVL_EXDMA_RDMA0_EN);
	mtk_ddp_write_mask(cmdq_pkt, OVL_EXDMA_L0_EN, &priv->cmdq_reg, priv->regs,
			   DISP_REG_OVL_EXDMA_L0_EN, OVL_EXDMA_L0_EN);
}

unsigned int mtk_exdma_layer_nr(struct device *dev, int pipeline_index)
{
	return 1;
}

void mtk_exdma_config(struct mtk_ddp_comp *comp, unsigned int w,
			   unsigned int h, unsigned int vrefresh,
			   unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_exdma *priv = dev_get_drvdata(comp->dev);
	unsigned int tmp, val, mask;

	/*
	 * This configuration enables dynamic power switching mechanism for EXDMA,
	 * also known as "SRT mode".
	 * Such configuration allows the system to achieve better power efficiency.
	 */
	val = OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN | OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN;
	mask = OVL_EXDMA_RDMA_BURST_CON1_BURST16_EN | OVL_EXDMA_RDMA_BURST_CON1_DDR_EN |
	       OVL_EXDMA_RDMA_BURST_CON1_DDR_ACK_EN;
	tmp = readl(priv->regs + DISP_REG_OVL_EXDMA_RDMA_BURST_CON1);
	tmp = (tmp & ~mask) | val;
	writel(tmp, priv->regs + DISP_REG_OVL_EXDMA_RDMA_BURST_CON1);

	/*
	 * The dummy register is used in the configuration of the EXDMA engine to
	 * signal ddren_request, and get ddren_ack before accessing the DRAM to
	 * ensure data transfers occur normally.
	 */
	val = OVL_EXDMA_EXT_DDR_EN_OPT | OVL_EXDMA_FORCE_EXT_DDR_EN;
	writel(val, priv->regs + DISP_REG_OVL_EXDMA_DUMMY_REG);
	val = OVL_EXDMA_MOUT_BGCLR_OUT;
	mask = OVL_EXDMA_MOUT_BGCLR_OUT | OVL_EXDMA_MOUT_OUT_DATA;
	tmp = readl(priv->regs + DISP_REG_OVL_EXDMA_MOUT);
	tmp = (tmp & ~mask) | val;
	writel(tmp, priv->regs + DISP_REG_OVL_EXDMA_MOUT);
	writel(GENMASK(31, 0), priv->regs + DISP_REG_OVL_EXDMA_GDRDY_PRD);
	val = OVL_EXDMA_HG_FOVL_EXDMA_CK_ON | OVL_EXDMA_HF_FOVL_EXDMA_CK_ON |
	      OVL_EXDMA_OP_8BIT_MODE;
	writel(val, priv->regs + DISP_REG_OVL_EXDMA_EN_CON);
	writel(OVL_EXDMA_RDMA0_L0_VCSEL, priv->regs + DISP_REG_OVL_EXDMA_L0_GUSER_EXT);
}

const u32 *mtk_exdma_get_formats(struct device *dev)
{
	return mt8195_ovl_formats;
}

size_t mtk_exdma_get_num_formats(struct device *dev)
{
	return mt8195_ovl_formats_len;
}

int mtk_exdma_clk_enable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_exdma *exdma = dev_get_drvdata(comp->dev);

	return clk_prepare_enable(exdma->clk);
}

void mtk_exdma_clk_disable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_exdma *exdma = dev_get_drvdata(comp->dev);

	clk_disable_unprepare(exdma->clk);
}

static int mtk_exdma_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_exdma_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_exdma_component_ops = {
	.bind	= mtk_exdma_bind,
	.unbind = mtk_exdma_unbind,
};

static int mtk_disp_exdma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_disp_exdma *priv;
	int ret = 0;

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
	platform_set_drvdata(pdev, priv);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	ret = component_add(dev, &mtk_disp_exdma_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	return 0;
}

static void mtk_disp_exdma_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_exdma_component_ops);
}

static const struct of_device_id mtk_disp_exdma_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-disp-exdma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_disp_exdma_driver_dt_match);

struct platform_driver mtk_disp_exdma_driver = {
	.probe = mtk_disp_exdma_probe,
	.remove = mtk_disp_exdma_remove,
	.driver = {
		.name = "mediatek-disp-exdma",
		.owner = THIS_MODULE,
		.of_match_table = mtk_disp_exdma_driver_dt_match,
	},
};

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Nancy Lin <nancy.lin@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Display Controller extended DMA Engine (exDMA) Driver");
MODULE_LICENSE("GPL");
