// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Collabora Ltd.
 *   Author: Arnaud Ferraris <arnaud.ferraris@collabora.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include "phy-mtk-io.h"

/* SGMII subsystem config registers
 * Register to auto-negotiation restart
 */
#define SGMII_PCS_CONTROL_1		0x0

#define SGMII_PCS_SPEED_ABILITY		0x8

/* Register to control remote fault */
#define SGMII_SGMII_MODE		0x20
#define SGMII_RESERVED			0x24

/* Register to power up QPHY */
#define SGMII_QPHY_PWR_STATE_CTRL	0xe8

/* Register to select SGMII-0 for SNPS MAC */
#define SGMII_UTIF_CTRL			0xf4
#define SGMII_MAC_SEL			BIT(24)

#define SGMII_ANA_RG			0x128

#define NUM_CLKS	2

struct mtk_sgmii_phy_data {
	void __iomem *pcs_base;
	void __iomem *phy_base;
	struct clk_bulk_data clks[NUM_CLKS];
	phy_interface_t mode;
};

static void mtk_sgmii_phy_init(struct mtk_sgmii_phy_data *data)
{
	writel(0x80000102, data->phy_base + 0x0120);
	writel(0x00000080, data->phy_base + 0x0074);
	writel(0x00000006, data->phy_base + 0x9008);
	writel(0x00000314, data->phy_base + 0x90C0);
	writel(0x442901A0, data->phy_base + 0x90CC);
	writel(0x00001210, data->phy_base + 0x9010);
	writel(0x00001124, data->phy_base + 0x90D0);
	writel(0x94130481, data->phy_base + 0x90D4);
	writel(0x00010000, data->phy_base + 0x106C);
	writel(0x06060606, data->phy_base + 0x1038);
	writel(0x000000A6, data->phy_base + 0x107C);
	writel(0x0000FF00, data->phy_base + 0x100C);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX)
		writel(0x6893B13B, data->phy_base + 0x1098);
	else
		writel(0x5089D89E, data->phy_base + 0x1098);

	writel(0x00000001, data->phy_base + 0x109C);
	writel(0x00000369, data->phy_base + 0x10E0);
	writel(0x00000351, data->phy_base + 0x10E8);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX)
		writel(0x0FF0A4A6, data->phy_base + 0x1078);
	else
		writel(0x0FF0A0A2, data->phy_base + 0x1078);

	writel(0x0049248A, data->phy_base + 0x1084);
	writel(0x00659619, data->phy_base + 0x1080);
	if (data->mode == PHY_INTERFACE_MODE_2500BASEX) {
		writel(0xAA555500, data->phy_base + 0x20D0);
	} else {
		writel(0xAA555504, data->phy_base + 0x20D0);
		writel(0xAAA6000F, data->phy_base + 0x2004);
		writel(0x00020003, data->phy_base + 0x2040);
	}

	writel(0x0F048923, data->phy_base + 0xA038);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX)
		writel(0x0089D0C1, data->phy_base + 0xA030);
	else
		writel(0x0099D0C1, data->phy_base + 0xA030);

	writel(0x0000AAA1, data->phy_base + 0x30DC);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX) {
		writel(0x0294A552, data->phy_base + 0x30E4);
		writel(0x99885155, data->phy_base + 0x30E0);
		writel(0x00C00DBD, data->phy_base + 0x50F0);
	} else {
		writel(0x0294A55A, data->phy_base + 0x30E4);
		writel(0x00C00DAD, data->phy_base + 0x50F0);
	}

	writel(0xF5FA2AA1, data->phy_base + 0x6050);

	if (data->mode != PHY_INTERFACE_MODE_2500BASEX)
		writel(0x3CF34F00, data->phy_base + 0x6074);

	writel(0x00000E00, data->phy_base + 0x6084);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX) {
		writel(0xE505B760, data->phy_base + 0x605C);
		writel(0x78580A24, data->phy_base + 0x6044);
	} else {
		writel(0xE109B760, data->phy_base + 0x605C);
		writel(0x78580524, data->phy_base + 0x6044);
	}

	writel(0x00550455, data->phy_base + 0x604C);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX) {
		writel(0x006ED6D2, data->phy_base + 0x6078);
		writel(0x0000F4F0, data->phy_base + 0x6070);
		writel(0x28280A0A, data->phy_base + 0x6040);
	} else {
		writel(0x006F56CA, data->phy_base + 0x6078);
		writel(0x2828050A, data->phy_base + 0x6040);
	}

	udelay(1);
	writel(0x00201F21, data->phy_base + 0x00F8);
	writel(0x00050C00, data->phy_base + 0x0030);
	writel(0x20000000, data->phy_base + 0x3040);
	writel(0x0200E800, data->phy_base + 0x0070);
	usleep_range(155, 156);
	writel(0x0200C111, data->phy_base + 0x0070);
	udelay(1);
	writel(0x0200C101, data->phy_base + 0x0070);
	usleep_range(10, 11);
	writel(0x0201C111, data->phy_base + 0x0070);
	udelay(1);
	writel(0x0201C101, data->phy_base + 0x0070);
	udelay(2);

	if (data->mode == PHY_INTERFACE_MODE_2500BASEX)
		writel(0x00000007, data->phy_base + 0x90F0);
	else
		writel(0x0000000B, data->phy_base + 0x90F0);

	writel(0x02000000, data->phy_base + 0x2050);
	writel(0x00080000, data->phy_base + 0x205C);
	writel(0x02000000, data->phy_base + 0x1050);
	writel(0x03000000, data->phy_base + 0x3060);
	usleep_range(10, 11);
	writel(0x00000000, data->phy_base + 0x1050);

	if (data->mode != PHY_INTERFACE_MODE_2500BASEX) {
		usleep_range(10, 11);
		writel(0x00000000, data->phy_base + 0x2050);
		usleep_range(512, 513);
		writel(0x00000000, data->phy_base + 0x205C);
	}

	usleep_range(50, 51);
	writel(0x00201F01, data->phy_base + 0x00F8);
	writel(0x30000000, data->phy_base + 0x3040);
	usleep_range(75, 76);
	writel(0x00000000, data->phy_base + 0x3060);
	usleep_range(401, 402);
}

