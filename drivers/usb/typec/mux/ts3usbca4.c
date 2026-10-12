// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments TS3USBCA4 Type-C SBU Multiplexer driver
 *
 * The TS3USBCA4 is a 3:1 (TS3USBCA410) or 4:1 (TS3USBCA420) passive mux
 * for the SBU1/SBU2 pins of a USB Type-C connector. It routes those pins
 * to one of: DisplayPort AUX (LnA), analog audio MIC/AGND (MIC_GND/Ln1),
 * generic differential I/O (LnB), or a fourth channel (LnC, 420 only).
 *
 * Orientation (cable flip) and signal selection are controlled either
 * on a I2C bus or via an array of GPIOs.
 *
 * This driver uses the I2C interface, 8-bit addressing.
 *
 * Copyright (C) 2026 Collabora Ltd.
 *                    Julien Massot <julien.massot@collabora.com>
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>

/* Register offsets */
#define TS3USBCA4_REG_REVISION		0x09

#define TS3USBCA4_REG_GENERAL_1		0x0a
#  define TS3USBCA4_FLIPSEL		BIT(0)
#  define TS3USBCA4_DEVICE_ENABLE	BIT(1)

#define TS3USBCA4_REG_GENERAL_2		0x0b
#  define TS3USBCA4_SWSEL_MASK		GENMASK(1, 0)
#    define TS3USBCA4_SWSEL_LNB		0
#    define TS3USBCA4_SWSEL_MICGND	1
#    define TS3USBCA4_SWSEL_LNC		2
#    define TS3USBCA4_SWSEL_LNA		3

/**
 * struct ts3usbca4 - Main driver structure
 * @regmap:      Pointer to I2C regmap
 * @mux:         Pointer to Type-C Mux
 * @sw:          Pointer to Type-C Switch
 * @enable_gpio: OEn (Output Enable) GPIO - Active Low
 * @vcc:         Regulator providing power to the VCC pin
 * @lock:        Protects concurrent access for mux and switch
 * @lane_sel:    Tracks the current lane selection
 */
struct ts3usbca4 {
	struct regmap		*regmap;
	struct typec_mux_dev	*mux;
	struct typec_switch_dev	*sw;
	struct gpio_desc	*enable_gpio;
	struct regulator	*vcc;
	struct mutex		lock;
	u8			lane_sel;
};

static const struct regmap_config ts3usbca4_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TS3USBCA4_REG_GENERAL_2,
	.disable_locking = true,
};

static int ts3usbca4_sw_set(struct typec_switch_dev *sw,
			    enum typec_orientation orientation)
{
	struct ts3usbca4 *ts = typec_switch_get_drvdata(sw);
	u8 mask_g1 = TS3USBCA4_FLIPSEL;
	u8 val_g1;
	int ret;

	mutex_lock(&ts->lock);

	switch (orientation) {
	case TYPEC_ORIENTATION_NONE:
		/* Disable: clear both DEVICE_ENABLE and FLIPSEL */
		val_g1 = 0;
		mask_g1 |= TS3USBCA4_DEVICE_ENABLE;
		break;
	case TYPEC_ORIENTATION_NORMAL:
		/* Normal polarity */
		val_g1 = 0;
		break;
	case TYPEC_ORIENTATION_REVERSE:
		/* Flip polarity */
		val_g1 = TS3USBCA4_FLIPSEL;
		break;
	default:
		ret = 0;
		goto out;
	}

	ret = regmap_update_bits(ts->regmap, TS3USBCA4_REG_GENERAL_1,
				 mask_g1, val_g1);
	if (ret)
		goto out;

	/* Route SBU to LnA (DisplayPort AUX) on any active orientation */
	ret = regmap_update_bits(ts->regmap, TS3USBCA4_REG_GENERAL_2,
				 TS3USBCA4_SWSEL_MASK, ts->lane_sel);
out:
	mutex_unlock(&ts->lock);
	return ret;
}


static int ts3usbca4_mux_set(struct typec_mux_dev *mux,
            		     struct typec_mux_state *state)
{
	struct ts3usbca4 *ts = typec_mux_get_drvdata(mux);
	u32 enable;
	int ret;

	if (state->mode >= TYPEC_STATE_MODAL &&
	    state->alt->svid != USB_TYPEC_DP_SID)
		return -EINVAL;

	mutex_lock(&ts->lock);

	switch (state->mode) {
	case TYPEC_STATE_SAFE:
	case TYPEC_STATE_USB:
		/* Disable the mux on SAFE or USB and park SBU to LnB */
		enable = 0;
		ts->lane_sel = TS3USBCA4_SWSEL_LNB;
		break;
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_D:
	case TYPEC_DP_STATE_E:
		/* Enable the mux and route SBU to LnA for DisplayPort AUX */
		enable = TS3USBCA4_DEVICE_ENABLE;
		ts->lane_sel = TS3USBCA4_SWSEL_LNA;
		break;
	default:
		ret = 0;
		goto out;
	}

	ret = regmap_update_bits(ts->regmap, TS3USBCA4_REG_GENERAL_2,
				 TS3USBCA4_SWSEL_MASK, ts->lane_sel);
	if (ret)
		goto out;

	ret = regmap_update_bits(ts->regmap, TS3USBCA4_REG_GENERAL_1,
				 TS3USBCA4_DEVICE_ENABLE, enable);
out:
	mutex_unlock(&ts->lock);
	return ret;
}

