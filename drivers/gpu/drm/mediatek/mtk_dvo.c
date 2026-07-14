// SPDX-License-Identifier: GPL-2.0-only
/*
 * MediaTek Digital Video Out
 *
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/types.h>

#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

#include "mtk_drm_drv.h"
#include "mtk_crtc.h"
#include "mtk_disp_drv.h"
#include "mtk_dpi_common.h"
#include "mtk_dvo_regs.h"

#define MTK_DVO_OUT_PIXITER_1T1P		0
#define MTK_DVO_OUT_PIXITER_1T2P		1
#define MTK_DVO_OUT_PIXITER_1T4P		2

/* DVO INPUT default value is 1T2P */
#define MTK_DVO_INPUT_MODE			2
/* DVO OUTPUT value is 1T4P*/
#define MTK_DVO_OUTPUT_MODE			4
/* 1 unit = 2 group data = 2 * 1T4P = 8P */
#define MTK_DISP_BUF_SRAM_UNIT_SIZE		8
#define MTK_DISP_LINE_BUF_DVO_US		40
#define MTK_BLANKING_RATIO			1
#define MTK_DVO_EDP_MAX_CLK			297
#define MTK_DVO_MAX_HACTIVE			3840
#define MTK_DVO_LINE_BUFFER_SIZE		(MTK_DVO_MAX_HACTIVE / MTK_DISP_BUF_SRAM_UNIT_SIZE)
#define MTK_DVO_BITS_PER_CYCLE			(8 * 32 * 1000000)
#define TWAIT_SLEEP				1
#define TWAKE_UP				5
#define PREULTRA_HIGH_US			26
#define PREULTRA_LOW_US				25
#define ULTRA_HIGH_US				25
#define ULTRA_LOW_US				23
#define URGENT_HIGH_US				12
#define URGENT_LOW_US				11

static struct mtk_dpi_gs_info mtk_dvo_gs[MTK_DPI_GSL_MAX] = {
	[MTK_DPI_FHD_60FPS_1920] = { 6880, 511 },
	[MTK_DPI_8K_30FPS] = { 5255, 3899 },
};

static void mtk_dvo_test_pattern_en(struct mtk_dpi *dpi, u8 type, bool enable)
{
	u32 val;

	if (enable)
		val = FIELD_PREP(PRE_PAT_SEL_MASK, type) | PRE_PAT_EN | PRE_PAT_FORCE_ON;
	else
		val = 0;

	mtk_dpi_mask(dpi, DVO_PATTERN_CTRL, val, PRE_PAT_SEL_MASK | PRE_PAT_FORCE_ON | PRE_PAT_EN);
}

static void mtk_dvo_sw_reset(struct mtk_dpi *dvo, bool reset)
{
	mtk_dpi_mask(dvo, DVO_RET, reset ? SWRST : 0, SWRST);
}

static void mtk_dvo_enable(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_EN, EN, EN);
}

static void mtk_dvo_disable(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_EN, 0, EN);
}

static void mtk_dvo_irq_enable(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_INTEN, INT_VDE_END_EN | UNDERFLOW_EN,
		     INT_VDE_END_EN | UNDERFLOW_EN);
}

static void mtk_dvo_info_queue_start(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_TGEN_INFOQ_LATENCY, 0,
		     INFOQ_START_LATENCY_MASK | INFOQ_END_LATENCY_MASK);
}

static void mtk_dpi_buffer_ctrl(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_BUF_CON0, DISP_BUF_EN, DISP_BUF_EN);
	mtk_dpi_mask(dvo, DVO_BUF_CON0, FIFO_UNDERFLOW_DONE_BLOCK, FIFO_UNDERFLOW_DONE_BLOCK);
}

static void mtk_dvo_trailing_blank_setting(struct mtk_dpi *dvo)
{
	mtk_dpi_mask(dvo, DVO_TGEN_V_LAST_TRAILING_BLANK, 0x20, V_LAST_TRAILING_BLANK_MASK);
	mtk_dpi_mask(dvo, DVO_TGEN_OUTPUT_DELAY_LINE, 0x20, EXT_TG_DLY_LINE_MASK);
}

static void mtk_dvo_get_gs_level(struct mtk_dpi *dvo)
{
	struct drm_display_mode *mode = &dvo->mode;
	enum mtk_dpi_golden_setting_level *gsl = &dvo->gs_level;

	if (mode->hdisplay <= 1920 && mode->vdisplay <= 1080)
		*gsl = MTK_DPI_FHD_60FPS_1920;
	else
		*gsl = MTK_DPI_8K_30FPS;
}

