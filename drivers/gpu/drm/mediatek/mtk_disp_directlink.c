// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek Asynchronous Direct Link (DL_ASYNC) driver
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"

#define MTK_DISP_REG_DL_IN_ASYNC0_SIZE_MT8196		0
#define MTK_DISP_REG_DL_OUT_ASYNC0_SIZE_MT8196		0x64
#define MTK_OVL_REG_DL_IN_RELAY0_SIZE_MT8196		0
#define MTK_OVL_REG_DL_OUT_RELAY0_SIZE_MT8196		0x28

#define MTK_DL_INOUT_REG_WIDTH				0x4

#define MTK_DISP_DL_RELAY_WIDTH				GENMASK(15, 0)
#define MTK_DISP_DL_RELAY_HEIGHT			GENMASK(29, 16)
#define MTK_DISP_DL_IN_RELAY_PIXITER			GENMASK(31, 30)
#  define MTK_DISP_DL_IN_RELAY_1T1P			0
#  define MTK_DISP_DL_IN_RELAY_1T2P			1
#  define MTK_DISP_DL_IN_RELAY_1T4P			2

/**
 * struct mtk_direct_link_data - SoC-specific data for DL_ASYNC
 * @dli0_size_reg:   Register offset of DL_IN_ASYNC0_SIZE
 * @dlo0_size_reg:   Register offset of DL_OUT_ASYNC0_SIZE
 * @pixels_per_iter: Number of pixels per iteration
 */
struct mtk_direct_link_data {
	u16 dli0_size_reg;
	u16 dlo0_size_reg;
	u8 pixels_per_iter;
};

/**
 * struct mtk_disp_dl_port - DirectLink Input/Output/Relay Port structure
 * @clks:            Clocks for all of the DirectLink endpoints in port
 * @link_ids:        Active DirectLink hardware endpoint identifier
 * @num_links:       Number of active DirectLink hardware endpoints
 * @p0_mtx_trig_id:  Mute-X trigger idenrifier for first DirectLink HW Endpoint
 */
struct mtk_disp_dl_port {
	struct clk **clks;
	u8 *link_ids;
	u8 num_links;
	u8 p0_mtx_trig_id;
};

/**
 * enum mtk_direct_link_port_type - DirectLink Hardware Port Type
 * @MTK_DISP_DL_IN:  DirectLink input port
 * @MTK_DISP_DL_OUT: DirectLink output port
 * @MTK_DISP_DL_PORT_MAX: Number of supported DirectLink port types
 */
enum mtk_direct_link_port_type {
	MTK_DISP_DL_IN,
	MTK_DISP_DL_OUT,
	MTK_DISP_DL_PORT_MAX
};

/**
 * struct mtk_direct_link - Main Asynchronous DirectLink driver structure
 * @data:            SoC-specific data for DirectLink
 * @regs:            DirectLink registers handle
 * @clk:             Main configuration clock for DirectLink HW
 * @cmdq_reg:        CMDQ Client register
 * @dl_port:         Array of DirectLink Port structure
 */
struct mtk_direct_link {
	const struct mtk_direct_link_data *data;
	void __iomem *regs;
	struct clk *clk;
	struct cmdq_client_reg cmdq_reg;
	struct mtk_disp_dl_port dl_port[MTK_DISP_DL_PORT_MAX];
};

static struct mtk_disp_dl_port *mtk_direct_link_get_port(struct mtk_ddp_comp *comp)
{
	struct mtk_direct_link *dl_async;

	if (comp->type != MTK_DISP_DIRECT_LINK_IN && comp->type != MTK_DISP_DIRECT_LINK_OUT)
		return NULL;

	dl_async = dev_get_drvdata(comp->dev);

	if (comp->type == MTK_DISP_DIRECT_LINK_OUT)
		return &dl_async->dl_port[MTK_DISP_DL_OUT];

	return &dl_async->dl_port[MTK_DISP_DL_IN];
}

