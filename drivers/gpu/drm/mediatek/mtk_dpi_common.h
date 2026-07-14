/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 *               AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef _MTK_DPI_COMMON_H
#define _MTK_DPI_COMMON_H

#include <linux/clk.h>
#include <linux/pinctrl/consumer.h>
#include <uapi/linux/media-bus-format.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modes.h>
#include <video/videomode.h>

enum mtk_dpi_golden_setting_level {
	MTK_DPI_FHD_60FPS_1920 = 0,
	MTK_DPI_FHD_60FPS_2180,
	MTK_DPI_FHD_60FPS_2400,
	MTK_DPI_FHD_60FPS_2520,
	MTK_DPI_FHD_90FPS,
	MTK_DPI_FHD_120FPS,
	MTK_DPI_WQHD_60FPS,
	MTK_DPI_WQHD_120FPS,
	MTK_DPI_8K_30FPS,
	MTK_DPI_GSL_MAX,
};

struct mtk_dpi_gs_info {
	u32 dpi_buf_sodi_high;
	u32 dpi_buf_sodi_low;
};


enum mtk_dpi_out_bit_num {
	MTK_DPI_OUT_BIT_NUM_8BITS,
	MTK_DPI_OUT_BIT_NUM_10BITS,
	MTK_DPI_OUT_BIT_NUM_12BITS,
	MTK_DPI_OUT_BIT_NUM_16BITS
};

enum mtk_dpi_out_yc_map {
	MTK_DPI_OUT_YC_MAP_RGB,
	MTK_DPI_OUT_YC_MAP_CYCY,
	MTK_DPI_OUT_YC_MAP_YCYC,
	MTK_DPI_OUT_YC_MAP_CY,
	MTK_DPI_OUT_YC_MAP_YC
};

enum mtk_dpi_out_channel_swap {
	MTK_DPI_OUT_CHANNEL_SWAP_RGB,
	MTK_DPI_OUT_CHANNEL_SWAP_GBR,
	MTK_DPI_OUT_CHANNEL_SWAP_BRG,
	MTK_DPI_OUT_CHANNEL_SWAP_RBG,
	MTK_DPI_OUT_CHANNEL_SWAP_GRB,
	MTK_DPI_OUT_CHANNEL_SWAP_BGR
};

enum mtk_dpi_out_color_format {
	MTK_DPI_COLOR_FORMAT_RGB,
	MTK_DPI_COLOR_FORMAT_YCBCR_422,
	MTK_DPI_COLOR_FORMAT_YCBCR_444
};

struct mtk_dpi {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	void __iomem *regs;
	struct device *dev;
	struct device *mmsys_dev;
	struct clk *engine_clk;
	struct clk *pixel_clk;
	struct clk *tvd_clk;
	int irq;
	struct drm_display_mode mode;
	const struct mtk_dpi_conf *conf;
	enum mtk_dpi_out_color_format color_format;
	enum mtk_dpi_out_yc_map yc_map;
	enum mtk_dpi_out_bit_num bit_num;
	enum mtk_dpi_out_channel_swap channel_swap;
	enum mtk_dpi_golden_setting_level gs_level;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_gpio;
	struct pinctrl_state *pins_dpi;
	u32 output_fmt;
	s8 refcount;
};

enum mtk_dpi_polarity {
	MTK_DPI_POLARITY_RISING,
	MTK_DPI_POLARITY_FALLING,
};

struct mtk_dpi_polarities {
	enum mtk_dpi_polarity de_pol;
	enum mtk_dpi_polarity ck_pol;
	enum mtk_dpi_polarity hsync_pol;
	enum mtk_dpi_polarity vsync_pol;
};

struct mtk_dpi_sync_param {
	u32 sync_width;
	u32 front_porch;
	u32 back_porch;
	bool shift_half_line;
};

struct mtk_dpi_sync {
	struct mtk_dpi_sync_param hsync;
	struct mtk_dpi_sync_param vsync_l_odd;
	struct mtk_dpi_sync_param vsync_l_even;
	struct mtk_dpi_sync_param vsync_r_odd;
	struct mtk_dpi_sync_param vsync_r_even;
};

struct mtk_dpi_yc_limit {
	u16 y_top;
	u16 y_bottom;
	u16 c_top;
	u16 c_bottom;
};

struct mtk_dpi_factor {
	u32 clock;
	u8 factor;
};