static void mtk_dvo_sodi_setting(struct mtk_dpi *dvo, struct drm_display_mode *mode)
{
	const u64 sodi_total = MTK_DVO_BITS_PER_CYCLE * MTK_DISP_BUF_SRAM_UNIT_SIZE * 32ULL;
	u64 preultra_high, preultra_low, ultra_high, ultra_low, urgent_high, urgent_low;
	u64 sodi_high, sodi_low, sodi_high_rem, sodi_low_rem, tmp;
	u64 dvo_fifo_size, fifo_size, total_bit;
	u64 consume_rate, consume_rate_rem;
	u64 fill_rate_rem, fill_rate;
	u64 data_rate;
	u32 mmsys_clk;

	mmsys_clk = mode->clock / 1000;
	if (!mmsys_clk) {
		dev_err(dvo->dev, "mmclk is zero, use default value\n");
		mmsys_clk = 273;
	}

	fill_rate = div64_u64_rem(mmsys_clk * MTK_DVO_INPUT_MODE * 30,
				  32ULL * MTK_DISP_BUF_SRAM_UNIT_SIZE, &fill_rate_rem);
	data_rate = (u64)mode->hdisplay * mode->vdisplay *
		    div64_u64((u64)mode->clock * 1000, ((u64)mode->htotal * mode->vtotal));
	consume_rate = div64_u64_rem(((data_rate * 30 * 5) / 4),
				       MTK_DVO_BITS_PER_CYCLE, &consume_rate_rem);
	total_bit = (u64)MTK_DVO_EDP_MAX_CLK * MTK_DVO_OUTPUT_MODE * 30 *
		     MTK_DISP_LINE_BUF_DVO_US;

	fifo_size = total_bit / (MTK_DISP_BUF_SRAM_UNIT_SIZE * 30);

	/* DVO supports MSO mode, so calculate with three line buffers */
	dvo_fifo_size = fifo_size + (3 * MTK_DVO_LINE_BUFFER_SIZE);

	sodi_high_rem = (u64)fill_rate_rem * MTK_DVO_BITS_PER_CYCLE;
	tmp = (u64)consume_rate_rem * (32 * MTK_DISP_BUF_SRAM_UNIT_SIZE);

	if (sodi_high_rem < tmp) {
		fill_rate -= 1;
		sodi_high_rem += sodi_total;
	}
	sodi_high_rem -= tmp;
	sodi_high_rem *= 32 * 6;
	sodi_high_rem /= sodi_total;

	sodi_high = ((dvo_fifo_size * 30 * 5) -
		    (32 * 6 * (fill_rate - consume_rate)) -
		    sodi_high_rem + (5 * 32 - 1)) / (5 * 32);

	sodi_low_rem = consume_rate_rem * (ULTRA_LOW_US + TWAKE_UP) + MTK_DVO_BITS_PER_CYCLE - 1;
	sodi_low_rem /= MTK_DVO_BITS_PER_CYCLE;

	sodi_low = consume_rate * (ULTRA_LOW_US + TWAKE_UP) + sodi_low_rem;

	preultra_high = consume_rate * PREULTRA_HIGH_US;
	preultra_high += (consume_rate_rem * PREULTRA_HIGH_US + MTK_DVO_BITS_PER_CYCLE - 1)
			 / MTK_DVO_BITS_PER_CYCLE;

	preultra_low = consume_rate * PREULTRA_LOW_US;
	preultra_low += (consume_rate_rem * PREULTRA_LOW_US + MTK_DVO_BITS_PER_CYCLE - 1)
			/ MTK_DVO_BITS_PER_CYCLE;

	ultra_high = consume_rate * ULTRA_HIGH_US;
	ultra_high += (consume_rate_rem * ULTRA_HIGH_US + MTK_DVO_BITS_PER_CYCLE - 1)
		      / MTK_DVO_BITS_PER_CYCLE;

	ultra_low = consume_rate * ULTRA_LOW_US;
	ultra_low += (consume_rate_rem * ULTRA_LOW_US + MTK_DVO_BITS_PER_CYCLE - 1)
		     / MTK_DVO_BITS_PER_CYCLE;

	urgent_high = consume_rate * URGENT_HIGH_US;
	urgent_high += (consume_rate_rem * URGENT_HIGH_US + MTK_DVO_BITS_PER_CYCLE - 1)
		       / MTK_DVO_BITS_PER_CYCLE;

	urgent_low = consume_rate * URGENT_LOW_US;
	urgent_low += (consume_rate_rem * URGENT_LOW_US + MTK_DVO_BITS_PER_CYCLE - 1)
		      / MTK_DVO_BITS_PER_CYCLE;

	mtk_dpi_mask(dvo, DVO_BUF_SODI_HIGHT, sodi_high, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_SODI_LOW, sodi_low, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_PREULTRA_HIGHT, preultra_high, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_PREULTRA_LOW, preultra_low, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_ULTRA_HIGHT, ultra_high, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_ULTRA_LOW, ultra_low, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_URGENT_HIGHT, urgent_high, DVO_DISP_BUF_MASK);
	mtk_dpi_mask(dvo, DVO_BUF_URGENT_LOW, urgent_low, DVO_DISP_BUF_MASK);
}

