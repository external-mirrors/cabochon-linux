// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 ITE
 * Copyright (c) 2026 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 */
#include <linux/cleanup.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <media/cec.h>
#include <media/cec-notifier.h>

#include <sound/hdmi-codec.h>

#include <video/videomode.h>

#define MAX_CEC_ADDR 1

/* info-block mailbox */
#define DATA_BUFFER_DEPTH 32

#define OFFSET_CHIP_ID_L	0x00
#define OFFSET_CHIP_ID_M	0x01
#define OFFSET_CHIP_ID_H	0x02
#define OFFSET_VERSION_L	0x03
#define OFFSET_VERSION_M	0x04
#define OFFSET_VERSION_H	0x05
#define OFFSET_MIPI_CONFIG_L	0x06 /* MIPI_Configuration_0 */
#define OFFSET_MIPI_CONFIG_H	0x07 /* MIPI_Configuration_1 */
#define OFFSET_TX_CONFIG	0x08
#define OFFSET_TX_SETTING	0x09
#define OFFSET_MIPI_STATUS	0x0a
#define OFFSET_TX_STATUS	0x0c
#  define B_TX_STATUS_HPD	0x7
#  define B_TX_STATUS_VIDEO_STB	0x6
#  define B_TX_STATUS_HDCP	0x4
#  define M_TX_STATUS_HDCP	0x30

#define TX_VIDEO_STB BIT(B_TX_STATUS_VIDEO_STB)
#define TX_STATUS_HPD BIT(B_TX_STATUS_HPD)

#define GET_TX_HPD_STATUS(x) ((x & TX_STATUS_HPD) >> B_TX_STATUS_HPD)
#define GET_TX_VIDEO_STATUS(x) ((x & TX_VIDEO_STB) >> B_TX_STATUS_VIDEO_STB)
#define GET_TX_HDCP_STATUS(x) ((x & M_TX_STATUS_HDCP) >> B_TX_STATUS_HDCP)

#define OFFSET_SINK_CAP 0x0D
#define B_SINK_CAP_HDCP_VER 4
#define M_SINK_CAP_HDCP_VER 0x30

#define GET_SINK_CAP_HDCP_VER(x) ((x & M_SINK_CAP_HDCP_VER) >> B_SINK_CAP_HDCP_VER)

#define OFFSET_DATA_TYPE_IDX 0x0F
#define OFFSET_DATA_BUFFER 0x20

#define OFFSET_HOST_SETTING 0xFE
#define B_CONFIG_CHG BIT(7)
#define B_SET_CHG BIT(6)
#define HOST_SETTING_VIDEO_INFO         (1)
#define HOST_SETTING_AUDIO_INFO         (2)
#define HOST_SETTING_VIDEO_AUDIO_INFO   (3)
#define HOST_SETTING_EDID_R     (0x04)
#define HOST_SETTING_HDCP_R     (0x05)
#define INFO_BUFF_REQ_DDC_W     (0x06)
#define INFO_BUFF_REQ_CEC_W     (0x07)
#define INFO_BUFF_REQ_CEC_R     (0x08)
#define INFO_BUFF_REQ_DDC_R     (0x09)

#define OFFSET_EVENT_CHG 0xFF /* Chip_Interrupt, write 1 to clear */
#define B_EVENT_CEC_TX 6
#define M_EVENT_CEC_TX	(0xC0)

#define B_EVENT_CHG_BUFFER 4
#define M_EVENT_CHG_BUFFER_STS	(0x30)

#define B_EVENT_CEC_RX	3
#define B_EVENT_CHG	1
#define B_EVENT_IC_READY 0

#define EVENT_CEC_RX BIT(B_EVENT_CEC_RX)
#define EVENT_CHG  BIT(B_EVENT_CHG)
#define EVENT_READY BIT(B_EVENT_IC_READY)

#define GET_BUFFER_STATUS(x) ((x & M_EVENT_CHG_BUFFER_STS) >> B_EVENT_CHG_BUFFER)
#define GET_CEC_STATUS(x) ((x & M_EVENT_CEC_TX) >> B_EVENT_CEC_TX)

#define TIMEOUT_INFOBLOCK_MS 800
#define TIMEOUT_INFOBLOCK_SHORT_MS 100

/* Audio Output Selector */
#define AUD_OUT_SEL_I2S		0
#define AUD_OUT_SEL_SPDIF	1

/* Audio Word Length */
#define WORD_LENGTH_16BIT	0x0
#define WORD_LENGTH_18BIT	0x1
#define WORD_LENGTH_20BIT	0x2
#define WORD_LENGTH_24BIT	0x3

/* Audio Sample Rates */
#define SAMPLE_RATE_32K		0x3
#define SAMPLE_RATE_48K		0x2
#define SAMPLE_RATE_64K		0xb
#define SAMPLE_RATE_96K		0xa
#define SAMPLE_RATE_192K	0xe
#define SAMPLE_RATE_44_1K	0x0
#define SAMPLE_RATE_88_2K	0x8
#define SAMPLE_RATE_176_4K	0xc

/* Audio bitstream type */
#define AUD_TYPE_LPCM		0
#define AUD_TYPE_NLPCM		1

/* Data Buffer Status */
#define IT6162_DATA_BUF_STS_NONE	0
#define IT6162_DATA_BUF_STS_READY	1
#define IT6162_DATA_BUF_STS_FAIL	2

enum it6162_cec_return_code {
	CEC_NONE = 0,
	CEC_ACK,
	CEC_NACK,
	CEC_FAIL,
};

enum hdcp_state {
	NO_HDCP_STATE = 0x00,
	AUTH_DONE = 0x01,
	AUTH_FAIL = 0x02,
};

enum hdcp_ver {
	NO_HDCP = 0x0,
	HDCP_14 = 0x1,
	HDCP_23 = 0x2,
};

struct it6162_audio {
	u8 select:2;
	u8 type:2;
	u8 sample_rate:4;
	u8 word_length:2;
	u8 sample_width;
	u8 channel_number;
	u8 infoframe[HDMI_INFOFRAME_SIZE(AUDIO)];
	u8 channel_status[AES_IEC958_STATUS_SIZE];
};

struct it6162_video {
	u8 vic;
	u32 clock;
	u16 htotal;
	u16 hfp;
	u16 hsw;
	u16 hbp;
	u16 hdew;
	u16 vtotal;
	u16 vfp;
	u16 vsw;
	u16 vbp;
	u16 vdew;
	u8 hpol;
	u8 vpol;
	u8 prog;
	u16 v_aspect;
	u16 h_aspect;
	u8 pix_rep;
	u8 colorspace;
};

enum sync_mode {
	SYNC_EVENT = 0x0,
	SYNC_PULSE = 0x1,
	SYNC_AUTO = 0x2,
};

struct it6162_mipirx_config {
	u8 lane_num;
	bool pn_swap;
	bool lane_swap;
	bool en_port0;
	bool en_port1;
	bool continuous_clk;
	enum sync_mode mode;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
};

struct it6162_tx_out_set {
	enum hdcp_ver hdcp_version;
	bool hdcp_encyption;
	u8 stream_ID;
};

struct it6162_infoblock_msg {
	u8 action;
	int len;
	u8 msg[32];
};

struct it6162_platform_data {
	struct regulator *pwr18;
	struct regulator *ovdd;
	struct regulator *ivdd;
	struct gpio_desc *gpiod_reset;
};

struct it6162 {
	struct drm_bridge bridge;
	struct device *dev;
	enum drm_connector_status connector_status;
	struct drm_device *drm;

	struct i2c_client *it6162_i2c;
	struct regmap *it6162_regmap;

	struct completion cmd_complete;

	struct work_struct hdcp_work;
	struct wait_queue_head wq;

	struct it6162_platform_data pdata;
	bool powered;
	/*
	 * Tracks the supplies alone.  it6162_platform_set_power() is reached
	 * both from it6162_detect_devices() and from it6162_poweron(), so it
	 * needs a state of its own: @powered is only set once the chip has been
	 * found and configured, which is too late to keep regulator_enable()
	 * and regulator_disable() balanced.
	 */
	bool supplies_on;
	bool is_hdmi;
	bool en_audio;
	bool en_cec;

	/* operations can only be served one at the time */
	struct mutex lock;
	struct mutex cec_lock;

	struct hdmi_avi_infoframe avi_info;

	/* it6162 DSI RX related params */
	struct mipi_dsi_device *dsi;

	struct it6162_mipirx_config mipirx_config;
	struct it6162_tx_out_set tx_out_set;
	struct it6162_audio audio_config;

	struct videomode vm;

	struct platform_device *audio_pdev;
	hdmi_codec_plugged_cb plugged_cb;
	struct device *codec_dev;

	u8 data_buf_sts;
	enum hdcp_state tx_hdcp_sts;
	u8 tx_hdcp_ver;

	bool bridge_hpd_enable;
	bool bridge_enable;
	const struct drm_edid *cache_edid;

	struct i2c_adapter *ddc_adap;

	struct work_struct cec_rx_work;
	struct work_struct cec_tx_work;
	struct cec_adapter *cec_adap;
	struct cec_notifier *cec_notify;
	struct cec_msg cec_tx_msg;
	enum it6162_cec_return_code cec_ret;
};

static enum drm_mode_status it6162_mode_valid(struct it6162 *it6162,
					       const struct drm_display_mode *mode);