/**
 * struct mtk_dpi_conf - Configuration of MediaTek DPI/DVO
 * @dpi_factor: SoC-specific pixel clock PLL factor values.
 * @num_dpi_factor: Number of pixel clock PLL factor values.
 * @reg_h_fre_con: Register address of frequency control.
 * @max_clock_khz: Max clock frequency supported for this SoCs in khz units.
 * @edge_sel_en: Enable of edge selection.
 * @output_fmts: Array of supported output formats.
 * @num_output_fmts: Quantity of supported output formats.
 * @is_ck_de_pol: Support CK/DE polarity.
 * @swap_input_support: Support input swap function.
 * @support_direct_pin: IP supports direct connection to dpi panels.
 * @dimension_mask: Mask used for HWIDTH, HPORCH, VSYNC_WIDTH and VSYNC_PORCH
 *		    (no shift).
 * @hvsize_mask: Mask of HSIZE and VSIZE mask (no shift).
 * @channel_swap_shift: Shift value of channel swap.
 * @yuv422_en_bit: Enable bit of yuv422.
 * @csc_enable_bit: Enable bit of CSC.
 * @input_2p_en_bit: Enable bit for input two pixel per round feature.
 *		     If present, implies that the feature must be enabled.
 * @pixels_per_iter: Quantity of transferred pixels per iteration.
 * @edge_cfg_in_mmsys: If the edge configuration for DPI's output needs to be set in MMSYS.
 * @clocked_by_hdmi: HDMI IP outputs clock to dpi_pixel_clk input clock, needed
 *		     for DPI registers access.
 * @output_1pixel: Enable outputting one pixel per round; if the input is two pixel per
 *                 round, the DPI hardware will internally transform it to 1T1P.
 * @quirk_hfp_bs_fix: Set back porch as back+front and use front porch as an adjustment
 *                    setting (usually zero) to fix DisplayPort BS generation.
 */
struct mtk_dpi_conf {
	const struct mtk_dpi_factor *dpi_factor;
	const u8 num_dpi_factor;
	u32 reg_h_fre_con;
	u32 max_clock_khz;
	bool edge_sel_en;
	const u32 *output_fmts;
	u32 num_output_fmts;
	bool is_ck_de_pol;
	bool swap_input_support;
	bool support_direct_pin;
	u32 dimension_mask;
	u32 hvsize_mask;
	u32 channel_swap_shift;
	u32 yuv422_en_bit;
	u32 csc_enable_bit;
	u32 input_2p_en_bit;
	u32 pixels_per_iter;
	bool edge_cfg_in_mmsys;
	bool clocked_by_hdmi;
	bool output_1pixel;
	bool quirk_hfp_bs_fix;
};

static inline struct mtk_dpi *bridge_to_dpi(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dpi, bridge);
}

static inline void mtk_dpi_mask(struct mtk_dpi *dpi, u32 offset, u32 val, u32 mask)
{
	u32 tmp = readl(dpi->regs + offset);

	tmp &= ~mask;
	tmp |= (val & mask);
	writel(tmp, dpi->regs + offset);
}

static inline enum mtk_dpi_out_bit_num
mtk_dpi_bus_fmt_bit_num(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUV8_1X24:
		return MTK_DPI_OUT_BIT_NUM_8BITS;
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_OUT_BIT_NUM_10BITS;
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_OUT_BIT_NUM_12BITS;
	}
}

static inline enum mtk_dpi_out_channel_swap
mtk_dpi_bus_fmt_channel_swap(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_OUT_CHANNEL_SWAP_RGB;
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_OUT_CHANNEL_SWAP_BGR;
	}
}

static inline enum mtk_dpi_out_color_format
mtk_dpi_bus_fmt_color_format(unsigned int out_bus_format)
{
	switch (out_bus_format) {
	default:
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_BGR888_1X24:
	case MEDIA_BUS_FMT_RGB888_2X12_LE:
	case MEDIA_BUS_FMT_RGB888_2X12_BE:
	case MEDIA_BUS_FMT_RGB101010_1X30:
		return MTK_DPI_COLOR_FORMAT_RGB;
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_YUYV10_1X20:
	case MEDIA_BUS_FMT_YUYV12_1X24:
		return MTK_DPI_COLOR_FORMAT_YCBCR_422;
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		return MTK_DPI_COLOR_FORMAT_YCBCR_444;
	}
}

u32 *mtk_dpi_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					      struct drm_bridge_state *bridge_state,
					      struct drm_crtc_state *crtc_state,
					      struct drm_connector_state *conn_state,
					      u32 output_fmt,
					      unsigned int *num_input_fmts);
u32 *mtk_dpi_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					       struct drm_bridge_state *bridge_state,
					       struct drm_crtc_state *crtc_state,
					       struct drm_connector_state *conn_state,
					       unsigned int *num_output_fmts);
int mtk_dpi_bridge_atomic_check(struct drm_bridge *bridge,
				struct drm_bridge_state *bridge_state,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state);
void mtk_dpi_bridge_mode_set(struct drm_bridge *bridge,
			     const struct drm_display_mode *mode,
			     const struct drm_display_mode *adjusted_mode);

int mtk_dpi_common_bind(struct device *dev, struct device *master, void *data);
void mtk_dpi_common_unbind(struct device *dev, struct device *master, void *data);

void mtk_dpi_power_off(struct mtk_dpi *dpi);
int mtk_dpi_power_on(struct mtk_dpi *dpi);
int mtk_dpi_set_display_mode(struct mtk_dpi *dpi, struct videomode *vm,
			     struct mtk_dpi_sync *sync,
			     struct mtk_dpi_polarities *dpi_pol,
			     struct mtk_dpi_yc_limit *limit);

int mtk_dpi_common_debug_tp_show(struct seq_file *m, bool en, u32 sel);
ssize_t mtk_dpi_common_debug_tp_write(struct file *file, const char __user *ubuf,
				      size_t len, loff_t *offp, u32 *en, u32 *type);

struct mtk_dpi *mtk_dpi_common_probe(struct platform_device *pdev,
				     const struct drm_bridge_funcs *bridge_funcs);

#endif /* _MTK_DPI_COMMON_H */