void mtk_direct_link_add(struct mtk_ddp_comp *comp, struct mtk_mutex *mutex)
{
	struct mtk_disp_dl_port *dl_port = mtk_direct_link_get_port(comp);
	u8 port_id = dl_port->link_ids[comp->inst_id];

	mtk_mutex_add_trigger(mutex, comp->type, comp->inst_id,
			      dl_port->p0_mtx_trig_id + port_id);

	return;
}

void mtk_direct_link_mtx_remove(struct mtk_ddp_comp *comp, struct mtk_mutex *mutex)
{
	struct mtk_disp_dl_port *dl_port = mtk_direct_link_get_port(comp);
	u8 port_id = dl_port->link_ids[comp->inst_id];

	mtk_mutex_remove_trigger(mutex, comp->type, comp->inst_id,
				 dl_port->p0_mtx_trig_id + port_id);

	return;
}

void mtk_direct_link_connect(struct mtk_ddp_comp *comp, struct device *mmsys_dev,
			     struct mtk_ddp_comp *next)
{
	struct mtk_disp_dl_port *dl_comp_port = mtk_direct_link_get_port(comp);
	struct mtk_disp_dl_port *dl_next_port = mtk_direct_link_get_port(next);
	u8 comp_inst_id, next_inst_id;

	comp_inst_id = dl_comp_port ? dl_comp_port->link_ids[comp->inst_id] : comp->inst_id;
	next_inst_id = dl_next_port ? dl_next_port->link_ids[next->inst_id] : next->inst_id;

	mtk_mmsys_hw_connect(mmsys_dev, comp->type, comp_inst_id, next->type, next_inst_id);
}

void mtk_direct_link_disconnect(struct mtk_ddp_comp *comp, struct device *mmsys_dev,
				struct mtk_ddp_comp *next)
{
	struct mtk_disp_dl_port *dl_comp_port = mtk_direct_link_get_port(comp);
	struct mtk_disp_dl_port *dl_next_port = mtk_direct_link_get_port(next);
	u8 comp_inst_id, next_inst_id;

	comp_inst_id = dl_comp_port ? dl_comp_port->link_ids[comp->inst_id] : comp->inst_id;
	next_inst_id = dl_next_port ? dl_next_port->link_ids[next->inst_id] : next->inst_id;

	mtk_mmsys_hw_disconnect(mmsys_dev, comp->type, comp_inst_id, next->type, next_inst_id);
}

int mtk_direct_link_clk_enable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_dl_port *dl_port = mtk_direct_link_get_port(comp);

	return clk_prepare_enable(dl_port->clks[comp->inst_id]);
}

void mtk_direct_link_clk_disable(struct mtk_ddp_comp *comp)
{
	struct mtk_disp_dl_port *dl_port = mtk_direct_link_get_port(comp);

	clk_disable_unprepare(dl_port->clks[comp->inst_id]);
}

void mtk_direct_link_config(struct mtk_ddp_comp *comp,
			    unsigned int w, unsigned int h, unsigned int vrefresh,
			    unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_direct_link *dl_async = dev_get_drvdata(comp->dev);
	struct mtk_disp_dl_port *dl_port;
	u32 reg_offset;
	u8 port_id;
	u32 mask;
	u32 val;

	dl_port = mtk_direct_link_get_port(comp);
	if (!dl_port) {
		dev_err(comp->dev, "Could not get DirectLink port.\n");
		return;
	}

	/* DL_IN or DL_OUT */
	if (comp->type == MTK_DISP_DIRECT_LINK_OUT)
		reg_offset = dl_async->data->dlo0_size_reg;
	else
		reg_offset = dl_async->data->dli0_size_reg;

	port_id = dl_port->link_ids[comp->inst_id];
	reg_offset += port_id * MTK_DL_INOUT_REG_WIDTH;

	mask = MTK_DISP_DL_RELAY_WIDTH | MTK_DISP_DL_RELAY_HEIGHT;
	val = FIELD_PREP(MTK_DISP_DL_RELAY_WIDTH, w);
	val |= FIELD_PREP(MTK_DISP_DL_RELAY_HEIGHT, h);

	if (dl_async->data->pixels_per_iter) {
		val |= FIELD_PREP(MTK_DISP_DL_IN_RELAY_PIXITER, dl_async->data->pixels_per_iter);
		mask |= MTK_DISP_DL_IN_RELAY_PIXITER;
	}

	mtk_ddp_write_mask(NULL, val, &dl_async->cmdq_reg, dl_async->regs,
			   reg_offset, mask);

	return;
}