static void it6162_show_drm_video_mode(struct it6162 *it6162,
					const struct videomode *vm);
static void it6162_notify_hpd(struct it6162 *it6162);
static void it6162_audio_update_connector_status(struct it6162 *it6162);
static int it6162_platform_set_power(struct it6162 *it6162);
static int it6162_platform_clear_power(struct it6162 *it6162);

static struct it6162 *bridge_to_it6162(struct drm_bridge *bridge)
{
	return container_of(bridge, struct it6162, bridge);
}

static const struct regmap_config it6162_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
};

/* Returns the register value, or a negative errno on I2C failure. */
static int it6162_infoblock_read(struct it6162 *it6162, unsigned int reg)
{
	unsigned int val;
	int err;
	struct device *dev = it6162->dev;

	err = regmap_read(it6162->it6162_regmap, reg, &val);
	if (err < 0) {
		dev_err(dev, "read failed rx reg[0x%x] err: %d", reg, err);
		return err;
	}

	return val;
}

static int it6162_infoblock_write(struct it6162 *it6162, unsigned int reg,
				  unsigned int val)
{
	int err;
	struct device *dev = it6162->dev;

	err = regmap_write(it6162->it6162_regmap, reg, val);

	if (err < 0) {
		dev_err(dev, "write failed rx reg[0x%x] = 0x%x err = %d",
			reg, val, err);
		return err;
	}

	return 0;
}

static inline void it6162_infoblock_bulk_read(struct it6162 *it6162,
					      unsigned int reg,
					      u8 *buf, size_t len)
{
	regmap_bulk_read(it6162->it6162_regmap, reg, buf, len);
}

static inline void it6162_infoblock_read_bufer(struct it6162 *it6162,
					       u8 *buf, size_t len)
{
	regmap_bulk_read(it6162->it6162_regmap,
			 OFFSET_DATA_BUFFER, buf, len);
}

static inline void it6162_infoblock_bulk_write(struct it6162 *it6162,
					       unsigned int reg,
					       u8 *buf, size_t len)
{
	regmap_bulk_write(it6162->it6162_regmap, reg, buf, len);
}

static inline void it6162_infoblock_write_bufer(struct it6162 *it6162,
						u8 *buf, size_t len)
{
	regmap_bulk_write(it6162->it6162_regmap,
			  OFFSET_DATA_BUFFER, buf, len);
}

static bool it6162_infoblock_complete(struct it6162 *it6162)
{
	int tmp;

	tmp = it6162_infoblock_read(it6162, OFFSET_HOST_SETTING);
	return tmp == 0 ? true : false;
}

static int it6162_infoblock_wait_complete(struct it6162 *it6162)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(TIMEOUT_INFOBLOCK_MS);

	/*
	 * Wait in short slices, re-reading the mailbox after each one: without
	 * INTP the completion is never signalled, and waiting out the full
	 * timeout first cost TIMEOUT_INFOBLOCK_MS per command.
	 */
	do {
		if (wait_for_completion_timeout(&it6162->cmd_complete,
						msecs_to_jiffies(TIMEOUT_INFOBLOCK_SHORT_MS)))
			return 0;

		/* The chip may have finished with the interrupt lost. */
		if (it6162_infoblock_complete(it6162))
			return 0;
	} while (time_before(jiffies, deadline));

	dev_err(it6162->dev, "Infoblock hardware timeout\n");
	return -ETIMEDOUT;
}

/*
 * Harvest the mailbox event bits the same way it6162_interrupt_handler() does,
 * for boards where INTP is not wired to an interrupt-capable pin.
 */
static void it6162_poll_event_chg(struct it6162 *it6162)
{
	int int_status = it6162_infoblock_read(it6162, OFFSET_EVENT_CHG);

	if (int_status < 0)
		return;

	it6162_infoblock_write(it6162, OFFSET_EVENT_CHG, 0xFF);

	if (!!GET_BUFFER_STATUS(int_status) || !!GET_CEC_STATUS(int_status)) {
		dev_dbg(it6162->dev, "IRQ int_status = %x", int_status);
		it6162->data_buf_sts = GET_BUFFER_STATUS(int_status);
		it6162->cec_ret = GET_CEC_STATUS(int_status);

		/* Release whoever is waiting on this mailbox command. */
		complete(&it6162->cmd_complete);
	}

	if (!(int_status & EVENT_CHG))
		return;
}

/* Shared by the two buffer waits, which differ only in how long they allow. */
static int it6162_infoblock_wait_buffer_ms(struct it6162 *it6162,
					   unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		it6162_poll_event_chg(it6162);

		if (it6162_infoblock_complete(it6162) &&
		    it6162->data_buf_sts == IT6162_DATA_BUF_STS_READY)
			return 0;

		usleep_range(200, 400);
	} while (time_before(jiffies, deadline));

	dev_err(it6162->dev, "%s timeout, data_buf_sts %d\n", __func__,
		it6162->data_buf_sts);

	return -ETIMEDOUT;
}

static int it6162_infoblock_wait_buffer(struct it6162 *it6162)
{
	return it6162_infoblock_wait_buffer_ms(it6162, TIMEOUT_INFOBLOCK_MS);
}

static int it6162_infoblock_wait_buffer_short(struct it6162 *it6162)
{
	return it6162_infoblock_wait_buffer_ms(it6162,
					       TIMEOUT_INFOBLOCK_SHORT_MS);
}

static void it6162_infoblock_host_set_no_wait(struct it6162 *it6162, u8 setting)
{
	it6162_infoblock_write(it6162, OFFSET_HOST_SETTING, setting);
}

static int it6162_infoblock_host_set(struct it6162 *it6162, u8 setting)
{
	reinit_completion(&it6162->cmd_complete);
	it6162_infoblock_host_set_no_wait(it6162, setting);

	return it6162_infoblock_wait_complete(it6162);
}

static int it6162_infoblock_get_data(struct it6162 *it6162,
				       u8 setting, u8 *buf)
{
	struct device *dev = it6162->dev;

	it6162_infoblock_host_set_no_wait(it6162, setting);

	/* wait for buffer ready */
	if (it6162_infoblock_wait_buffer(it6162) < 0) {
		dev_err(dev, "%s [0x%x] 0x%x timeout", __func__,
			setting, it6162->data_buf_sts);
		return -ETIMEDOUT;
	}
	if (it6162->data_buf_sts == IT6162_DATA_BUF_STS_READY) {
		it6162_infoblock_read_bufer(it6162, buf, DATA_BUFFER_DEPTH);
		return 0;
	}

	return -EIO;
}

static int it6162_infoblock_get_data_short(struct it6162 *it6162,
				       u8 setting, u8 *buf)
{
	struct device *dev = it6162->dev;

	it6162_infoblock_host_set_no_wait(it6162, setting);

	/* wait for buffer ready */
	if (it6162_infoblock_wait_buffer_short(it6162) < 0) {
		dev_err(dev, "%s [0x%x] 0x%x timeout", __func__,
			setting, it6162->data_buf_sts);
		return -ETIMEDOUT;
	}
	if (it6162->data_buf_sts == IT6162_DATA_BUF_STS_READY) {
		it6162_infoblock_read_bufer(it6162, buf, DATA_BUFFER_DEPTH);
		return 0;
	}

	return -EIO;
}

static void it6162_infoblock_mipi_config_set(struct it6162 *it6162)
{
	struct it6162_mipirx_config *cfg = &it6162->mipirx_config;
	u8 cfg_val = 0;

	cfg_val = (cfg->continuous_clk << 6) | (cfg->en_port1 << 5) |
		  (cfg->en_port0 << 4) | (cfg->lane_swap << 3) |
		  (cfg->pn_swap << 2) | (cfg->lane_num - 1);

	dev_dbg(it6162->dev, "%s 0x%02x 0x%02x", __func__,
		cfg_val, cfg->mode);

	it6162_infoblock_write(it6162, OFFSET_MIPI_CONFIG_L, cfg_val);
	it6162_infoblock_write(it6162, OFFSET_MIPI_CONFIG_H, cfg->mode);
}

static inline void it6162_infoblock_tx_config_set(struct it6162 *it6162)
{
	it6162_infoblock_write(it6162, OFFSET_TX_CONFIG, 0x00);
}

static inline void it6162_infoblock_write_msg_no_wait(struct it6162 *it6162,
						    struct it6162_infoblock_msg *msg)
{
	it6162_infoblock_write_bufer(it6162, msg->msg, msg->len);
	it6162_infoblock_host_set_no_wait(it6162, msg->action);
}

static inline void it6162_infoblock_write_msg(struct it6162 *it6162,
					struct it6162_infoblock_msg *msg)
{
	it6162_infoblock_write_msg_no_wait(it6162, msg);
	it6162_infoblock_wait_complete(it6162);
}

/*
 * Following APIs use infoblock access
 */

static void it6162_config_set(struct it6162 *it6162)
{
	guard(mutex)(&it6162->lock);
	it6162_infoblock_mipi_config_set(it6162);
	it6162_infoblock_tx_config_set(it6162);
	it6162_infoblock_host_set(it6162, B_CONFIG_CHG);
}