static void ts3usbca4_remove(void *data)
{
	struct ts3usbca4 *ts = data;

	/* Set the OEn line to LOW to avoid power leakage */
	gpiod_set_value_cansleep(ts->enable_gpio, 0);

	/* Turn off regulator, if present, to save power */
	regulator_disable(ts->vcc);

	if (ts->mux)
		typec_mux_unregister(ts->mux);
	if (ts->sw)
		typec_switch_unregister(ts->sw);
}

static int ts3usbca4_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct typec_switch_desc sw_desc = { };
	struct typec_mux_desc mux_desc = { };
	struct ts3usbca4 *ts;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	mutex_init(&ts->lock);

	ts->vcc = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(ts->vcc))
		return dev_err_probe(dev, ret, "Failed to get vcc supply\n");

	ts->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ts->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->enable_gpio),
				     "Unable to acquire enable gpio\n");

	ts->regmap = devm_regmap_init_i2c(client, &ts3usbca4_regmap_config);
	if (IS_ERR(ts->regmap))
		return dev_err_probe(dev, PTR_ERR(ts->regmap), "Failed to init regmap\n");

	/* Make sure that the OEn line is LOW to get out of Standby Mode */
	ret = gpiod_set_value_cansleep(ts->enable_gpio, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Could not set output enable GPIO\n");

	/* Enable the regulator, if present: on error, ignore the GPIO */
	ret = regulator_enable(ts->vcc);
	if (ret)
		return dev_err_probe(dev, ret, "Could not power up VCC!\n");

	ret = devm_add_action_or_reset(dev, ts3usbca4_remove, ts);
	if (ret)
		return ret;

	/*
	 * Start with the switch disabled and LnA pre-set connected, so that if
	 * a cable was plugged in before booting, the lines are ready.
	 *
	 * The TCPM will anyway set the mux and orientation at detection time.
	 */
	ret = regmap_write(ts->regmap, TS3USBCA4_REG_GENERAL_1, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize General_1\n");

	ret = regmap_write(ts->regmap, TS3USBCA4_REG_GENERAL_2, TS3USBCA4_SWSEL_LNA);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize General_2\n");

	ts->lane_sel = TS3USBCA4_SWSEL_LNA;

	if (device_property_read_bool(dev, "orientation-switch")) {
		sw_desc.drvdata = ts;
		sw_desc.fwnode = dev_fwnode(dev);
		sw_desc.set = ts3usbca4_sw_set;

		ts->sw = typec_switch_register(dev, &sw_desc);
		if (IS_ERR(ts->sw))
			return dev_err_probe(dev, PTR_ERR(ts->sw),
					             "Failed to register typec switch\n");
	}

	if (device_property_read_bool(dev, "mode-switch")) {
		mux_desc.drvdata = ts;
		mux_desc.fwnode = dev_fwnode(dev);
		mux_desc.set = ts3usbca4_mux_set;

		ts->mux = typec_mux_register(dev, &mux_desc);
		if (IS_ERR(ts->mux)) {
			if (ts->sw)
				typec_switch_unregister(ts->sw);
			return dev_err_probe(dev, PTR_ERR(ts->mux),
					            "Failed to register typec mux\n");
		}
	}

	/* If there's no switch and no mux, this driver would be doing nothing */
	if (!ts->sw && !ts->mux)
		return dev_err_probe(dev, -EINVAL,
				     "No switch and no mux registered. Bailing out.\n");

	i2c_set_clientdata(client, ts);

	return 0;
}

static int __maybe_unused ts3usbca4_suspend(struct device *dev)
{
	struct ts3usbca4 *ts = dev_get_drvdata(dev);

	/* Setting OEn HIGH sets the chip to Standby Mode and resets registers */
	gpiod_set_value_cansleep(ts->enable_gpio, 1);

	/* When losing power the registers are zeroed so the default is LnB */
	ts->lane_sel = TS3USBCA4_SWSEL_LNB;

	return regulator_disable(ts->vcc);
}

static int __maybe_unused ts3usbca4_resume(struct device *dev)
{
	struct ts3usbca4 *ts = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&ts->lock);

	/* Get back to Normal Operation mode */
	gpiod_set_value_cansleep(ts->enable_gpio, 0);

	ret = regulator_enable(ts->vcc);
	if (ret) {
		dev_err(dev, "Could not enable regulator!\n");
		mutex_unlock(&ts->lock);
		return ret;
	}

	mutex_unlock(&ts->lock);

	return 0;
}

static const struct of_device_id ts3usbca4_of_match[] = {
	{ .compatible = "ti,ts3usbca4" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ts3usbca4_of_match);

static const struct i2c_device_id ts3usbca4_id[] = {
	{ "ts3usbca4" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ts3usbca4_id);

static SIMPLE_DEV_PM_OPS(ts3usbca4_pm_ops, ts3usbca4_suspend, ts3usbca4_resume);

static struct i2c_driver ts3usbca4_driver = {
	.driver = {
		.name		= "ts3usbca4",
		.pm = &ts3usbca4_pm_ops,
		.of_match_table	= ts3usbca4_of_match,
	},
	.probe		= ts3usbca4_probe,
	.id_table	= ts3usbca4_id,
};
module_i2c_driver(ts3usbca4_driver);

MODULE_AUTHOR("Julien Massot <julien.massot@collabora.com>");
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("Texas Instruments TS3USBCA4 Type-C SBU Multiplexer driver");
MODULE_LICENSE("GPL");