static int mtk_direct_link_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_direct_link *dl_async = dev_get_drvdata(dev);

	return clk_prepare_enable(dl_async->clk);
}

static void mtk_direct_link_unbind(struct device *dev, struct device *master, void *data)
{
	struct mtk_direct_link *dl_async = dev_get_drvdata(dev);

	clk_disable_unprepare(dl_async->clk);
}

static const struct component_ops mtk_direct_link_component_ops = {
	.bind	= mtk_direct_link_bind,
	.unbind = mtk_direct_link_unbind,
};

static int mtk_direct_link_port_probe(struct device *dev, struct mtk_disp_dl_port *dl_port, u32 of_port_id)
{
	struct device_node *node = dev->of_node;
	struct device_node *of_port;
	struct clk **clks;
	u8 *link_ids;
	u8 num_links = 0;
	u8 i;

	/*
	 * HW Port IDs: 0=DL_IN, 1=DL_OUT, 2=DL_IN_RELAY, 3=DL_OUT_RELAY
	 *
	 * Relay ports are currently not handled here because at the time of
	 * writing those share configurations with the DL_IN/DL_OUT ports and
	 * they don't need any special and/or extra configuration.
	 *
	 * Since port 2/3 do actually exist, those are handled gracefully, but
	 * anything greater doesn't exist, so has to return an error.
	 */
	if (of_port_id > 3)
		return dev_err_probe(dev, -EINVAL, "Port %d out of range", of_port_id);
	else if (of_port_id > 1)
		return 0;

	of_port = of_graph_get_port_by_id(node, of_port_id);
	if (!of_port)
		return -ENOENT;

	for_each_of_graph_port_endpoint(of_port, ep)
		num_links++;

	clks = devm_kcalloc(dev, num_links, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	link_ids = devm_kcalloc(dev, num_links, sizeof(*link_ids), GFP_KERNEL);
	if (!link_ids)
		return -ENOMEM;

	i = 0;
	for_each_of_graph_port_endpoint(of_port, ep) {
		struct of_endpoint endpoint;
		u32 hw_port_id;
		int ret;

		ret = of_graph_parse_endpoint(ep, &endpoint);
		if (ret)
			continue;

		ret = of_property_read_u32(ep, "mediatek,port-id", &hw_port_id);
		link_ids[i] = ret ? endpoint.id : hw_port_id;
		clks[i] = devm_get_clk_from_child(dev, ep, NULL);
		if (IS_ERR(clks[i])) {
			return dev_err_probe(dev, PTR_ERR(clks[i]),
					     "Could not get clock\n");
		}
		i++;
	}
	dl_port->clks = clks;
	dl_port->link_ids = link_ids;
	dl_port->num_links = num_links;

	return 0;
}

static int mtk_direct_link_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *main_node = dev->of_node;
	struct mtk_direct_link *dl_async;
	int in_mtx_id, out_mtx_id;
	unsigned int num_ports;
	int i, ret;

	dl_async = devm_kzalloc(dev, sizeof(*dl_async), GFP_KERNEL);
	if (!dl_async)
		return -ENOMEM;

	dl_async->data = device_get_match_data(dev);

	dl_async->regs = of_iomap(dev->of_node, 0);
	if (IS_ERR(dl_async->regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(dl_async->regs),
				     "Could not get regs\n");

	num_ports = of_graph_get_port_count(main_node);
	if (num_ports == 0)
		return dev_err_probe(dev, -ENOENT, "No Input/Output ports found.\n");

	/*
	 * Get the Trigger ID for DL_IN_0 and for DL_OUT_0, as all of the other
	 * HW ports have linearly increasing IDs.
	 */
	in_mtx_id = mtk_ddp_comp_get_mutex_trigger(main_node, MTK_DISP_DL_IN);
	out_mtx_id = mtk_ddp_comp_get_mutex_trigger(main_node, MTK_DISP_DL_OUT);
	switch (num_ports) {
	default:
		/* fallthrough */
	case 2:
		if (in_mtx_id && out_mtx_id) {
			ret = 0;
			break;
		}

		ret = in_mtx_id < 0 ? in_mtx_id : out_mtx_id;
		break;
	case 1:
		if (in_mtx_id || out_mtx_id) {
			if (in_mtx_id < 0)
				in_mtx_id = 0;
			if (out_mtx_id < 0)
				out_mtx_id = 0;

			ret = 0;
			break;
		}

		ret = in_mtx_id < 0 ? in_mtx_id : out_mtx_id;
		break;
	}

	if (ret)
		return dev_err_probe(dev, ret, "Could not get %s port 0 trigger ID\n",
				     in_mtx_id < 0 ? "input" : "output");

	if (in_mtx_id > U8_MAX || out_mtx_id > U8_MAX)
		return dev_err_probe(dev, -EINVAL, "Implausible trigger ID found\n");

	/* IDs are now validated and safe to assign */
	dl_async->dl_port[MTK_DISP_DL_IN].p0_mtx_trig_id = in_mtx_id;
	dl_async->dl_port[MTK_DISP_DL_OUT].p0_mtx_trig_id = out_mtx_id;

	i = 0;
	for_each_of_graph_port(main_node, port_node) {
		u32 port_id;

		of_property_read_u32(port_node, "reg", &port_id);
		if (port_id > 1)
			continue;

		ret = mtk_direct_link_port_probe(dev, &dl_async->dl_port[i], port_id);
		if (ret)
			return ret;

		i++;
	}

	dl_async->clk = devm_clk_get(dev, 0);
	if (IS_ERR(dl_async->clk))
		return dev_err_probe(dev, PTR_ERR(dl_async->clk),
				     "Could not get config clock\n");

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &dl_async->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	platform_set_drvdata(pdev, dl_async);

	ret = component_add(dev, &mtk_direct_link_component_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add component\n");

	dev_info(&pdev->dev, "Found %u HW Inputs and %u HW Outputs\n",
		 dl_async->dl_port[0].num_links, dl_async->dl_port[1].num_links);

	return 0;
}

static void mtk_direct_link_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_direct_link_component_ops);
}

