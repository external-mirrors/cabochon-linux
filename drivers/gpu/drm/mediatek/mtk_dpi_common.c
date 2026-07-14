// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>

#include "mtk_ddp_comp.h"
#include "mtk_dpi_common.h"
#include "mtk_drm_drv.h"

void mtk_dpi_power_off(struct mtk_dpi *dpi)
{
	if (WARN_ON(dpi->refcount == 0))
		return;

	if (--dpi->refcount != 0)
		return;

	clk_disable_unprepare(dpi->pixel_clk);
	clk_disable_unprepare(dpi->tvd_clk);
	clk_disable_unprepare(dpi->engine_clk);
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_power_off, "DRM_MTK_DPI");

int mtk_dpi_power_on(struct mtk_dpi *dpi)
{
	int ret;

	if (++dpi->refcount != 1)
		return 0;

	ret = clk_prepare_enable(dpi->engine_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable engine clock: %d\n", ret);
		goto err_refcount;
	}

	ret = clk_prepare_enable(dpi->tvd_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable tvd pll: %d\n", ret);
		goto err_engine;
	}

	ret = clk_prepare_enable(dpi->pixel_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable pixel clock: %d\n", ret);
		goto err_pixel;
	}

	return 0;

err_pixel:
	clk_disable_unprepare(dpi->tvd_clk);
err_engine:
	clk_disable_unprepare(dpi->engine_clk);
err_refcount:
	dpi->refcount--;
	return ret;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_power_on, "DRM_MTK_DPI");

static unsigned int mtk_dpi_calculate_factor(struct mtk_dpi *dpi, int mode_clk)
{
	const struct mtk_dpi_factor *dpi_factor = dpi->conf->dpi_factor;
	int i;

	for (i = 0; i < dpi->conf->num_dpi_factor; i++) {
		if (mode_clk <= dpi_factor[i].clock)
			return dpi_factor[i].factor;
	}

	/* If no match try the lowest possible factor */
	return dpi_factor[dpi->conf->num_dpi_factor - 1].factor;
}