static void it6162_tx_hdcp_enable(struct it6162 *it6162)
{
	struct it6162_tx_out_set *tx_out = &it6162->tx_out_set;
	int tmp;

	tmp = it6162_infoblock_read(it6162, OFFSET_TX_SETTING);
	if (tmp < 0)
		return;

	tmp |= (tx_out->hdcp_version << 6) |
		(tx_out->hdcp_encyption << 5) |
		(tx_out->stream_ID << 4);

	guard(mutex)(&it6162->lock);
	it6162_infoblock_write(it6162, OFFSET_TX_SETTING, tmp);
	it6162_infoblock_host_set(it6162, B_SET_CHG);

	dev_dbg(it6162->dev, "%s 0x%02x", __func__, tmp);
}

static void it6162_tx_hdcp_disable(struct it6162 *it6162)
{
	int tmp;

	guard(mutex)(&it6162->lock);
	tmp = it6162_infoblock_read(it6162, OFFSET_TX_SETTING);
	if (tmp < 0)
		return;

	tmp &= 0x0F;
	it6162_infoblock_write(it6162, OFFSET_TX_SETTING, tmp);
	it6162_infoblock_host_set(it6162, B_SET_CHG);

	dev_dbg(it6162->dev, "%s 0x%02x", __func__, tmp);
}

static void it6162_tx_enable(struct it6162 *it6162)
{
	it6162_tx_hdcp_enable(it6162);
}

static void it6162_tx_disable(struct it6162 *it6162)
{
	it6162_tx_hdcp_disable(it6162);
}

static void it6162_get_video_setting(struct it6162 *it6162,
				     struct it6162_video *video)
{
	struct videomode *vm = &it6162->vm;
	struct hdmi_avi_infoframe *avi_info = &it6162->avi_info;

	it6162_show_drm_video_mode(it6162, &it6162->vm);

	/*
	 * The caller's struct is uninitialised and the assignments below are
	 * conditional, so clear it first - hpol, vpol and prog all reach
	 * msg[0x14] of the video-info message.
	 */
	memset(video, 0, sizeof(*video));
	video->prog = true;