static void mtk_dvo_golden_setting(struct mtk_dpi *dvo)
{
	struct mtk_dpi_gs_info *gs_info = NULL;

	if (dvo->gs_level >= MTK_DPI_GSL_MAX) {
		dev_err(dvo->dev, "Invalid Golden Setting level %d\n", dvo->gs_level);
		return;
	}

	gs_info = &mtk_dvo_gs[dvo->gs_level];

	mtk_dpi_mask(dvo, DVO_BUF_SODI_HIGHT, gs_info->dpi_buf_sodi_high, GENMASK(31, 0));
	mtk_dpi_mask(dvo, DVO_BUF_SODI_LOW, gs_info->dpi_buf_sodi_low, GENMASK(31, 0));
}

static void mtk_dvo_config_hsync(struct mtk_dpi *dvo,
				 struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_mask(dvo, DVO_TGEN_H0, sync->sync_width << HSYNC,
		     dvo->conf->dimension_mask << HSYNC);
	mtk_dpi_mask(dvo, DVO_TGEN_H0, sync->front_porch << HFP,
		     dvo->conf->dimension_mask << HFP);
	mtk_dpi_mask(dvo, DVO_TGEN_H1, (sync->back_porch + sync->sync_width) << HSYNC2ACT,
		     dvo->conf->dimension_mask << HSYNC2ACT);
}

static void mtk_dvo_config_vsync(struct mtk_dpi *dvo,
				 struct mtk_dpi_sync_param *sync,
				 u32 width_addr, u32 porch_addr)
{
	mtk_dpi_mask(dvo, width_addr,
		     sync->sync_width << VSYNC,
		     dvo->conf->dimension_mask << VSYNC);
	mtk_dpi_mask(dvo, width_addr,
		     sync->front_porch << VFP,
		     dvo->conf->dimension_mask << VFP);
	mtk_dpi_mask(dvo, porch_addr,
		     (sync->back_porch + sync->sync_width) << VSYNC2ACT,
		     dvo->conf->dimension_mask << VSYNC2ACT);
}

static void mtk_dvo_config_interface(struct mtk_dpi *dvo, bool inter)
{
	mtk_dpi_mask(dvo, DVO_CON, inter ? INTL_EN : 0, INTL_EN);
}

static void mtk_dvo_config_fb_size(struct mtk_dpi *dvo, u32 width, u32 height)
{
	mtk_dpi_mask(dvo, DVO_SRC_SIZE, width << SRC_HSIZE,
		     dvo->conf->hvsize_mask << SRC_HSIZE);
	mtk_dpi_mask(dvo, DVO_SRC_SIZE, height << SRC_VSIZE,
		     dvo->conf->hvsize_mask << SRC_VSIZE);

	mtk_dpi_mask(dvo, DVO_PIC_SIZE, width << PIC_HSIZE,
		     dvo->conf->hvsize_mask << PIC_HSIZE);
	mtk_dpi_mask(dvo, DVO_PIC_SIZE, height << PIC_VSIZE,
		     dvo->conf->hvsize_mask << PIC_VSIZE);

	mtk_dpi_mask(dvo, DVO_TGEN_H1, DIV_ROUND_UP(width, dvo->conf->pixels_per_iter) << HACT,
		     dvo->conf->hvsize_mask << HACT);
	mtk_dpi_mask(dvo, DVO_TGEN_V1, height << VACT,
		     dvo->conf->hvsize_mask << VACT);
}

