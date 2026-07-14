// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/types.h>

#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>

#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_dpi_common.h"
#include "mtk_dpi_regs.h"
#include "mtk_drm_drv.h"

static void mtk_dpi_test_pattern_en(struct mtk_dpi *dpi, u8 type, bool enable)
{
	u32 val;

	if (enable)
		val = FIELD_PREP(DPI_PAT_SEL, type) | DPI_PAT_EN;
	else
		val = 0;

	mtk_dpi_mask(dpi, DPI_PATTERN0, val, DPI_PAT_SEL | DPI_PAT_EN);
}

static void mtk_dpi_sw_reset(struct mtk_dpi *dpi, bool reset)
{
	mtk_dpi_mask(dpi, DPI_RET, reset ? RST : 0, RST);
}

static void mtk_dpi_enable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, EN, EN);
}

static void mtk_dpi_disable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, 0, EN);
}

static void mtk_dpi_config_hsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_mask(dpi, DPI_TGEN_HWIDTH, sync->sync_width << HPW,
		     dpi->conf->dimension_mask << HPW);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH, sync->back_porch << HBP,
		     dpi->conf->dimension_mask << HBP);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH, sync->front_porch << HFP,
		     dpi->conf->dimension_mask << HFP);
}

static void mtk_dpi_config_vsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync,
				 u32 width_addr, u32 porch_addr)
{
	mtk_dpi_mask(dpi, width_addr,
		     sync->shift_half_line << VSYNC_HALF_LINE_SHIFT,
		     VSYNC_HALF_LINE_MASK);
	mtk_dpi_mask(dpi, width_addr,
		     sync->sync_width << VSYNC_WIDTH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_WIDTH_SHIFT);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->back_porch << VSYNC_BACK_PORCH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_BACK_PORCH_SHIFT);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->front_porch << VSYNC_FRONT_PORCH_SHIFT,
		     dpi->conf->dimension_mask << VSYNC_FRONT_PORCH_SHIFT);
}

static void mtk_dpi_config_pol(struct mtk_dpi *dpi,
			       struct mtk_dpi_polarities *dpi_pol)
{
	unsigned int pol;
	unsigned int mask;

	mask = HSYNC_POL | VSYNC_POL;
	pol = (dpi_pol->hsync_pol == MTK_DPI_POLARITY_RISING ? 0 : HSYNC_POL) |
	      (dpi_pol->vsync_pol == MTK_DPI_POLARITY_RISING ? 0 : VSYNC_POL);
	if (dpi->conf->is_ck_de_pol) {
		mask |= CK_POL | DE_POL;
		pol |= (dpi_pol->ck_pol == MTK_DPI_POLARITY_RISING ?
			0 : CK_POL) |
		       (dpi_pol->de_pol == MTK_DPI_POLARITY_RISING ?
			0 : DE_POL);
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, pol, mask);
}

static void mtk_dpi_config_3d(struct mtk_dpi *dpi, bool en_3d)
{
	mtk_dpi_mask(dpi, DPI_CON, en_3d ? TDFP_EN : 0, TDFP_EN);
}

static void mtk_dpi_config_interface(struct mtk_dpi *dpi, bool inter)
{
	mtk_dpi_mask(dpi, DPI_CON, inter ? INTL_EN : 0, INTL_EN);
}

static void mtk_dpi_config_fb_size(struct mtk_dpi *dpi, u32 width, u32 height)
{
	mtk_dpi_mask(dpi, DPI_SIZE, width << HSIZE,
		     dpi->conf->hvsize_mask << HSIZE);
	mtk_dpi_mask(dpi, DPI_SIZE, height << VSIZE,
		     dpi->conf->hvsize_mask << VSIZE);
}

static void mtk_dpi_config_channel_limit(struct mtk_dpi *dpi, struct mtk_dpi_yc_limit *limit)
{
	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit->y_bottom << Y_LIMINT_BOT,
		     Y_LIMINT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit->y_top << Y_LIMINT_TOP,
		     Y_LIMINT_TOP_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit->c_bottom << C_LIMIT_BOT,
		     C_LIMIT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit->c_top << C_LIMIT_TOP,
		     C_LIMIT_TOP_MASK);
}

static void mtk_dpi_config_bit_num(struct mtk_dpi *dpi,
				   enum mtk_dpi_out_bit_num num)
{
	u32 val;