	if (vm->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		video->hpol = 1;

	if (vm->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		video->vpol = 1;

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		video->prog = false;

	video->clock = vm->pixelclock / 1000;
	video->hdew = vm->hactive;

	video->hfp = vm->hfront_porch;
	video->hsw = vm->hsync_len;
	video->hbp = vm->hback_porch;
	video->htotal = vm->hactive +
			vm->hfront_porch +
			vm->hsync_len +
			vm->hback_porch;

	video->vdew = vm->vactive;
	video->vfp = vm->vfront_porch;
	video->vsw = vm->vsync_len;
	video->vbp = vm->vback_porch;
	video->vtotal = vm->vactive +
			vm->vfront_porch +
			vm->vsync_len +
			vm->vback_porch;

	video->vic = avi_info->video_code;

	dev_dbg(it6162->dev, "vic %x", video->vic);

	switch (avi_info->picture_aspect) {
	case HDMI_PICTURE_ASPECT_4_3:
		video->h_aspect = 4;
		video->v_aspect = 3;
		break;
	case HDMI_PICTURE_ASPECT_16_9:
		video->h_aspect = 16;
		video->v_aspect = 9;
		break;
	case HDMI_PICTURE_ASPECT_64_27:
		video->h_aspect = 64;
		video->v_aspect = 27;
		break;
	case HDMI_PICTURE_ASPECT_256_135:
		video->h_aspect = 256;
		video->v_aspect = 135;
		break;
	default:
		video->h_aspect = 4;
		video->v_aspect = 3;
		break;
	}
	dev_dbg(it6162->dev, "aspect %d:%d",
		video->h_aspect, video->v_aspect);

	video->pix_rep = avi_info->pixel_repeat + 1;
	dev_dbg(it6162->dev, "pix_rep %d", video->pix_rep);

	video->colorspace = avi_info->colorspace;
	dev_dbg(it6162->dev, "colorspace %d", video->colorspace);
}

static void it6162_pack_video_setting(struct it6162 *it6162,
				      struct it6162_video *video,
				      struct it6162_infoblock_msg *msg)
{
	msg->action = HOST_SETTING_VIDEO_INFO;
	msg->len = 0x1C;

	msg->msg[0x00] =  video->hdew & 0xFF;
	msg->msg[0x01] =  (video->hdew >> 8) & 0x3F;
	msg->msg[0x02] =  video->vdew & 0xFF;
	msg->msg[0x03] =  (video->vdew >> 8) & 0x3F;
	msg->msg[0x04] =  video->clock  & 0xFF;
	msg->msg[0x05] =  (video->clock >> 8) & 0xFF;
	msg->msg[0x06] =  (video->clock >> 16) & 0xFF;
	msg->msg[0x07] =  (video->clock >> 24) & 0xFF;
	msg->msg[0x08] =  video->hfp & 0xFF;
	msg->msg[0x09] =  (video->hfp >> 8) & 0x3F;
	msg->msg[0x0A] =  video->hsw & 0xFF;
	msg->msg[0x0B] =  (video->hsw >> 8) & 0x3F;
	msg->msg[0x0C] =  video->hbp & 0xFF;
	msg->msg[0x0D] =  (video->hbp >> 8) & 0x3F;
	msg->msg[0x0E] =  video->vfp & 0xFF;
	msg->msg[0x0F] =  (video->vfp >> 8) & 0x3F;
	msg->msg[0x10] =  video->vsw & 0xFF;
	msg->msg[0x11] =  (video->vsw >> 8) & 0x3F;
	msg->msg[0x12] =  video->vbp & 0xFF;
	msg->msg[0x13] =  (video->vbp >> 8) & 0x3F;
	msg->msg[0x14] =  (video->prog << 2) |
			  (video->vpol << 1) |
			  video->hpol;
	msg->msg[0x15] =  video->v_aspect;
	msg->msg[0x16] =  video->h_aspect & 0xFF;
	msg->msg[0x17] =  video->h_aspect >> 8;
	msg->msg[0x18] =  video->pix_rep;
	msg->msg[0x19] =  video->vic;
	msg->msg[0x1A] =  video->colorspace;
	msg->msg[0x1B] =  1; /* 24 bit color depth */
}

static void it6162_mipi_set_d2v_video_timing(struct it6162 *it6162)
{
	struct it6162_video video;
	struct it6162_infoblock_msg msg;

	it6162_get_video_setting(it6162, &video);
	it6162_pack_video_setting(it6162, &video, &msg);
	guard(mutex)(&it6162->lock);
	it6162_infoblock_write_msg(it6162, &msg);
}

static void it6162_remove_edid_cache(struct it6162 *it6162)
{
	if (it6162->cache_edid) {
		drm_edid_free(it6162->cache_edid);
		it6162->cache_edid = NULL;
	}
}

static void it6162_interrupt_handler(struct it6162 *it6162)
{
	int int_status, tx_status, mipi_status, sink_cap;
	enum drm_connector_status connector_status;

	int_status = it6162_infoblock_read(it6162, OFFSET_EVENT_CHG);
	if (int_status < 0)
		return;

	it6162_infoblock_write(it6162, OFFSET_EVENT_CHG, 0xFF);

	if (!!GET_BUFFER_STATUS(int_status) || !!GET_CEC_STATUS(int_status)) {
		dev_dbg(it6162->dev, "IRQ int_status = %x", int_status);
		it6162->data_buf_sts = GET_BUFFER_STATUS(int_status);
		it6162->cec_ret = GET_CEC_STATUS(int_status);

		/* Release whoever is waiting on this mailbox command. */
		complete(&it6162->cmd_complete);
	}

	/*
	 * cec_rx_work is only initialised together with the CEC adapter, so
	 * ignore RX events when CEC is not wired up (or failed to register).
	 */
	if ((int_status & EVENT_CEC_RX) && it6162->cec_adap)
		schedule_work(&it6162->cec_rx_work);

	if (!(int_status & EVENT_CHG))
		return;

	dev_dbg(it6162->dev, "event change");
	tx_status = it6162_infoblock_read(it6162, OFFSET_TX_STATUS);
	dev_dbg(it6162->dev, "Tx status %x", tx_status);

	mipi_status = it6162_infoblock_read(it6162, OFFSET_MIPI_STATUS);
	dev_dbg(it6162->dev, "MIPI status %x", mipi_status);

	sink_cap = it6162_infoblock_read(it6162, OFFSET_SINK_CAP);
	dev_dbg(it6162->dev, "SINK capability %x", sink_cap);

	if (tx_status < 0 || mipi_status < 0 || sink_cap < 0)
		return;

	connector_status = GET_TX_HPD_STATUS(tx_status) ?
			   connector_status_connected :
			   connector_status_disconnected;

	if (it6162->connector_status != connector_status) {
		it6162->connector_status = connector_status;
		it6162_notify_hpd(it6162);
		it6162_audio_update_connector_status(it6162);
		if (connector_status == connector_status_disconnected) {
			cec_phys_addr_invalidate(it6162->cec_adap);
			it6162->tx_hdcp_sts = NO_HDCP_STATE;
			it6162_remove_edid_cache(it6162);
		}
	}

	if (GET_TX_HPD_STATUS(tx_status) && GET_TX_VIDEO_STATUS(tx_status)) {
		if (it6162->tx_hdcp_sts != GET_TX_HDCP_STATUS(tx_status)) {
			it6162->tx_hdcp_sts = GET_TX_HDCP_STATUS(tx_status);
			it6162->tx_hdcp_ver = GET_SINK_CAP_HDCP_VER(sink_cap);
			dev_dbg(it6162->dev, "hdcp %x, ver %x",
				it6162->tx_hdcp_sts, it6162->tx_hdcp_ver);
			if (it6162->tx_hdcp_sts == AUTH_DONE)
				schedule_work(&it6162->hdcp_work);
		}
	}
}

static bool it6162_wait_devices(struct it6162 *it6162)
{
	struct device *dev = &it6162->it6162_i2c->dev;
	int status = 0, regEF;
	unsigned int i;

	for (i = 0; i < 10; i++) {
		msleep(200);
		regEF = it6162_infoblock_read(it6162, OFFSET_HOST_SETTING);
		status = it6162_infoblock_read(it6162, OFFSET_EVENT_CHG);
		dev_err(dev, "wait 6162 rdy %x %x %u", status, regEF, i);

		if (regEF < 0 || status < 0)
			continue;

		if (status & EVENT_READY) {
			dev_dbg(dev, "IC status %01x", status);
			return true;
		}
		it6162_infoblock_write(it6162, 0x00, 0x00);
	}

	dev_err(dev, "-ENODEV %s %x", __func__, status);
	return false;
}

static void it6162_reset_init(struct it6162 *it6162)
{
	it6162_config_set(it6162);
	it6162_tx_disable(it6162);
}

static int it6162_platform_set_power(struct it6162 *it6162)
{
	struct it6162_platform_data *pdata = &it6162->pdata;
	struct device *dev = it6162->dev;
	int err;

	if (it6162->supplies_on) {
		dev_dbg(dev, "Supplies already enabled");
		return 0;
	}

	if (pdata->ivdd) {
		err = regulator_enable(pdata->ivdd);
		if (err) {
			dev_err(dev, "Failed to enable IVDD: %d",
				err);
			return err;
		}
	}

	if (pdata->pwr18) {
		err = regulator_enable(pdata->pwr18);
		if (err) {
			dev_err(dev, "Failed to enable VDD18: %d",
				err);
			goto err_disable_ivdd;
		}
	}

	if (pdata->ovdd) {
		err = regulator_enable(pdata->ovdd);
		if (err) {
			dev_err(dev, "Failed to enable OVDD: %d", err);
			goto err_disable_pwr18;
		}
	}

	it6162->supplies_on = true;

	if (pdata->gpiod_reset) {
		usleep_range(10000, 20000);
		gpiod_set_value_cansleep(pdata->gpiod_reset, 1);
		usleep_range(1000, 2000);
		gpiod_set_value_cansleep(pdata->gpiod_reset, 0);
		usleep_range(10000, 20000);
	}

	return 0;

	/*
	 * Unwind here rather than leaving it to it6162_platform_clear_power():
	 * supplies_on is still false, so that function would return early and
	 * the supplies enabled above would leak an enable_count.
	 */
err_disable_pwr18:
	if (pdata->pwr18)
		regulator_disable(pdata->pwr18);
err_disable_ivdd:
	if (pdata->ivdd)
		regulator_disable(pdata->ivdd);

	return err;
}

static int it6162_platform_clear_power(struct it6162 *it6162)
{
	struct it6162_platform_data *pdata = &it6162->pdata;
	struct device *dev = it6162->dev;
	int err;

	if (!it6162->supplies_on) {
		dev_dbg(dev, "Supplies already disabled");
		return 0;
	}

	if (pdata->ivdd) {
		err = regulator_disable(pdata->ivdd);
		if (err) {
			dev_err(dev, "Failed to disable IVDD: %d", err);
			return err;
		}
		usleep_range(2000, 3000);
	}

	if (pdata->pwr18) {
		err = regulator_disable(pdata->pwr18);
		if (err) {
			dev_err(dev, "Failed to disable VDD18: %d", err);
			return err;
		}
	}

	if (pdata->ovdd) {
		err = regulator_disable(pdata->ovdd);
		if (err)
			return err;
	}

	it6162->supplies_on = false;

	return 0;
}

/*
 * Runs on every unwind of a failed probe as well as on unbind, so that the
 * supplies are never still enabled when devm releases them - the probe error
 * paths before it6162_poweron() return directly and would otherwise leak an
 * enable_count, which _regulator_put() reports as a WARN.
 */
static void it6162_clear_power_action(void *data)
{
	it6162_platform_clear_power(data);
}

static int it6162_poweron(struct it6162 *it6162)
{
	struct device *dev = it6162->dev;
	int err;

	if (it6162->powered) {
		dev_dbg(dev, "Already powered on");
		return 0;
	}

	err = it6162_platform_set_power(it6162);
	if (err)
		return err;

	/* Wait for info block ready after power-on-reset */
	if (!it6162_wait_devices(it6162))
		return -ENODEV;

	it6162->powered = true;
	it6162->connector_status = connector_status_disconnected;
	it6162_reset_init(it6162);

	if (it6162->it6162_i2c->irq)
		enable_irq(it6162->it6162_i2c->irq);

	dev_dbg(dev, "it6162 poweron end");
	return 0;
}

static int it6162_poweroff(struct it6162 *it6162)
{
	struct device *dev = it6162->dev;
	int err;

	if (!it6162->powered) {
		dev_dbg(dev, "Already powered off");
		return 0;
	}

	err = it6162_platform_clear_power(it6162);

	if (err < 0)
		return err;

	it6162->powered = false;
	dev_dbg(dev, "it6162 poweroff");
	return 0;
}

static void it6162_config_default(struct it6162 *it6162)
{
	struct it6162_mipirx_config *mipirx_cfg = &it6162->mipirx_config;
	struct it6162_tx_out_set *tx_out = &it6162->tx_out_set;
	struct it6162_audio *audio_config = &it6162->audio_config;

	mipirx_cfg->lane_num = 4;
	mipirx_cfg->pn_swap = false;
	mipirx_cfg->lane_swap = false;
	mipirx_cfg->en_port0 = false;
	mipirx_cfg->en_port1 = false;
	mipirx_cfg->continuous_clk = true;
	mipirx_cfg->mode = SYNC_EVENT;
	mipirx_cfg->format = MIPI_DSI_FMT_RGB888;
	mipirx_cfg->mode_flags = MIPI_DSI_MODE_VIDEO;

	tx_out->hdcp_version = HDCP_23;
	tx_out->hdcp_encyption = true;

	tx_out->stream_ID = 0;

	audio_config->select = AUD_OUT_SEL_I2S;
	audio_config->sample_rate = SAMPLE_RATE_48K;
	audio_config->type = AUD_TYPE_LPCM;
	audio_config->word_length = WORD_LENGTH_16BIT;
	audio_config->channel_number = 2;

	it6162->connector_status = connector_status_disconnected;
}

static enum drm_connector_status it6162_detect(struct it6162 *it6162)
{
	enum drm_connector_status status;
	int tx_status;

	if (!it6162->powered)
		return connector_status_disconnected;

	/*
	 * Read HPD from the chip rather than the cached connector_status, which
	 * only it6162_interrupt_handler() refreshes.  Without INTP that handler
	 * never runs and the value latched at probe would stand for ever.
	 * OFFSET_TX_STATUS is a plain regmap read, needing no completion.
	 */
	tx_status = it6162_infoblock_read(it6162, OFFSET_TX_STATUS);
	if (tx_status < 0)
		return it6162->connector_status;

	status = GET_TX_HPD_STATUS(tx_status) ? connector_status_connected :
						connector_status_disconnected;

	if (it6162->connector_status != status) {
		dev_dbg(it6162->dev, "connector_status %x -> %x (tx_status 0x%02x)",
			it6162->connector_status, status, tx_status);
		it6162->connector_status = status;

		/* Same housekeeping the interrupt path does on unplug. */
		if (status == connector_status_disconnected)
			it6162_remove_edid_cache(it6162);
	}

	return status;
}

static void it6162_show_drm_video_mode(struct it6162 *it6162,
					const struct videomode *vm)
{
	struct device *dev = it6162->dev;

	dev_dbg(dev, "HActive = %u", vm->hactive);
	dev_dbg(dev, "VActive = %u", vm->vactive);
	dev_dbg(dev, "HTotal  = %u",
		vm->hactive + vm->hfront_porch + vm->hsync_len +
		vm->hback_porch);
	dev_dbg(dev, "VTotal  = %u",
		vm->vactive + vm->vfront_porch + vm->vsync_len +
		vm->vback_porch);
	dev_dbg(dev, "PCLK    = %lukhz", vm->pixelclock / 1000);
	dev_dbg(dev, "HFP     = %u", vm->hfront_porch);
	dev_dbg(dev, "HSW     = %u", vm->hsync_len);
	dev_dbg(dev, "HBP     = %u", vm->hback_porch);
	dev_dbg(dev, "VFP     = %u", vm->vfront_porch);
	dev_dbg(dev, "VSW     = %u", vm->vsync_len);
	dev_dbg(dev, "VBP     = %u", vm->vback_porch);
	if (vm->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		dev_dbg(dev, "HPOL +");
	else
		dev_dbg(dev, "HPOL -");

	if (vm->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		dev_dbg(dev, "VPOL +");
	else
		dev_dbg(dev, "VPOL -");

	if (vm->flags & DISPLAY_FLAGS_INTERLACED)
		dev_dbg(dev, "Intelaced");
	else
		dev_dbg(dev, "Progressive");
}

static enum drm_mode_status it6162_mode_valid(struct it6162 *it6162,
				const struct drm_display_mode *mode)
{
	if (mode->clock > 300000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static int it6162_get_edid_block(void *data, u8 *buf, unsigned int block,
				  size_t len)
{
	struct it6162 *it6162 = data;
	unsigned int cnt;
	unsigned int i;
	u8 config;
	int ret = 0;

	if (len > EDID_LENGTH)
		return -EINVAL;

	guard(mutex)(&it6162->lock);

	cnt = 0;

	for (i = 0; i < EDID_LENGTH; i += DATA_BUFFER_DEPTH, cnt++) {
		config = (block << 2) | (cnt);
		it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, config);
		if (it6162_infoblock_get_data_short(it6162, HOST_SETTING_EDID_R, buf + i)) {
			ret = -EIO;
			break;
		}
	}

	return ret;
}

static void it6162_set_capability_from_edid_parse
		(struct it6162 *it6162, const struct edid *edid)
{
	struct device *dev = it6162->dev;

	it6162->is_hdmi = drm_detect_hdmi_monitor(edid);
	it6162->en_audio = drm_detect_monitor_audio(edid);

	dev_dbg(dev, "%s mode, monitor %ssupport audio",
		it6162->is_hdmi ? "HDMI" : "DVI",
		it6162->en_audio ? "" : "not ");
}

static void it6162_enable_audio(struct it6162 *it6162)
{
	struct it6162_audio *config = &it6162->audio_config;

	guard(mutex)(&it6162->lock);
	it6162_infoblock_write(it6162, 0x3D, config->sample_rate);
	it6162_infoblock_write(it6162, 0x3C, (config->channel_number) |
					     (config->select << 4) |
					     (config->type << 6));
	it6162_infoblock_host_set(it6162, HOST_SETTING_AUDIO_INFO);
}

static void it6162_disable_audio(struct it6162 *it6162)
{
	guard(mutex)(&it6162->lock);
	it6162_infoblock_write(it6162, 0x3C, 0x00);
	it6162_infoblock_host_set(it6162, HOST_SETTING_AUDIO_INFO);
}

static irqreturn_t it6162_int_threaded_handler(int unused, void *data)
{
	struct it6162 *it6162 = data;

	it6162_interrupt_handler(it6162);

	return IRQ_HANDLED;
}

static void it6162_audio_update_connector_status(struct it6162 *it6162)
{
	enum drm_connector_status status;

	status = it6162_detect(it6162);
	if (it6162->plugged_cb && it6162->codec_dev) {
		it6162->plugged_cb(it6162->codec_dev,
				    status == connector_status_connected);
	}
}

static int it6162_audio_update_hw_params(struct it6162 *it6162,
					  struct hdmi_codec_daifmt *fmt,
					  struct hdmi_codec_params *hparms)
{
	struct it6162_audio *config = &it6162->audio_config;

	hdmi_audio_infoframe_pack(&hparms->cea, &config->infoframe,
				  sizeof(config->infoframe));

	memcpy(config->channel_status, &hparms->iec.status[0],
	       AES_IEC958_STATUS_SIZE);

	config->channel_number = hparms->channels;

	switch (hparms->sample_rate) {
	case 32000:
		config->sample_rate = SAMPLE_RATE_32K;
		break;
	case 44100:
		config->sample_rate = SAMPLE_RATE_44_1K;
		break;
	case 48000:
		config->sample_rate = SAMPLE_RATE_48K;
		break;
	case 88200:
		config->sample_rate = SAMPLE_RATE_88_2K;
		break;
	case 96000:
		config->sample_rate = SAMPLE_RATE_96K;
		break;
	case 176400:
		config->sample_rate = SAMPLE_RATE_176_4K;
		break;
	case 192000:
		config->sample_rate = SAMPLE_RATE_192K;
		break;
	default:
		return -EINVAL;
	}

	switch (hparms->sample_width) {
	case 16:
		config->sample_width = WORD_LENGTH_16BIT;
		break;
	case 24:
		config->sample_width = WORD_LENGTH_18BIT;
		break;
	case 18:
		config->sample_width = WORD_LENGTH_20BIT;
		break;
	case 20:
		config->sample_width = WORD_LENGTH_24BIT;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt->fmt) {
	case HDMI_I2S:
		config->select = AUD_OUT_SEL_I2S;
		break;
	case HDMI_SPDIF:
		config->select = AUD_OUT_SEL_SPDIF;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int it6162_hdmi_hw_params(struct device *dev, void *data,
				  struct hdmi_codec_daifmt *fmt,
				  struct hdmi_codec_params *hparms)
{
	struct it6162 *it6162 = dev_get_drvdata(dev);

	switch (hparms->sample_rate) {
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
	case 768000:
		break;
	default:
		return -EINVAL;
	}

	switch (hparms->sample_width) {
	case 16:
	case 24:
	case 18:
	case 20:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt->fmt) {
	case HDMI_I2S:
	case HDMI_SPDIF:
		break;
	default:
		return -EINVAL;
	}

	it6162_audio_update_hw_params(it6162, fmt, hparms);

	return 0;
}

static int it6162_hdmi_audio_startup(struct device *dev, void *data)
{
	struct it6162 *it6162 = dev_get_drvdata(dev);

	it6162_enable_audio(it6162);

	return 0;
}

static void it6162_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct it6162 *it6162 = dev_get_drvdata(dev);

	it6162_disable_audio(it6162);
}

static int it6162_hdmi_audio_hook_plugged_cb(struct device *dev,
					      void *data,
					      hdmi_codec_plugged_cb fn,
					      struct device *codec_dev)
{
	struct it6162 *it6162 = data;

	it6162->plugged_cb = fn;
	it6162->codec_dev = codec_dev;
	it6162_audio_update_connector_status(it6162);

	return 0;
}

static const struct hdmi_codec_ops it6162_audio_codec_ops = {
	.hw_params = it6162_hdmi_hw_params,
	.audio_shutdown = it6162_hdmi_audio_shutdown,
	.audio_startup = it6162_hdmi_audio_startup,
	.hook_plugged_cb = it6162_hdmi_audio_hook_plugged_cb,
};

static int it6162_hdmi_register_audio_driver(struct device *dev)
{
	struct it6162 *it6162 = dev_get_drvdata(dev);
	struct hdmi_codec_pdata codec_data = {
		.ops = &it6162_audio_codec_ops,
		.max_i2s_channels = 8,
		.i2s = 1,
		.data = it6162,
	};

	it6162->audio_pdev = platform_device_register_data(dev,
							    HDMI_CODEC_DRV_NAME,
							    PLATFORM_DEVID_AUTO,
							    &codec_data,
							    sizeof(codec_data));

	return PTR_ERR_OR_ZERO(it6162->audio_pdev);
}

static void it6162_hdmi_audio_dev_exit(struct it6162 *it6162)
{
	if (it6162->audio_pdev) {
		platform_device_unregister(it6162->audio_pdev);
		it6162->audio_pdev = NULL;
	}
}

static void it6162_hdcp_work(struct work_struct *work)
{
	struct it6162 *it6162 = container_of(work, struct it6162, hdcp_work);
	u8 buf[DATA_BUFFER_DEPTH];
	int i, dev_count;

	guard(mutex)(&it6162->lock);
	/*read Bstatus & BKSV*/
	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, 0x00);
	if (it6162_infoblock_get_data(it6162, HOST_SETTING_HDCP_R, buf)) {
		dev_err(it6162->dev, "read Bstatus & BKSV fail!!!!");
		return;
	}

	dev_dbg(it6162->dev, "Bstatus 0x%02x%02x", buf[1], buf[0]);
	dev_dbg(it6162->dev, "BKsv %02X %02X %02X %02X %02X",
		 buf[2], buf[3], buf[4], buf[5], buf[6]);

	if (it6162->tx_hdcp_ver == HDCP_14)
		dev_count = buf[0] & 0x7F;
	else
		dev_count = (buf[1] & 0x01 << 8) | (buf[0] & 0xF0);
	if (dev_count > 31) {
		dev_err(it6162->dev, "dev_count %x over 30", dev_count);
		dev_count = 30;
	}

	if (!dev_count)
		return;

	for (i = 0; i < (dev_count / 6 + 1); i++) {
		/* KsV lists */
		it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, i + 1);
		if (it6162_infoblock_get_data(it6162, HOST_SETTING_HDCP_R, buf)) {
			dev_dbg(it6162->dev, "ksvlist %x fail", i);
			return;
		}

		return;
	}
}

static int it6162_regmap_init(struct i2c_client *client,
			       struct it6162 *it6162)
{
	it6162->it6162_i2c = client;

	it6162->it6162_regmap = devm_regmap_init_i2c(it6162->it6162_i2c,
						       &it6162_regmap_config);
	if (IS_ERR(it6162->it6162_regmap))
		return PTR_ERR(it6162->it6162_regmap);

	return 0;
}

static int it6162_of_get_dsi_host(struct it6162 *it6162,
				struct mipi_dsi_host **host)
{
	struct device *dev = it6162->dev;
	struct device_node *host_node;
	struct device_node *endpoint;
	int port, node, ready;

	node = 0;
	ready = 0;
	for (port = 0; port < 2; port++) {
		endpoint = of_graph_get_endpoint_by_regs(dev->of_node, port, -1);
		if (!endpoint)
			continue;
		dev_dbg(dev, "find endpoint[%d]", port);
		host_node = of_graph_get_remote_port_parent(endpoint);
		of_node_put(endpoint);

		if (!host_node)
			continue;
		dev_dbg(dev, "found remote endpoint[%d]", port);
		node++;
		host[port] = of_find_mipi_dsi_host_by_node(host_node);
		of_node_put(host_node);
		if (host[port])
			ready++;
	}

	dev_dbg(dev, "%s: %d node(s), %d ready", __func__, node, ready);
	if (node == 0)
		return -ENODEV;

	if (ready == 0)
		return -EPROBE_DEFER;

	return 0;
}

static int it6162_attach_dsi(struct it6162 *it6162,
			      struct mipi_dsi_host *host)
{
	struct device *dev = it6162->dev;
	const struct mipi_dsi_device_info info = {"it6162",
						  0,
						  dev->of_node};
	struct mipi_dsi_device *dsi;
	int ret = 0;

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		/*
		 * Report the real errno: -EEXIST here means another DSI device
		 * already occupies this virtual channel on the same host, which
		 * is indistinguishable from "no device" if it is folded into
		 * -ENODEV.
		 */
		ret = PTR_ERR(dsi);
		dev_err(dev, "failed to create dsi device: %d", ret);
		return ret;
	}

	it6162->dsi = dsi;
	dsi->lanes = 4;
	dsi->format = it6162->mipirx_config.format;
	dsi->mode_flags = it6162->mipirx_config.mode_flags;
	ret = devm_mipi_dsi_attach(dev, dsi);

	return ret;
}

static void it6162_load_mipi_pars(struct it6162 *it6162,
				  struct device_node *endpoint)
{
	struct device *dev = it6162->dev;
	struct it6162_mipirx_config *mipirx = &it6162->mipirx_config;
	int dsi_lanes;

	dsi_lanes = drm_of_get_data_lanes_count(endpoint, 1, 4);

	if (dsi_lanes < 0)
		mipirx->lane_num = 4;
	else
		mipirx->lane_num = dsi_lanes;

	mipirx->pn_swap = of_property_present(endpoint,
				"ite,mipi-dsi-phy-pn-swap");
	mipirx->lane_swap = of_property_present(endpoint,
				"ite,mipi-dsi-phy-link-swap");

	if (of_property_present(endpoint, "ite,mipi-dsi-mode-video-sync-pulse")) {
		mipirx->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
		mipirx->mode = SYNC_PULSE;
	}

	if (of_property_present(endpoint, "ite,mipi-dsi-clock-non-continous")) {
		mipirx->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;
		mipirx->continuous_clk = false;
	}

	dev_dbg(dev, "lanes: %d pn_swap: %d, lane_swap: %d, mode_flags: %lu",
		 mipirx->lane_num, mipirx->pn_swap,
		 mipirx->lane_swap, mipirx->mode_flags);
}

static unsigned int it6162_parse_dt(struct it6162 *it6162)
{
	struct device *dev = it6162->dev;
	struct device_node *np = dev->of_node;
	struct it6162_mipirx_config *mipirx = &it6162->mipirx_config;
	struct device_node *endpoint;

	if (!np)
		return -EINVAL;

	endpoint = of_graph_get_endpoint_by_regs(np, 0, -1);
	if (endpoint) {
		mipirx->en_port0 = true;
		it6162_load_mipi_pars(it6162, endpoint);
		of_node_put(endpoint);
	}

	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	if (endpoint) {
		mipirx->en_port1 = true;

		if (!mipirx->en_port0)
			it6162_load_mipi_pars(it6162, endpoint);

		of_node_put(endpoint);
	}
	return 0;
}

static int it6162_init_pdata(struct it6162 *it6162)
{
	struct it6162_platform_data *pdata = &it6162->pdata;
	struct device *dev = it6162->dev;
	int ret;

	/*
	 * All of these are genuinely optional: boards that leave a rail
	 * always-on, or that tie the reset pin to a local power-on-reset
	 * network, describe nothing for it.  -EPROBE_DEFER must still be
	 * propagated though, otherwise a supply that is described but not yet
	 * registered is silently dropped and the chip is probed unpowered.
	 */
	pdata->ivdd = devm_regulator_get_optional(dev, "ivdd");
	if (IS_ERR(pdata->ivdd)) {
		ret = PTR_ERR(pdata->ivdd);
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_dbg(dev, "no ivdd regulator: %d", ret);
		pdata->ivdd = NULL;
	}

	pdata->pwr18 = devm_regulator_get_optional(dev, "pwr18");
	if (IS_ERR(pdata->pwr18)) {
		ret = PTR_ERR(pdata->pwr18);
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_dbg(dev, "no pwr18 regulator: %d", ret);
		pdata->pwr18 = NULL;
	}

	pdata->ovdd = devm_regulator_get_optional(dev, "ovdd");
	if (IS_ERR(pdata->ovdd)) {
		ret = PTR_ERR(pdata->ovdd);
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_dbg(dev, "no ovdd regulator: %d", ret);
		pdata->ovdd = NULL;
	}

	pdata->gpiod_reset = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata->gpiod_reset)) {
		ret = PTR_ERR(pdata->gpiod_reset);
		if (ret == -EPROBE_DEFER)
			return ret;
		dev_dbg(dev, "no reset gpio: %d", ret);
		pdata->gpiod_reset = NULL;
	}

	return 0;
}

static int it6162_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);
	struct device *dev = it6162->dev;
	struct drm_device *drm = bridge->dev;

	it6162->drm = drm;

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(dev,
			"it6162 driver only copes with atomic updates");
		return -EOPNOTSUPP;
	}

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		struct drm_connector *connector;
		int ret;

		connector = drm_bridge_connector_init(drm, encoder);
		if (IS_ERR(connector)) {
			dev_err(dev, "Unable to create bridge connector\n");
			return PTR_ERR(connector);
		}

		ret = drm_connector_attach_encoder(connector, encoder);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static enum drm_mode_status
	it6162_bridge_mode_valid(struct drm_bridge *bridge,
				  const struct drm_display_info *info,
				  const struct drm_display_mode *mode)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	return it6162_mode_valid(it6162, mode);
}