static void mtk_dvo_config_channel_limit(struct mtk_dpi *dpi, struct mtk_dpi_yc_limit *limit)
{
	mtk_dpi_mask(dpi, DVO_Y_LIMIT, FIELD_PREP(Y_LMT_BOT, limit->y_bottom), Y_LMT_BOT);
	mtk_dpi_mask(dpi, DVO_Y_LIMIT, FIELD_PREP(Y_LMT_TOP, limit->y_top), Y_LMT_TOP);
	mtk_dpi_mask(dpi, DVO_C_LIMIT, FIELD_PREP(C_LMT_BOT, limit->c_bottom), C_LMT_BOT);
	mtk_dpi_mask(dpi, DVO_C_LIMIT, FIELD_PREP(C_LMT_TOP, limit->c_top), C_LMT_TOP);
}

static void mtk_dvo_config_channel_swap(struct mtk_dpi *dpi,
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

	mtk_dpi_mask(dpi, DVO_OUTPUT_SET, FIELD_PREP(CH_SWAP_MASK, val), CH_SWAP_MASK);
}

static void mtk_dvo_config_csc_enable(struct mtk_dpi *dvo, bool enable)
{
	mtk_dpi_mask(dvo, DVO_MATRIX_SET, enable ? BIT(1) : 0, DVO_INT_MTX_SEL_MASK);
	mtk_dpi_mask(dvo, DVO_MATRIX_SET, enable ? DVO_CSC_EN : 0, DVO_CSC_EN);
}

static void mtk_dvo_config_yuv422_enable(struct mtk_dpi *dvo, bool enable)
{
	mtk_dpi_mask(dvo, DVO_YUV422_SET, enable ? DVO_YUV422_EN : 0, DVO_YUV422_EN);
	mtk_dpi_mask(dvo, DVO_YUV422_SET, enable ? DVO_CRYCB_MAP : 0, DVO_CRYCB_MAP);
}

static void mtk_dvo_set_output_pixiter(struct mtk_dpi *dvo)
{
	u32 val;

	switch (dvo->conf->pixels_per_iter) {
	default:
	case 0:
	case 1:
		val = MTK_DVO_OUT_PIXITER_1T1P;
		break;
	case 2:
		val = MTK_DVO_OUT_PIXITER_1T2P;
		break;
	case 3:
	case 4:
		val = MTK_DVO_OUT_PIXITER_1T4P;
		break;
	}

	/* Enable register shadow only when more than 1 pixel per iteration */
	if (val > MTK_DVO_OUT_PIXITER_1T1P) {
		mtk_dpi_mask(dvo, DVO_SHADOW_CTRL, 0, BYPASS_SHADOW);
		mtk_dpi_mask(dvo, DVO_SHADOW_CTRL, FORCE_COMMIT, FORCE_COMMIT);
	};

	mtk_dpi_mask(dvo, DVO_OUTPUT_SET, val, OUT_NP_SEL);
}

static void mtk_dvo_config_color_format(struct mtk_dpi *dvo,
					enum mtk_dpi_out_color_format format)
{
	mtk_dvo_config_channel_swap(dvo, dvo->channel_swap);

	dev_err(dvo->dev, "[DPTX] format:%d", format);

	switch (format) {
	case MTK_DPI_COLOR_FORMAT_YCBCR_444:
		mtk_dvo_config_yuv422_enable(dvo, false);
		mtk_dvo_config_csc_enable(dvo, true);
		break;
	case MTK_DPI_COLOR_FORMAT_YCBCR_422:
		mtk_dvo_config_yuv422_enable(dvo, true);
		mtk_dvo_config_csc_enable(dvo, true);
		break;
	case MTK_DPI_COLOR_FORMAT_RGB:
	default:
		mtk_dvo_config_yuv422_enable(dvo, false);
		mtk_dvo_config_csc_enable(dvo, false);
		break;
	}
}

static void mtk_dpi_config_pol(struct mtk_dpi *dpi,
			       struct mtk_dpi_polarities *dpi_pol)
{
	u32 pol;

	pol = (dpi_pol->hsync_pol == MTK_DPI_POLARITY_RISING ? 0 : HS_INV) |
	      (dpi_pol->vsync_pol == MTK_DPI_POLARITY_RISING ? 0 : VS_INV);

	mtk_dpi_mask(dpi, DVO_OUTPUT_SET, pol, HS_INV | VS_INV);
}