	switch (num) {
	case MTK_DPI_OUT_BIT_NUM_8BITS:
		val = OUT_BIT_8;
		break;
	case MTK_DPI_OUT_BIT_NUM_10BITS:
		val = OUT_BIT_10;
		break;
	case MTK_DPI_OUT_BIT_NUM_12BITS:
		val = OUT_BIT_12;
		break;
	case MTK_DPI_OUT_BIT_NUM_16BITS:
		val = OUT_BIT_16;
		break;
	default:
		val = OUT_BIT_8;
		break;
	}
	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << OUT_BIT,
		     OUT_BIT_MASK);
}

static void mtk_dpi_config_yc_map(struct mtk_dpi *dpi,
				  enum mtk_dpi_out_yc_map map)
{
	u32 val;

	switch (map) {
	case MTK_DPI_OUT_YC_MAP_RGB:
		val = YC_MAP_RGB;
		break;
	case MTK_DPI_OUT_YC_MAP_CYCY:
		val = YC_MAP_CYCY;
		break;
	case MTK_DPI_OUT_YC_MAP_YCYC:
		val = YC_MAP_YCYC;
		break;
	case MTK_DPI_OUT_YC_MAP_CY:
		val = YC_MAP_CY;
		break;
	case MTK_DPI_OUT_YC_MAP_YC:
		val = YC_MAP_YC;
		break;
	default:
		val = YC_MAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << YC_MAP, YC_MAP_MASK);
}

static void mtk_dpi_config_channel_swap(struct mtk_dpi *dpi,
					enum mtk_dpi_out_channel_swap swap)
{
	u32 val;

	switch (swap) {
	case MTK_DPI_OUT_CHANNEL_SWAP_RGB:
		val = SWAP_RGB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GBR:
		val = SWAP_GBR;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BRG:
		val = SWAP_BRG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_RBG:
		val = SWAP_RBG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GRB:
		val = SWAP_GRB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BGR:
		val = SWAP_BGR;
		break;
	default:
		val = SWAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING,
		     val << dpi->conf->channel_swap_shift,
		     CH_SWAP_MASK << dpi->conf->channel_swap_shift);
}

static void mtk_dpi_config_yuv422_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? dpi->conf->yuv422_en_bit : 0,
		     dpi->conf->yuv422_en_bit);
}

static void mtk_dpi_config_csc_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? dpi->conf->csc_enable_bit : 0,
		     dpi->conf->csc_enable_bit);
}

static void mtk_dpi_config_swap_input(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? IN_RB_SWAP : 0, IN_RB_SWAP);
}

static void mtk_dpi_config_2n_h_fre(struct mtk_dpi *dpi)
{
	if (dpi->conf->reg_h_fre_con)
		mtk_dpi_mask(dpi, dpi->conf->reg_h_fre_con, H_FRE_2N, H_FRE_2N);
}

static void mtk_dpi_config_disable_edge(struct mtk_dpi *dpi)
{
	if (dpi->conf->edge_sel_en && dpi->conf->reg_h_fre_con)
		mtk_dpi_mask(dpi, dpi->conf->reg_h_fre_con, 0, EDGE_SEL_EN);
}

static void mtk_dpi_config_color_format(struct mtk_dpi *dpi,
					enum mtk_dpi_out_color_format format)
{
	mtk_dpi_config_channel_swap(dpi, dpi->channel_swap);

	switch (format) {
	case MTK_DPI_COLOR_FORMAT_YCBCR_444:
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, true);
		if (dpi->conf->swap_input_support)
			mtk_dpi_config_swap_input(dpi, false);
		break;
	case MTK_DPI_COLOR_FORMAT_YCBCR_422:
		mtk_dpi_config_yuv422_enable(dpi, true);
		mtk_dpi_config_csc_enable(dpi, true);

		/*
		 * If height is smaller than 720, we need to use RGB_TO_BT601
		 * to transfer to yuv422. Otherwise, we use RGB_TO_JPEG.
		 */
		mtk_dpi_mask(dpi, DPI_MATRIX_SET, dpi->mode.hdisplay <= 720 ?
			     MATRIX_SEL_RGB_TO_BT601 : MATRIX_SEL_RGB_TO_JPEG,
			     INT_MATRIX_SEL_MASK);
		break;
	default:
	case MTK_DPI_COLOR_FORMAT_RGB:
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, false);
		if (dpi->conf->swap_input_support)
			mtk_dpi_config_swap_input(dpi, false);
		break;
	}
}