static const struct mtk_direct_link_data mt8196_disp_dl_async_data = {
	.dli0_size_reg = MTK_DISP_REG_DL_IN_ASYNC0_SIZE_MT8196,
	.dlo0_size_reg = MTK_DISP_REG_DL_OUT_ASYNC0_SIZE_MT8196,
	.pixels_per_iter = MTK_DISP_DL_IN_RELAY_1T2P,
};

static const struct mtk_direct_link_data mt8196_ovl_dl_async_data = {
	.dli0_size_reg = MTK_OVL_REG_DL_IN_RELAY0_SIZE_MT8196,
	.dlo0_size_reg = MTK_OVL_REG_DL_OUT_RELAY0_SIZE_MT8196,
};

static const struct of_device_id mtk_direct_link_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8196-disp-direct-link", .data = &mt8196_disp_dl_async_data },
	{ .compatible = "mediatek,mt8196-ovl-direct-link", .data = &mt8196_ovl_dl_async_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_direct_link_driver_dt_match);

struct platform_driver mtk_direct_link_driver = {
	.probe		= mtk_direct_link_probe,
	.remove		= mtk_direct_link_remove,
	.driver		= {
		.name	= "mediatek-disp-direct-link",
		.of_match_table = mtk_direct_link_driver_dt_match,
	},
};

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek Display Controller Direct Link Driver");
MODULE_LICENSE("GPL");