static enum drm_connector_status
	it6162_bridge_detect(struct drm_bridge *bridge,
			      struct drm_connector *connector)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	return it6162_detect(it6162);
}

static void it6162_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	it6162->bridge_hpd_enable = true;
}

static void it6162_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	it6162->bridge_hpd_enable = false;
}

static void it6162_notify_hpd(struct it6162 *it6162)
{
	enum drm_connector_status status =  connector_status_disconnected;

	if (!it6162->bridge_hpd_enable)
		return;

	status =  it6162->connector_status;
	dev_dbg(it6162->dev, "hpd_notify hpd irq %d", status);
	drm_bridge_hpd_notify(&it6162->bridge, status);
}

static void it6162_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_commit *state)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);
	struct device *dev = it6162->dev;
	struct drm_crtc_state *crtc_state;
	struct drm_connector_state *conn_state;
	struct drm_display_mode *mode;
	struct drm_connector *connector;
	int ret;

	it6162->bridge_enable = true;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);

	if (!connector)
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;
	if (WARN_ON(!mode))
		return;

	if (it6162->is_hdmi) {
		ret = drm_hdmi_avi_infoframe_from_display_mode(&it6162->avi_info,
							       connector,
							       mode);
		if (ret)
			dev_err(dev, "Failed to setup AVI infoframe: %d", ret);
	}

	it6162_mipi_set_d2v_video_timing(it6162);
	it6162_tx_enable(it6162);

	if (it6162->en_audio)
		it6162_enable_audio(it6162);
	else
		it6162_disable_audio(it6162);
}