static void mtk_dpi_dual_edge(struct mtk_dpi *dpi)
{
	if ((dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE) ||
	    (dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_BE)) {
		mtk_dpi_mask(dpi, DPI_DDR_SETTING, DDR_EN | DDR_4PHASE,
			     DDR_EN | DDR_4PHASE);
		mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING,
			     dpi->output_fmt == MEDIA_BUS_FMT_RGB888_2X12_LE ?
			     EDGE_SEL : 0, EDGE_SEL);
		if (dpi->conf->edge_cfg_in_mmsys)
			mtk_mmsys_ddp_dpi_fmt_config(dpi->mmsys_dev, MTK_DPI_RGB888_DDR_CON);
	} else {
		mtk_dpi_mask(dpi, DPI_DDR_SETTING, DDR_EN | DDR_4PHASE, 0);
		if (dpi->conf->edge_cfg_in_mmsys)
			mtk_mmsys_ddp_dpi_fmt_config(dpi->mmsys_dev, MTK_DPI_RGB888_SDR_CON);
	}
}

static void mtk_dpi_config_hw(struct mtk_dpi *dpi,
			      struct videomode *vm, struct mtk_dpi_sync *sync,
			      struct mtk_dpi_polarities *dpi_pol,
			      struct mtk_dpi_yc_limit *limit)
{
	struct drm_display_mode *mode = &dpi->mode;
	u32 vactive = vm->vactive;

	mtk_dpi_sw_reset(dpi, true);
	mtk_dpi_config_pol(dpi, dpi_pol);

	mtk_dpi_config_hsync(dpi, &sync->hsync);

	mtk_dpi_config_vsync(dpi, &sync->vsync_l_odd,
			     DPI_TGEN_VWIDTH, DPI_TGEN_VPORCH);
	mtk_dpi_config_vsync(dpi, &sync->vsync_r_odd,
			     DPI_TGEN_VWIDTH_RODD, DPI_TGEN_VPORCH_RODD);
	mtk_dpi_config_vsync(dpi, &sync->vsync_l_even,
			     DPI_TGEN_VWIDTH_LEVEN, DPI_TGEN_VPORCH_LEVEN);
	mtk_dpi_config_vsync(dpi, &sync->vsync_r_even,
			     DPI_TGEN_VWIDTH_REVEN, DPI_TGEN_VPORCH_REVEN);

	mtk_dpi_config_3d(dpi, !!(mode->flags & DRM_MODE_FLAG_3D_MASK));
	mtk_dpi_config_interface(dpi, !!(vm->flags & DISPLAY_FLAGS_INTERLACED));

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		vactive >>= 1;

	mtk_dpi_config_fb_size(dpi, vm->hactive, vactive);
	mtk_dpi_config_channel_limit(dpi, limit);
	mtk_dpi_config_bit_num(dpi, dpi->bit_num);
	mtk_dpi_config_channel_swap(dpi, dpi->channel_swap);
	mtk_dpi_config_color_format(dpi, dpi->color_format);

	if (dpi->conf->support_direct_pin) {
		mtk_dpi_config_yc_map(dpi, dpi->yc_map);
		mtk_dpi_config_2n_h_fre(dpi);

		/* DPI can connect to either an external bridge or the internal HDMI encoder */
		if (dpi->conf->output_1pixel)
			mtk_dpi_mask(dpi, DPI_CON, DPI_OUTPUT_1T1P_EN, DPI_OUTPUT_1T1P_EN);
		else
			mtk_dpi_dual_edge(dpi);

		mtk_dpi_config_disable_edge(dpi);
	}

	if (dpi->conf->input_2p_en_bit)
		mtk_dpi_mask(dpi, DPI_CON, dpi->conf->input_2p_en_bit,
			     dpi->conf->input_2p_en_bit);

	mtk_dpi_sw_reset(dpi, false);
	return;
}

static int mtk_dpi_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	return drm_bridge_attach(encoder, dpi->next_bridge,
				 &dpi->bridge, flags);
}

static void mtk_dpi_bridge_disable(struct drm_bridge *bridge,
				   struct drm_atomic_commit *commit)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	mtk_dpi_disable(dpi);
	mtk_dpi_power_off(dpi);

	if (dpi->pinctrl && dpi->pins_gpio)
		pinctrl_select_state(dpi->pinctrl, dpi->pins_gpio);
}