static void mtk_dvo_config_hw(struct mtk_dpi *dvo,
			      struct videomode *vm, struct mtk_dpi_sync *sync,
			      struct mtk_dpi_polarities *dpi_pol,
			      struct mtk_dpi_yc_limit *limit)
{
	struct drm_display_mode *mode = &dvo->mode;
	u32 vactive = vm->vactive;

	mtk_dvo_sw_reset(dvo, true);

	mtk_dvo_irq_enable(dvo);

	mtk_dpi_config_pol(dvo, dpi_pol);

	mtk_dvo_config_hsync(dvo, &sync->hsync);
	mtk_dvo_config_vsync(dvo, &sync->vsync_l_odd, DVO_TGEN_V0, DVO_TGEN_V1);
	mtk_dvo_config_vsync(dvo, &sync->vsync_l_even, DVO_TGEN_V_EVEN0, DVO_TGEN_V_EVEN1);

	mtk_dvo_config_interface(dvo, !!(vm->flags & DISPLAY_FLAGS_INTERLACED));

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		vactive >>= 1;

	mtk_dvo_config_fb_size(dvo, vm->hactive, vactive);

	mtk_dvo_config_channel_limit(dvo, limit);
	mtk_dvo_config_channel_swap(dvo, dvo->channel_swap);
	mtk_dvo_config_color_format(dvo, dvo->color_format);

	mtk_dvo_info_queue_start(dvo);
	mtk_dpi_buffer_ctrl(dvo);

	mtk_dvo_trailing_blank_setting(dvo);
	mtk_dvo_get_gs_level(dvo);
	mtk_dvo_golden_setting(dvo);
	mtk_dvo_sodi_setting(dvo, mode);

	mtk_dvo_set_output_pixiter(dvo);

	mtk_dvo_sw_reset(dvo, false);
}

static int mtk_dvo_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct mtk_dpi *dvo = bridge_to_dpi(bridge);
	int ret;

	ret = drm_bridge_attach(bridge->encoder, dvo->next_bridge,
				&dvo->bridge, 1);
	if (ret)
		dev_err(dvo->dev, "Failed to attach bridge: %d\n", ret);

	return ret;
}

static void mtk_dvo_bridge_disable(struct drm_bridge *bridge,
				   struct drm_atomic_commit *commit)
{
	struct mtk_dpi *dvo = bridge_to_dpi(bridge);

	mtk_dvo_disable(dvo);
	mtk_dpi_power_off(dvo);
}

static void mtk_dvo_bridge_enable(struct drm_bridge *bridge,
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
	mtk_dvo_config_hw(dpi, &vm, &sync, &dpi_pol, &limit);

	mtk_dvo_enable(dpi);
}

static enum drm_mode_status
mtk_dvo_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int mtk_dvo_debug_tp_show(struct seq_file *m, void *arg)
{
	struct mtk_dpi *dvo = m->private;
	bool en;
	u32 val;

	if (!dvo)
		return -EINVAL;

	val = readl(dvo->regs + DVO_PATTERN_CTRL);
	en = (val & (PRE_PAT_EN | PRE_PAT_FORCE_ON)) == (PRE_PAT_EN | PRE_PAT_FORCE_ON);
	val = FIELD_GET(PRE_PAT_SEL_MASK, val);

	return mtk_dpi_common_debug_tp_show(m, en, val);
}

static ssize_t mtk_dvo_debug_tp_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	u32 en, type;
	ssize_t ret;

	/* seq_file and dvo pointers are checked by mtk_dpi_common_debug_tp_write() */
	ret = mtk_dpi_common_debug_tp_write(file, ubuf, len, offp, &en, &type);
	if (ret < 0)
		return ret;

	mtk_dvo_test_pattern_en((struct mtk_dpi *)m->private, type, en);
	return ret;
}

static int mtk_dvo_debug_tp_open(struct inode *inode, struct file *file)
{
	return single_open(file, mtk_dvo_debug_tp_show, inode->i_private);
}

static const struct file_operations mtk_dvo_debug_tp_fops = {
	.owner = THIS_MODULE,
	.open = mtk_dvo_debug_tp_open,
	.read = seq_read,
	.write = mtk_dvo_debug_tp_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void mtk_dvo_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct mtk_dpi *dpi = bridge_to_dpi(bridge);

	debugfs_create_file("dvo_test_pattern", 0640, root, dpi, &mtk_dvo_debug_tp_fops);
}