static void it6162_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_atomic_commit *state)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	it6162->bridge_enable = false;
	it6162_tx_disable(it6162);
}

static const struct drm_edid *it6162_bridge_edid_read(struct drm_bridge *bridge,
						      struct drm_connector *connector)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);
	struct device *dev = it6162->dev;

	if (!it6162->cache_edid) {
		const struct drm_edid *drm_edid;

		drm_edid = drm_edid_read_custom(connector,
						it6162_get_edid_block, it6162);
		if (!drm_edid) {
			dev_err(dev, "failed to read EDID");
			return NULL;
		}

		it6162->cache_edid = drm_edid;
	} else {
		dev_dbg(dev, "use cached EDID");
	}

	it6162_set_capability_from_edid_parse(it6162,
					      drm_edid_raw(it6162->cache_edid));
	cec_s_phys_addr_from_edid(it6162->cec_adap,
				  drm_edid_raw(it6162->cache_edid));

	/* The caller takes ownership of the returned EDID. */
	return drm_edid_dup(it6162->cache_edid);
}

static void it6162_bridge_mode_set(struct drm_bridge *bridge,
				    const struct drm_display_mode *mode,
				    const struct drm_display_mode *adj)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);

	drm_display_mode_to_videomode(adj, &it6162->vm);
}

/* CEA-861 mode 16, used when the sink's EDID cannot be read. */
static const struct drm_display_mode it6162_fallback_mode = {
	DRM_MODE("1920x1080", DRM_MODE_TYPE_DRIVER, 148500,
		 1920, 2008, 2052, 2200, 0,
		 1080, 1084, 1089, 1125, 0,
		 DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC),
};