static void mtk_dpi_bridge_enable(struct drm_bridge *bridge,
				  struct drm_atomic_commit *commit)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);
	struct mtk_dpi_polarities dpi_pol;
	struct mtk_dpi_sync sync = { 0 };
	struct mtk_dpi_yc_limit limit;
	struct videomode vm;

	if (dpi->pinctrl && dpi->pins_dpi)
		pinctrl_select_state(dpi->pinctrl, dpi->pins_dpi);

	mtk_dpi_power_on(dpi);

	/* Set pixel clock and initialize parameters to send to the HW */
	mtk_dpi_set_display_mode(dpi, &vm, &sync, &dpi_pol, &limit);

	/* Format and send the parameters to the HW */
	mtk_dpi_config_hw(dpi, &vm, &sync, &dpi_pol, &limit);

	mtk_dpi_enable(dpi);
}

static enum drm_mode_status
mtk_dpi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	if (mode->clock > dpi->conf->max_clock_khz)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int mtk_dpi_debug_tp_show(struct seq_file *m, void *arg)
{
	struct mtk_dpi *dpi = m->private;
	bool en;
	u32 val;

	if (!dpi)
		return -EINVAL;

	val = readl(dpi->regs + DPI_PATTERN0);
	en = val & DPI_PAT_EN;
	val = FIELD_GET(DPI_PAT_SEL, val);

	return mtk_dpi_common_debug_tp_show(m, en, val);
}

static ssize_t mtk_dpi_debug_tp_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	u32 en, type;
	ssize_t ret;

	/* seq_file and dpi pointers are checked by mtk_dpi_common_debug_tp_write() */
	ret = mtk_dpi_common_debug_tp_write(file, ubuf, len, offp, &en, &type);
	if (ret < 0)
		return ret;

	mtk_dpi_test_pattern_en((struct mtk_dpi *)m->private, type, en);
	return ret;
}

static int mtk_dpi_debug_tp_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtk_dpi_debug_tp_show, inode->i_private);
}

static const struct file_operations mtk_dpi_debug_tp_fops = {
	.owner = THIS_MODULE,
	.open = mtk_dpi_debug_tp_open,
	.read = seq_read,
	.write = mtk_dpi_debug_tp_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void mtk_dpi_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	debugfs_create_file("dpi_test_pattern", 0640, root, dpi, &mtk_dpi_debug_tp_fops);
}

static const struct drm_bridge_funcs mtk_dpi_bridge_funcs = {
	.attach = mtk_dpi_bridge_attach,
	.mode_set = mtk_dpi_bridge_mode_set,
	.mode_valid = mtk_dpi_bridge_mode_valid,
	.atomic_disable = mtk_dpi_bridge_disable,
	.atomic_enable = mtk_dpi_bridge_enable,
	.atomic_check = mtk_dpi_bridge_atomic_check,
	.atomic_get_output_bus_fmts = mtk_dpi_bridge_atomic_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = mtk_dpi_bridge_atomic_get_input_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.debugfs_init = mtk_dpi_debugfs_init,
};

void mtk_dpi_start(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	if (dpi->conf->clocked_by_hdmi)
		return;

	mtk_dpi_power_on(dpi);
}

void mtk_dpi_stop(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);

	if (dpi->conf->clocked_by_hdmi)
		return;

	mtk_dpi_disable(dpi);
	mtk_dpi_power_off(dpi);
}

unsigned int mtk_dpi_encoder_index(struct device *dev)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	unsigned int encoder_index = drm_encoder_index(&dpi->encoder);

	dev_dbg(dev, "encoder index:%d\n", encoder_index);
	return encoder_index;
}

static const struct component_ops mtk_dpi_component_ops = {
	.bind = mtk_dpi_common_bind,
	.unbind = mtk_dpi_common_unbind,
};

static const u32 mt8173_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

static const u32 mt8183_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
};

static const u32 mt8195_dpi_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB101010_1X30,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_YUYV12_1X24,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_YUV10_1X30,
};

static const u32 mt8195_dp_intf_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB888_2X12_LE,
	MEDIA_BUS_FMT_RGB888_2X12_BE,
	MEDIA_BUS_FMT_RGB101010_1X30,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
	MEDIA_BUS_FMT_BGR888_1X24,
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_YUV10_1X30,
};

static const struct mtk_dpi_factor dpi_factor_mt2701[] = {
	{ 64000, 4 }, { 128000, 2 }, { U32_MAX, 1 }
};