static const struct drm_bridge_funcs mtk_dvo_bridge_funcs = {
	.attach = mtk_dvo_bridge_attach,
	.mode_set = mtk_dpi_bridge_mode_set,
	.mode_valid = mtk_dvo_bridge_mode_valid,
	.atomic_disable = mtk_dvo_bridge_disable,
	.atomic_enable = mtk_dvo_bridge_enable,
	.atomic_check = mtk_dpi_bridge_atomic_check,
	.atomic_get_output_bus_fmts = mtk_dpi_bridge_atomic_get_output_bus_fmts,
	.atomic_get_input_bus_fmts = mtk_dpi_bridge_atomic_get_input_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,
	.debugfs_init = mtk_dvo_debugfs_init,
};

void mtk_dvo_start(struct device *dev)
{
	struct mtk_dpi *dvo = dev_get_drvdata(dev);

	mtk_dpi_power_on(dvo);
}

void mtk_dvo_stop(struct device *dev)
{
	struct mtk_dpi *dvo = dev_get_drvdata(dev);

	mtk_dvo_disable(dvo);
	mtk_dpi_power_off(dvo);
}

unsigned int mtk_dvo_encoder_index(struct device *dev)
{
	struct mtk_dpi *dvo = dev_get_drvdata(dev);

	return drm_encoder_index(&dvo->encoder);
}

static const struct component_ops mtk_dvo_component_ops = {
	.bind = mtk_dpi_common_bind,
	.unbind = mtk_dpi_common_unbind,
};

static const struct mtk_dpi_factor dpi_factor_mt8196_dvo[] = {
	{ 70000 - 1, 4 }, { 200000 - 1, 2 }, { U32_MAX, 1 }
};

static const u32 mt8196_output_fmts[] = {
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

static const struct mtk_dpi_conf mt8189_conf = {
	.dpi_factor = dpi_factor_mt8196_dvo,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8196_dvo),
	.reg_h_fre_con = 0xb0,
	.max_clock_khz = 640000,
	.output_fmts = mt8196_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8196_output_fmts),
	.pixels_per_iter = 4,
	.dimension_mask = HFP_MASK,
	.hvsize_mask = PIC_HSIZE_MASK,
};

static const struct mtk_dpi_conf mt8189_dp_conf = {
	.dpi_factor = dpi_factor_mt8196_dvo,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8196_dvo),
	.reg_h_fre_con = 0xb0,
	.max_clock_khz = 640000,
	.output_fmts = mt8196_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8196_output_fmts),
	.pixels_per_iter = 4,
	.dimension_mask = HFP_MASK,
	.hvsize_mask = PIC_HSIZE_MASK,
	.quirk_hfp_bs_fix = true,
};

static const struct mtk_dpi_conf mt8196_conf = {
	.dpi_factor = dpi_factor_mt8196_dvo,
	.num_dpi_factor = ARRAY_SIZE(dpi_factor_mt8196_dvo),
	.reg_h_fre_con = 0xb0,
	.max_clock_khz = 600000,
	.output_fmts = mt8196_output_fmts,
	.num_output_fmts = ARRAY_SIZE(mt8196_output_fmts),
	.pixels_per_iter = 4,
	.dimension_mask = HFP_MASK,
	.hvsize_mask = PIC_HSIZE_MASK,
};

static int mtk_dvo_probe(struct platform_device *pdev)
{
	struct mtk_dpi *dvo;
	int ret;

	dvo = mtk_dpi_common_probe(pdev, &mtk_dvo_bridge_funcs);
	if (IS_ERR(dvo))
		return PTR_ERR(dvo);

	ret = component_add(&pdev->dev, &mtk_dvo_component_ops);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to add component.\n");

	return 0;
}

static void mtk_dvo_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_dvo_component_ops);
}

static const struct of_device_id mtk_dvo_of_ids[] = {
	{ .compatible = "mediatek,mt8189-dp-dvo", .data = &mt8189_dp_conf },
	{ .compatible = "mediatek,mt8189-edp-dvo", .data = &mt8189_conf },
	{ .compatible = "mediatek,mt8196-edp-dvo", .data = &mt8196_conf },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_dvo_of_ids);

struct platform_driver mtk_dvo_driver = {
	.probe = mtk_dvo_probe,
	.remove = mtk_dvo_remove,
	.driver = {
		.name = "mediatek-dvo",
		.of_match_table = mtk_dvo_of_ids,
	},
};
MODULE_IMPORT_NS("DRM_MTK_DPI");