static int mtk_sgmii_phy_configure(struct phy *phy)
{
	struct mtk_sgmii_phy_data *data = phy_get_drvdata(phy);

	udelay(2);

	/* Enable SGMII force mode setting */
	writel(0x31120009, data->pcs_base + SGMII_SGMII_MODE);

	/* Disable SGMII AN */
	writel(0x01000140, data->pcs_base + SGMII_PCS_CONTROL_1);
	writel(0x001000FA, data->pcs_base + SGMII_RESERVED);

	/* Select PHY speed */
	if (data->mode == PHY_INTERFACE_MODE_2500BASEX)
		writel(0x00014017, data->pcs_base + SGMII_ANA_RG);
	else
		writel(0x00014013, data->pcs_base + SGMII_ANA_RG);

	/* Switch SGMII to SNPS MAC */
	mtk_phy_set_bits(data->pcs_base + SGMII_UTIF_CTRL, SGMII_MAC_SEL);

	/* Release PHYA power down state */
	writel(0x00000000, data->pcs_base + SGMII_QPHY_PWR_STATE_CTRL);

	usleep_range(161, 162);
	
	mtk_sgmii_phy_init(data);

	return 0;
}

static int mtk_sgmii_phy_power_on(struct phy *phy)
{
	struct mtk_sgmii_phy_data *data = phy_get_drvdata(phy);
	int ret = 0;

	if (data->mode != PHY_INTERFACE_MODE_SGMII &&
	    data->mode != PHY_INTERFACE_MODE_2500BASEX)
		return -EINVAL;

	ret = clk_bulk_prepare_enable(NUM_CLKS, data->clks);
	if (ret)
		return ret;

	ret = mtk_sgmii_phy_configure(phy);
	if (ret)
		clk_bulk_disable_unprepare(NUM_CLKS, data->clks);

	return ret;
}

static int mtk_sgmii_phy_set_mode(struct phy *phy, enum phy_mode mode,
					 int submode)
{
	struct mtk_sgmii_phy_data *data = phy_get_drvdata(phy);

	dev_warn(&phy->dev, "Entering %s with submode %d\n", __func__, submode);

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	if (submode != PHY_INTERFACE_MODE_SGMII &&
	    submode != PHY_INTERFACE_MODE_2500BASEX)
		return -EINVAL;

	if (submode != data->mode)
		data->mode = submode;

	if (phy->power_count == 0)
		return 0;

	return mtk_sgmii_phy_configure(phy);
}

static const struct phy_ops mtk_sgmii_phy_ops = {
	.power_on	= mtk_sgmii_phy_power_on,
	.set_mode	= mtk_sgmii_phy_set_mode,
	.owner		= THIS_MODULE,
};

static int mtk_sgmii_phy_probe(struct platform_device *pdev)
{
	struct mtk_sgmii_phy_data *data;
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct phy *phy;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->mode = PHY_INTERFACE_MODE_NA;

	data->pcs_base = devm_platform_ioremap_resource_byname(pdev, "pcs");
	if (IS_ERR(data->pcs_base))
		return PTR_ERR(data->pcs_base);

	data->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(data->phy_base))
		return PTR_ERR(data->phy_base);

	phy = devm_phy_create(dev, NULL, &mtk_sgmii_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	ret = devm_clk_bulk_get(dev, NUM_CLKS, data->clks);
	if (ret)
		return ret;

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	phy_set_drvdata(phy, data);

	return 0;
}

static const struct of_device_id mtk_sgmii_phy_of_match[] = {
	{ .compatible = "mediatek,mt8196-sgmii-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, mtk_sgmii_phy_of_match);

static struct platform_driver mtk_sgmii_phy_driver = {
	.probe	= mtk_sgmii_phy_probe,
	.driver = {
		.name	= "mtk-sgmii-phy",
		.of_match_table	= mtk_sgmii_phy_of_match,
	}
};

module_platform_driver(mtk_sgmii_phy_driver);

MODULE_DESCRIPTION("MediaTek SGMII PHY driver");
MODULE_LICENSE("GPL");