/*
 * Supply modes ourselves: drm_bridge_connector_get_modes_edid() returns none at
 * all when the EDID read fails, leaving nothing settable.  EDID is still tried
 * first, so a sink that answers is used normally.
 */
static int it6162_bridge_get_modes(struct drm_bridge *bridge,
				   struct drm_connector *connector)
{
	struct it6162 *it6162 = bridge_to_it6162(bridge);
	const struct drm_edid *drm_edid;
	struct drm_display_mode *mode;
	int n = 0;

	drm_edid = it6162_bridge_edid_read(bridge, connector);
	if (drm_edid_valid(drm_edid)) {
		drm_edid_connector_update(connector, drm_edid);
		n = drm_edid_connector_add_modes(connector);
	}
	drm_edid_free(drm_edid);

	if (n > 0)
		return n;

	drm_edid_connector_update(connector, NULL);

	mode = drm_mode_duplicate(connector->dev, &it6162_fallback_mode);
	if (!mode)
		return 0;

	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	dev_info(it6162->dev, "no usable EDID, offering %s only\n",
		 it6162_fallback_mode.name);

	return 1;
}

static const struct drm_bridge_funcs it6162_bridge_funcs = {
	.attach = it6162_bridge_attach,
	.mode_valid = it6162_bridge_mode_valid,
	.detect = it6162_bridge_detect,
	.hpd_enable = it6162_bridge_hpd_enable,
	.hpd_disable = it6162_bridge_hpd_disable,

	.atomic_enable = it6162_bridge_atomic_enable,
	.atomic_disable = it6162_bridge_atomic_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_create_state = drm_atomic_helper_bridge_create_state,

	.edid_read = it6162_bridge_edid_read,
	.get_modes = it6162_bridge_get_modes,
	.mode_set = it6162_bridge_mode_set,
};

static int it6162_ddc_xfer_write(struct it6162 *it6162,
				 u8 addr, u8 *buf, unsigned int num)
{
	struct it6162_infoblock_msg msg;
	int i;

	if (num > DATA_BUFFER_DEPTH - 1)
		return -EIO;

	guard(mutex)(&it6162->lock);
	dev_dbg(it6162->dev, "%s %02x", __func__, addr);
	msg.action = INFO_BUFF_REQ_DDC_W;
	msg.len = num + 1;
	msg.msg[0] = addr << 1;
	for (i = 0; i < num; i++) {
		msg.msg[i + 1] = buf[i];
		dev_dbg(it6162->dev, "buf[%d] = %02x", i, buf[i]);
	}

	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, msg.len);
	it6162_infoblock_write_msg_no_wait(it6162, &msg);
	if (it6162_infoblock_wait_complete(it6162) < 0)
		return -EIO;

	return num;
}

static int it6162_ddc_xfer_read(struct it6162 *it6162, u8 addr,
				u8 *buf, unsigned int num)
{
	struct it6162_infoblock_msg msg;

	dev_dbg(it6162->dev, "%s %02x %d", __func__, addr, num);

	if (num > DATA_BUFFER_DEPTH)
		return -EIO;

	guard(mutex)(&it6162->lock);
	msg.action = INFO_BUFF_REQ_DDC_R;
	msg.len = num;
	msg.msg[0] = addr << 1;
	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, msg.len);
	it6162_infoblock_write_msg_no_wait(it6162, &msg);
	if (it6162_infoblock_wait_buffer(it6162) < 0)
		return -EIO;

	if (it6162->data_buf_sts != IT6162_DATA_BUF_STS_READY)
		return -EIO;

	it6162_infoblock_read_bufer(it6162, buf, num);

	return num;
}

static int it6162_ddc_xfer(struct it6162 *it6162, struct i2c_msg *msgs)
{
	int ret;

	dev_dbg(it6162->dev, "%s msgs->flags %X", __func__, msgs->flags);
	if ((msgs->flags & I2C_M_RD) == 0)
		ret = it6162_ddc_xfer_write(it6162, msgs->addr, msgs->buf, msgs->len);
	else
		ret = it6162_ddc_xfer_read(it6162, msgs->addr, msgs->buf, msgs->len);

	return ret;
}

static int it6162_hdmi_i2c_xfer(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num)
{
	struct it6162 *it6162 = i2c_get_adapdata(adap);
	struct device *dev = it6162->dev;
	int i;

	dev_dbg(dev, "%s %d", __func__, num);

	for (i = 0; i < num; i++) {
		if (msgs->addr != 0x50) {
			it6162_ddc_xfer(it6162, &msgs[i]);
		} else {
			/*The DDC does not suooprt read len > 32*/
			/*this patch edid reads to  it6162_get_edid_block*/
			static int offset;

			if ((msgs->flags & I2C_M_RD) == 0) {
				if (msgs->len)
					offset = (int)msgs->buf[0];
			} else {
				it6162_get_edid_block(it6162,
						      msgs->buf,
						      offset / 128,
						      msgs->len);
			}
		}
	}

	return i;
}

static u32 it6162_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hdmi_ddc_algorithm = {
	.xfer		= it6162_hdmi_i2c_xfer,
	.functionality	= it6162_hdmi_i2c_func,
};

static int it6162_add_ddc_i2c_adapter(struct it6162 *it6162)
{
	struct i2c_adapter *adap;
	struct device *dev = it6162->dev;
	int ret;

	adap = devm_kzalloc(dev, sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	adap->owner = THIS_MODULE;
	adap->dev.parent = dev;
	adap->algo = &hdmi_ddc_algorithm;
	strscpy(adap->name, "ITE IT6162 HDMI DDC", sizeof(adap->name));
	i2c_set_adapdata(adap, it6162);

	ret = devm_i2c_add_adapter(dev, adap);
	if (ret) {
		dev_err(dev, "cannot add %s I2C adapter", adap->name);
		return ret;
	}

	it6162->ddc_adap = adap;
	return 0;
}

static int it6162_infoblock_wait_cec(struct it6162 *it6162)
{
	struct device *dev = it6162->dev;
	int status;

	status = wait_event_timeout(it6162->wq,
		!!it6162->cec_ret,
		msecs_to_jiffies(TIMEOUT_INFOBLOCK_MS));

	if (status > 0 || !!it6162->cec_ret)
		return 0;

	dev_err(dev, "%s err status = %d %d", __func__, status, it6162->cec_ret);
	return -ETIMEDOUT;
}

static int it6162_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct it6162 *it6162 = cec_get_drvdata(adap);

	dev_dbg(it6162->dev, "%s, %d", __func__, (int)enable);

	guard(mutex)(&it6162->cec_lock);
	guard(mutex)(&it6162->lock);
	it6162->cec_ret = 0;
	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, enable ? 0x81 : 0x80);
	it6162_infoblock_host_set_no_wait(it6162, INFO_BUFF_REQ_CEC_W);
	it6162_infoblock_wait_cec(it6162);
	dev_dbg(it6162->dev, "%s,it6162->cec_ret = %d", __func__, it6162->cec_ret);
	return 0;
}

static int it6162_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct it6162 *it6162 = cec_get_drvdata(adap);
	struct it6162_infoblock_msg msg;

	guard(mutex)(&it6162->cec_lock);
	guard(mutex)(&it6162->lock);
	it6162->cec_ret = 0;
	msg.action = INFO_BUFF_REQ_CEC_W;
	msg.len = 1;
	msg.msg[0] = addr;
	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, 0x82);
	it6162_infoblock_write_msg(it6162, &msg);
	it6162_infoblock_wait_cec(it6162);
	dev_dbg(it6162->dev, "%s,it6162->cec_ret = %d", __func__, it6162->cec_ret);
	return 0;
}

static int it6162_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				u32 signal_free_time, struct cec_msg *cec_msg)
{
	struct it6162 *it6162 = cec_get_drvdata(adap);

	dev_dbg(it6162->dev, "%s msg->flags %X", __func__, cec_msg->flags);
	guard(mutex)(&it6162->cec_lock);
	memcpy(&it6162->cec_tx_msg, cec_msg, sizeof(struct cec_msg));
	schedule_work(&it6162->cec_tx_work);

	return 0;
}

static void it6162_cec_rx_work(struct work_struct *work)
{
	struct it6162 *it6162 = container_of(work, struct it6162, cec_rx_work);
	struct cec_msg msg = {};
	u8 buf[DATA_BUFFER_DEPTH];
	int i, len;

	guard(mutex)(&it6162->lock);
	len = it6162_infoblock_read(it6162, OFFSET_DATA_TYPE_IDX);
	if (len < 0)
		return;

	msg.len = len;
	if (it6162_infoblock_get_data(it6162, INFO_BUFF_REQ_CEC_R, buf) < 0)
		return;
	if (msg.len > 0x0F) {
		dev_err(it6162->dev, "err cec recive buf.len = %X", msg.len);
		return;
	}

	dev_dbg(it6162->dev, "cec recive buf.len = %X", msg.len);
	for (i = 0; i < msg.len; i++) {
		msg.msg[i] = buf[i + 1];
		dev_dbg(it6162->dev, "cec recive buf[%d] = %X", i, msg.msg[i]);
	}
	cec_received_msg(it6162->cec_adap, &msg);
}