static void mtk_dpi_set_pixel_clk(struct mtk_dpi *dpi, struct videomode *vm, int mode_clk)
{
	unsigned long pll_rate;
	unsigned int factor;

	/* let pll_rate can fix the valid range of tvdpll (1G~2GHz) */
	factor = mtk_dpi_calculate_factor(dpi, mode_clk);
	pll_rate = vm->pixelclock * factor;

	dev_dbg(dpi->dev, "Want PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, vm->pixelclock);

	clk_set_rate(dpi->tvd_clk, pll_rate);
	pll_rate = clk_get_rate(dpi->tvd_clk);

	/*
	 * Depending on the IP version, we may output a different amount of
	 * pixels for each iteration: divide the clock by this number.
	 */
	vm->pixelclock = pll_rate / factor;
	vm->pixelclock /= dpi->conf->pixels_per_iter;

	if ((dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE) ||
	    (dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_BE))
		clk_set_rate(dpi->pixel_clk, vm->pixelclock * 2);
	else
		clk_set_rate(dpi->pixel_clk, vm->pixelclock);

	vm->pixelclock = clk_get_rate(dpi->pixel_clk);

	dev_dbg(dpi->dev, "Got PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, vm->pixelclock);
}

int mtk_dpi_set_display_mode(struct mtk_dpi *dpi, struct videomode *vm,
			     struct mtk_dpi_sync *sync,
			     struct mtk_dpi_polarities *dpi_pol,
			     struct mtk_dpi_yc_limit *limit)
{
	struct drm_display_mode *mode = &dpi->mode;

	drm_display_mode_to_videomode(mode, vm);

	if (!dpi->conf->clocked_by_hdmi)
		mtk_dpi_set_pixel_clk(dpi, vm, mode->clock);

	dpi_pol->ck_pol = MTK_DPI_POLARITY_FALLING;
	dpi_pol->de_pol = MTK_DPI_POLARITY_RISING;
	dpi_pol->hsync_pol = vm->flags & DISPLAY_FLAGS_HSYNC_HIGH ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;
	dpi_pol->vsync_pol = vm->flags & DISPLAY_FLAGS_VSYNC_HIGH ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;

	/*
	 * Depending on the IP version, we may output a different amount of
	 * pixels for each iteration: adjust the display porches accordingly.
	 */
	sync->hsync.sync_width = vm->hsync_len / dpi->conf->pixels_per_iter;
	sync->hsync.back_porch = vm->hback_porch / dpi->conf->pixels_per_iter;
	sync->hsync.front_porch = vm->hfront_porch / dpi->conf->pixels_per_iter;
	sync->hsync.shift_half_line = false;

	sync->vsync_l_odd.sync_width = vm->vsync_len;
	sync->vsync_l_odd.back_porch = vm->vback_porch;
	sync->vsync_l_odd.front_porch = vm->vfront_porch;
	sync->vsync_l_odd.shift_half_line = false;

	if (vm->flags & DISPLAY_FLAGS_INTERLACED) {
		sync->vsync_l_even = sync->vsync_l_odd;
		sync->vsync_l_even.shift_half_line = true;

		if (mode->flags & DRM_MODE_FLAG_3D_MASK) {
			sync->vsync_r_odd = sync->vsync_l_odd;
			sync->vsync_r_even = sync->vsync_l_odd;
			sync->vsync_r_even.shift_half_line = true;
		}
	} else if (mode->flags & DRM_MODE_FLAG_3D_MASK) {
		sync->vsync_r_odd = sync->vsync_l_odd;
	}

	if (drm_default_rgb_quant_range(&dpi->mode) == HDMI_QUANTIZATION_RANGE_LIMITED) {
		limit->y_bottom = 0x10;
		limit->y_top = 0xfe0;
		limit->c_bottom = 0x10;
		limit->c_top = 0xfe0;
	} else {
		limit->y_bottom = 0;
		limit->y_top = 0xfff;
		limit->c_bottom = 0;
		limit->c_top = 0xfff;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_set_display_mode, "DRM_MTK_DPI");

u32 *mtk_dpi_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					      struct drm_bridge_state *bridge_state,
					      struct drm_crtc_state *crtc_state,
					      struct drm_connector_state *conn_state,
					      u32 output_fmt,
					      unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(1, sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	*num_input_fmts = 1;
	input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;

	return input_fmts;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_bridge_atomic_get_input_bus_fmts, "DRM_MTK_DPI");

u32 *mtk_dpi_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					       struct drm_bridge_state *bridge_state,
					       struct drm_crtc_state *crtc_state,
					       struct drm_connector_state *conn_state,
					       unsigned int *num_output_fmts)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);
	u32 *output_fmts;

	*num_output_fmts = 0;

	if (!dpi->conf->output_fmts) {
		dev_err(dpi->dev, "output_fmts should not be null\n");
		return NULL;
	}

	output_fmts = kcalloc(dpi->conf->num_output_fmts, sizeof(*output_fmts), GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	*num_output_fmts = dpi->conf->num_output_fmts;

	memcpy(output_fmts, dpi->conf->output_fmts,
	       sizeof(*output_fmts) * dpi->conf->num_output_fmts);

	return output_fmts;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_bridge_atomic_get_output_bus_fmts, "DRM_MTK_DPI");

int mtk_dpi_bridge_atomic_check(struct drm_bridge *bridge,
				struct drm_bridge_state *bridge_state,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	unsigned int out_bus_format = bridge_state->output_bus_cfg.format;
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	if (out_bus_format == MEDIA_BUS_FMT_FIXED && dpi->conf->num_output_fmts)
		out_bus_format = dpi->conf->output_fmts[0];

	dev_dbg(dpi->dev, "input format 0x%04x, output format 0x%04x\n",
		bridge_state->input_bus_cfg.format,
		bridge_state->output_bus_cfg.format);

	dpi->output_fmt = out_bus_format;
	dpi->bit_num = mtk_dpi_bus_fmt_bit_num(out_bus_format);
	dpi->channel_swap = mtk_dpi_bus_fmt_channel_swap(out_bus_format);
	dpi->yc_map = MTK_DPI_OUT_YC_MAP_RGB;
	dpi->color_format = mtk_dpi_bus_fmt_color_format(out_bus_format);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_bridge_atomic_check, "DRM_MTK_DPI");

void mtk_dpi_bridge_mode_set(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	drm_mode_copy(&dpi->mode, adjusted_mode);
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_bridge_mode_set, "DRM_MTK_DPI");

static const struct drm_encoder_funcs mtk_dpi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int mtk_dpi_common_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct mtk_drm_private *priv = drm_dev->dev_private;
	int ret;

	dpi->mmsys_dev = priv->mmsys_dev;
	ret = drm_encoder_init(drm_dev, &dpi->encoder, &mtk_dpi_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		dev_err(dev, "Failed to initialize decoder: %d\n", ret);
		return ret;
	}

	ret = mtk_find_possible_crtcs(drm_dev, dpi->dev);
	if (ret < 0)
		goto err_cleanup;
	dpi->encoder.possible_crtcs = ret;

	ret = drm_bridge_attach(&dpi->encoder, &dpi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto err_cleanup;

	dpi->connector = drm_bridge_connector_init(drm_dev, &dpi->encoder);
	if (IS_ERR(dpi->connector)) {
		dev_err(dev, "Unable to create bridge connector\n");
		ret = PTR_ERR(dpi->connector);
		goto err_cleanup;
	}

	return 0;

err_cleanup:
	drm_encoder_cleanup(&dpi->encoder);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_common_bind, "DRM_MTK_DPI");

void mtk_dpi_common_unbind(struct device *dev, struct device *master, void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dpi->encoder);
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_common_unbind, "DRM_MTK_DPI");