static const struct mtk_dpi_factor dpi_factor_mt8173[] = {
	{ 27000, 48 }, { 84000, 24 }, { 167000, 12 }, { U32_MAX, 6 }
};

static const struct mtk_dpi_factor dpi_factor_mt8183[] = {
	{ 27000, 8 }, { 167000, 4 }, { U32_MAX, 2 }
};

static const struct mtk_dpi_factor dpi_factor_mt8195_dp_intf[] = {
	{ 70000 - 1, 4 }, { 200000 - 1, 2 }, { U32_MAX, 1 }
};

static const struct mtk_dpi_conf mt8173_conf = {
	.dpi_factor = dpi_factor_mt8173,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8173),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 300000,
	.output_fmts = mt8173_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8173_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt2701_conf = {
	.dpi_factor = dpi_factor_mt2701,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt2701),
	.reg_h_fre_con = 0xb0,
	.edge_sel_en = true,
	.max_clock_khz = 150000,
	.output_fmts = mt8173_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8173_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8183_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 100000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8186_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 150000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.edge_cfg_in_mmsys = true,
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8192_conf = {
	.dpi_factor = dpi_factor_mt8183,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8183),
	.reg_h_fre_con = 0xe0,
	.max_clock_khz = 150000,
	.output_fmts = mt8183_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8183_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
};

static const struct mtk_dpi_conf mt8195_conf = {
	.max_clock_khz = 594000,
	.output_fmts = mt8195_dpi_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8195_dpi_output_fmts),
	.pixels_per_iter = 1,
	.is_ck_de_pol = true,
	.swap_input_support = true,
	.support_direct_pin = true,
	.dimension_mask = HPW_MASK,
	.hvsize_mask = HSIZE_MASK,
	.channel_swap_shift = CH_SWAP,
	.yuv422_en_bit = YUV422_EN,
	.csc_enable_bit = CSC_ENABLE,
	.input_2p_en_bit = DPI_INPUT_2P_EN,
	.clocked_by_hdmi = true,
	.output_1pixel = true,
};

static const struct mtk_dpi_conf mt8195_dpintf_conf = {
	.dpi_factor = dpi_factor_mt8195_dp_intf,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8195_dp_intf),
	.max_clock_khz = 600000,
	.output_fmts = mt8195_dp_intf_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8195_dp_intf_output_fmts),
	.pixels_per_iter = 4,
	.dimension_mask = DPINTF_HPW_MASK,
	.hvsize_mask = DPINTF_HSIZE_MASK,
	.channel_swap_shift = DPINTF_CH_SWAP,
	.yuv422_en_bit = DPINTF_YUV422_EN,
	.csc_enable_bit = DPINTF_CSC_ENABLE,
	.input_2p_en_bit = DPINTF_INPUT_2P_EN,
};

static int mtk_dpi_probe(struct platform_device *pdev)
{
	struct mtk_dpi *dpi;
	int ret;

	dpi = mtk_dpi_common_probe(pdev, &mtk_dpi_bridge_funcs);
	if (IS_ERR(dpi))
		return PTR_ERR(dpi);

	ret = component_add(&pdev->dev, &mtk_dpi_component_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to add component.\n");

	return 0;
}

static void mtk_dpi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_dpi_component_ops);
}

static const struct of_device_id mtk_dpi_of_ids[] = {
	{ .compatible = "mediatek,mt2701-dpi", .data = &mt2701_conf },
	{ .compatible = "mediatek,mt8173-dpi", .data = &mt8173_conf },
	{ .compatible = "mediatek,mt8183-dpi", .data = &mt8183_conf },
	{ .compatible = "mediatek,mt8186-dpi", .data = &mt8186_conf },
	{ .compatible = "mediatek,mt8188-dp-intf", .data = &mt8195_dpintf_conf },
	{ .compatible = "mediatek,mt8192-dpi", .data = &mt8192_conf },
	{ .compatible = "mediatek,mt8195-dp-intf", .data = &mt8195_dpintf_conf },
	{ .compatible = "mediatek,mt8195-dpi", .data = &mt8195_conf },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_dpi_of_ids);

struct platform_driver mtk_dpi_driver = {
	.probe = mtk_dpi_probe,
	.remove = mtk_dpi_remove,
	.driver = {
		.name = "mediatek-dpi",
		.of_match_table = mtk_dpi_of_ids,
	},
};
MODULE_IMPORT_NS("DRM_MTK_DPI");