static int it6162_cec_send_msg(struct it6162 *it6162,
			       struct it6162_infoblock_msg *msg)
{
	it6162->cec_ret = 0;
	it6162_infoblock_write(it6162, OFFSET_DATA_TYPE_IDX, msg->len);
	it6162_infoblock_write_msg(it6162, msg);
	if (it6162_infoblock_wait_complete(it6162) < 0)
		return -ETIMEDOUT;
	return 0;
}

static void it6162_cec_tx_work(struct work_struct *work)
{
	struct it6162 *it6162 = container_of(work, struct it6162, cec_tx_work);
	struct it6162_infoblock_msg msg;
	struct cec_msg *cec_msg = &it6162->cec_tx_msg;
	int i;

	guard(mutex)(&it6162->cec_lock);
	guard(mutex)(&it6162->lock);

	msg.action = INFO_BUFF_REQ_CEC_W;
	msg.len = cec_msg->len;

	for (i = 0; i < CEC_MAX_MSG_SIZE && i < msg.len; i++) {
		dev_dbg(it6162->dev, "msg[%d] = %X", i, cec_msg->msg[i]);
		msg.msg[i] = cec_msg->msg[i];
	}
	if (it6162_cec_send_msg(it6162, &msg) < 0)
		it6162->cec_ret = CEC_FAIL;
	else
		it6162_infoblock_wait_cec(it6162);

	dev_dbg(it6162->dev, "cec_ret = %X", it6162->cec_ret);
	switch (it6162->cec_ret) {
	case CEC_ACK:
		cec_transmit_attempt_done(it6162->cec_adap, CEC_TX_STATUS_OK);
		break;
	case CEC_NACK:
		cec_transmit_attempt_done(it6162->cec_adap, CEC_TX_STATUS_NACK);
		break;
	default:
		cec_transmit_attempt_done(it6162->cec_adap, CEC_TX_STATUS_ERROR);
		break;
	}
}

static const struct cec_adap_ops it6162_cec_adap_ops = {
	.adap_enable = it6162_cec_adap_enable,
	.adap_log_addr = it6162_cec_adap_log_addr,
	.adap_transmit = it6162_cec_adap_transmit,
};

static int it6162_create_cec_adapter(struct it6162 *it6162)
{
	struct cec_adapter *adap;
	struct cec_notifier *notify;
	int ret;

	adap = cec_allocate_adapter(&it6162_cec_adap_ops,
				    it6162,
				    "it6162-cec",
				    CEC_CAP_DEFAULTS |
				    CEC_CAP_PHYS_ADDR,
				    MAX_CEC_ADDR);
	if (IS_ERR(adap))
		return PTR_ERR(adap);

	mutex_init(&it6162->cec_lock);
	INIT_WORK(&it6162->cec_rx_work, it6162_cec_rx_work);
	INIT_WORK(&it6162->cec_tx_work, it6162_cec_tx_work);

	notify = cec_notifier_cec_adap_register(it6162->dev, NULL, adap);
	if (!notify) {
		ret = -ENOMEM;
		goto err_cec_delete_adapter;
	}

	ret = cec_register_adapter(adap, it6162->dev);
	if (ret < 0)
		goto err_cec_notifier_nregister;

	it6162->cec_adap = adap;
	it6162->cec_notify = notify;

	return 0;

err_cec_notifier_nregister:
	cec_notifier_cec_adap_unregister(notify, adap);
err_cec_delete_adapter:
	mutex_destroy(&it6162->cec_lock);
	cec_delete_adapter(adap);

	return ret;
}

static void it6162_remove_cec_adapter(struct it6162 *it6162)
{
	if (!it6162->cec_adap)
		return;
	cancel_work_sync(&it6162->cec_rx_work);
	cancel_work_sync(&it6162->cec_tx_work);
	cec_notifier_cec_adap_unregister(it6162->cec_notify, it6162->cec_adap);
	cec_delete_adapter(it6162->cec_adap);
	mutex_destroy(&it6162->cec_lock);
}

static int it6162_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct mipi_dsi_host *host[2] = { NULL, NULL };
	struct it6162 *it6162;
	int i, ret;

	it6162 = devm_drm_bridge_alloc(dev, struct it6162, bridge,
				       &it6162_bridge_funcs);
	if (IS_ERR(it6162))
		return PTR_ERR(it6162);

	it6162->dev = dev;

	ret = it6162_regmap_init(client, it6162);
	if (ret != 0)
		return ret;

	i2c_set_clientdata(client, it6162);

	init_waitqueue_head(&it6162->wq);
	init_completion(&it6162->cmd_complete);
	INIT_WORK(&it6162->hdcp_work, it6162_hdcp_work);
	mutex_init(&it6162->lock);

	ret = it6162_init_pdata(it6162);
	if (ret) {
		dev_err(dev, "Failed to initialize pdata: %d", ret);
		return ret;
	}

	ret = it6162_of_get_dsi_host(it6162, host);
	if (ret < 0) {
		dev_err(dev, "Failed to get DSI host: %d", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, it6162_clear_power_action, it6162);
	if (ret)
		return ret;

	it6162_config_default(it6162);
	it6162_parse_dt(it6162);

	/*
	 * INTP is optional: not every board wires it to an interrupt-capable
	 * pad, and the chip is driven over I2C regardless.  Without it the
	 * connector is polled rather than HPD-driven.
	 */
	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
						it6162_int_threaded_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT |
						IRQF_NO_AUTOEN,
						"it6162-intp", it6162);
		if (ret) {
			dev_err(dev, "Failed to request INTP threaded IRQ: %d",
				ret);
			return ret;
		}
	} else {
		dev_info(dev, "No INTP interrupt, polling for hotplug");
	}

	ret = it6162_hdmi_register_audio_driver(dev);
	if (ret < 0) {
		dev_err(dev,
			"Failed to register audio driver: %d", ret);
		return ret;
	}

	it6162_create_cec_adapter(it6162);
	it6162_add_ddc_i2c_adapter(it6162);

	it6162->bridge.of_node = np;
	/* _MODES, not _EDID: it6162_bridge_get_modes() reads the EDID itself. */
	it6162->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_MODES;
	/*
	 * Only claim HPD if the INTP interrupt was requested above: with
	 * DRM_BRIDGE_OP_HPD set, drm_bridge_connector leaves the connector
	 * unpolled and waits for drm_bridge_hpd_notify(), which only ever runs
	 * from the interrupt handler.
	 */
	if (client->irq)
		it6162->bridge.ops |= DRM_BRIDGE_OP_HPD;
	it6162->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	it6162->bridge.vendor = "ITE";
	it6162->bridge.product = "IT6162";
	it6162->bridge.ddc = it6162->ddc_adap;

	/*
	 * Attaching the DSI device below runs the whole DRM bind chain
	 * synchronously, so bring the chip up first.
	 */
	ret = it6162_poweron(it6162);
	if (ret) {
		dev_err(dev, "Failed to power on: %d", ret);
		goto err_cec;
	}

	/*
	 * The bridge has to be registered before the DSI device is attached:
	 * the DSI host resolves its next bridge from the OF graph inside its
	 * host_attach() callback.
	 */
	ret = devm_drm_bridge_add(dev, &it6162->bridge);
	if (ret) {
		dev_err(dev, "Failed to add bridge: %d", ret);
		goto err_poweroff;
	}

	for (i = 0; i < 2; i++) {
		if (!host[i])
			continue;

		ret = it6162_attach_dsi(it6162, host[i]);
		if (ret < 0) {
			dev_err(dev, "Failed attach_dsi[%d] %d", i, ret);
			goto err_poweroff;
		}
	}

	return 0;

err_poweroff:
	if (client->irq)
		disable_irq(client->irq);
	it6162_poweroff(it6162);
err_cec:
	it6162_remove_cec_adapter(it6162);
	it6162_hdmi_audio_dev_exit(it6162);
	return ret;
}

static void it6162_remove(struct i2c_client *client)
{
	struct it6162 *it6162 = i2c_get_clientdata(client);

	if (client->irq)
		disable_irq(client->irq);
	cancel_work_sync(&it6162->hdcp_work);
	it6162_hdmi_audio_dev_exit(it6162);
	it6162_remove_cec_adapter(it6162);
	it6162_remove_edid_cache(it6162);
	it6162_poweroff(it6162);
	mutex_destroy(&it6162->lock);
}

static const struct of_device_id it6162_dt_ids[] = {
	{ .compatible = "ite,it6162" },
	{ /* sentinel */}
};
MODULE_DEVICE_TABLE(of, it6162_dt_ids);

static const struct i2c_device_id it6162_i2c_ids[] = {
	{ "it6162" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(i2c, it6162_i2c_ids);

static struct i2c_driver it6162_driver = {
	.driver = {
		.name = "it6162",
		.of_match_table = it6162_dt_ids,
	},
	.probe = it6162_probe,
	.remove = it6162_remove,
	.id_table = it6162_i2c_ids,
};
module_i2c_driver(it6162_driver);

MODULE_AUTHOR("Pet Weng <pet.weng@ite.com.tw>");
MODULE_AUTHOR("Hermes Wu <Hermes.Wu@ite.com.tw>");
MODULE_DESCRIPTION("ITE IT6162 MIPI-DSI to HDMI bridge driver");
MODULE_LICENSE("GPL");