int mtk_dpi_common_debug_tp_show(struct seq_file *m, bool en, u32 sel)
{
	seq_printf(m, "DPI Test Pattern: %s\n", en ? "Enabled" : "Disabled");

	if (en) {
		seq_printf(m, "Internal pattern %d: ", sel);

		switch (sel) {
		case 0:
			seq_puts(m, "256 Vertical Gray\n");
			break;
		case 1:
			seq_puts(m, "1024 Vertical Gray\n");
			break;
		case 2:
			seq_puts(m, "256 Horizontal Gray\n");
			break;
		case 3:
			seq_puts(m, "1024 Horizontal Gray\n");
			break;
		case 4:
			seq_puts(m, "Vertical Color bars\n");
			break;
		case 6:
			seq_puts(m, "Frame border\n");
			break;
		case 7:
			seq_puts(m, "Dot moire\n");
			break;
		default:
			seq_puts(m, "Invalid selection\n");
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_common_debug_tp_show, "DRM_MTK_DPI");

ssize_t mtk_dpi_common_debug_tp_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *offp, u32 *en, u32 *type)
{
	struct seq_file *m = file->private_data;
	char buf[6];

	if (!m || !m->private || *offp || len > sizeof(buf) - 1)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	if (sscanf(buf, "%u %u", en, type) != 2)
		return -EINVAL;

	if (*en < 0 || *en > 1 || *type < 0 || *type > 7)
		return -EINVAL;

	return len;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_common_debug_tp_write, "DRM_MTK_DPI");

struct mtk_dpi *mtk_dpi_common_probe(struct platform_device *pdev,
				     const struct drm_bridge_funcs *bridge_funcs)
{
	struct device *dev = &pdev->dev;
	struct mtk_dpi *dpi;
	int ret;

	dpi = devm_drm_bridge_alloc(dev, struct mtk_dpi, bridge, bridge_funcs);
	if (IS_ERR(dpi))
		return dpi;

	dpi->dev = dev;
	dpi->conf = (struct mtk_dpi_conf *)of_device_get_match_data(dev);
	dpi->output_fmt = MEDIA_BUS_FMT_RGB888_1X24;

	dpi->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(dpi->pinctrl)) {
		dev_dbg(dev, "Cannot find pinctrl!\n");
		dpi->pinctrl = NULL;
	} else {
		dpi->pins_gpio = pinctrl_lookup_state(dpi->pinctrl, "sleep");
		if (IS_ERR(dpi->pins_gpio)) {
			dpi->pins_gpio = NULL;
			dev_dbg(dev, "Cannot find pinctrl idle!\n");
		}
		if (dpi->pins_gpio)
			pinctrl_select_state(dpi->pinctrl, dpi->pins_gpio);

		dpi->pins_dpi = pinctrl_lookup_state(dpi->pinctrl, "default");
		if (IS_ERR(dpi->pins_dpi)) {
			dpi->pins_dpi = NULL;
			dev_dbg(dev, "Cannot find pinctrl active!\n");
		}
	}

	dpi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dpi->regs))
		return dev_err_ptr_probe(dev, PTR_ERR(dpi->regs),
					 "Failed to ioremap mem resource\n");

	dpi->engine_clk = devm_clk_get(dev, "engine");
	if (IS_ERR(dpi->engine_clk))
		return dev_err_ptr_probe(dev, PTR_ERR(dpi->engine_clk),
					 "Failed to get engine clock\n");

	dpi->pixel_clk = devm_clk_get(dev, "pixel");
	if (IS_ERR(dpi->pixel_clk))
		return dev_err_ptr_probe(dev, PTR_ERR(dpi->pixel_clk),
					 "Failed to get pixel clock\n");

	dpi->tvd_clk = devm_clk_get(dev, "pll");
	if (IS_ERR(dpi->tvd_clk))
		return dev_err_ptr_probe(dev, PTR_ERR(dpi->tvd_clk),
					 "Failed to get tvdpll clock\n");

	dpi->irq = platform_get_irq(pdev, 0);
	if (dpi->irq < 0)
		return ERR_PTR(dpi->irq);

	dpi->next_bridge = devm_drm_of_get_bridge(dpi->dev, dpi->dev->of_node, 1, -1);
	if (IS_ERR(dpi->next_bridge) && PTR_ERR(dpi->next_bridge) == -ENODEV) {
		/* Old devicetree has only one endpoint */
		dpi->next_bridge = devm_drm_of_get_bridge(dpi->dev, dpi->dev->of_node, 0, 0);
	}
	if (IS_ERR(dpi->next_bridge))
		return dev_err_ptr_probe(dpi->dev, PTR_ERR(dpi->next_bridge),
					 "Failed to get bridge\n");

	platform_set_drvdata(pdev, dpi);

	dpi->bridge.of_node = dev->of_node;
	dpi->bridge.type = DRM_MODE_CONNECTOR_DPI;

	ret = devm_drm_bridge_add(dev, &dpi->bridge);
	if (ret)
		return ERR_PTR(ret);

	return dpi;
}
EXPORT_SYMBOL_NS_GPL(mtk_dpi_common_probe, "DRM_MTK_DPI");

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek DPI/DVO Common Library");
MODULE_LICENSE("GPL");
