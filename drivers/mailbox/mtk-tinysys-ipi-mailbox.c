// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek TinySYS Inter Processor Interrupt (IPI) Mailbox
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/mtk-vcp-mailbox.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * Those are used as both doorbells for the MCU to fire an interrupt to AP
 * (from firmware), or for the AP to fire an interrupt to the MCU (from here)
 * and are renamed here compared to the actual real name as to greatly enhance
 * human readability, as SET or CLEAR depends on the "point of view": seeing
 * that from the firmware or from the driver, the two meanings will be swapped!
 *
 * In real register naming (AP's point of view) those are referred to as
 * AP_TO_MCU Doorbell: MBOX_IRQ_SET
 * MCU_TO_AP Doorbell: MBOX_IRQ_CLR
 */
#define MTK_IPI_MBOX_DB_AP_TO_MCU	0x100
#define MTK_IPI_MBOX_DB_MCU_TO_AP	0x10c
#define MTK_VCP_MBOX_MAX_RX_SIZE	72

#define MTK_IPI_MBOX_CELLS		2
#define MTK_IPI_MBOX_CELL_DIRECTION	0
#define MTK_IPI_MBOX_CELL_IPI_NUMBER	1

enum mtk_vcp_chans {
	VCP_MBOX_CHAN_RX,
	VCP_MBOX_CHAN_TX,
	VCP_MBOX_CHAN_MAX
};

/**
 * struct mtk_ipi_mbox_chan_desc - per-channel constant data
 * @name:           name of this channel
 * @mbox_num:       index of the mailbox that can reach this channel
 * @fw_table_index: index of the mailbox channel in firmware tables, also
 *                  called a "mailbox pin index". Cannot be more than 31
 *                  and is limited to 5 bits for this only reason, as the
 *                  compiler will complain about overflow when value > 31
 * @ipi_num:        index of this channel, starting at 0
 * @offset:         byte offset measured from mmio base for outgoing or incoming data
 * @len:            size, in bytes, of the outgoing or incoming data on this channel
 * @txdone_ack:     true if this channel sends an ack when data fully received by MCU
 */
struct mtk_ipi_mbox_chan_desc {
	const char *name;
	const u8 mbox_num;
	const u8 fw_table_index : 5;
	const u8 ipi_num;
	const u16 offset;
	const u8 len;
	const bool txdone_ack;
};

struct mtk_ipi_mbox_variant {
	const char *mbox_type;
	const struct mtk_ipi_mbox_chan_desc *rx_channels;
	const struct mtk_ipi_mbox_chan_desc *tx_channels;
	const u8 num_tx_channels;
	const u8 num_rx_channels;
	const u8 num_mboxes;
};

struct mtk_ipi_mbox_chan {
	char *full_name;
	enum mtk_vcp_chans chan_type;
	u8 desc_idx;
	struct mbox_chan *sibling_chan;
	void *rx_buf;
};

struct mtk_ipi_mbox {
	struct mtk_ipi_main *priv;
	void __iomem *base;
	int irq;
	u8 mbox_num;
	struct mbox_controller mbox;
	struct mtk_ipi_mbox_chan *mchans;
};

struct mtk_ipi_main {
	const struct mtk_ipi_mbox_variant *pdata;
	struct device *dev;

	int num_mboxes;
	struct mtk_ipi_mbox ipi_mboxes[] __counted_by(num_mboxes);
};

static inline struct mtk_ipi_mbox *to_mtk_ipi_mbox(struct mbox_chan *chan)
{
	return container_of(chan->mbox, struct mtk_ipi_mbox, mbox);
}

static irqreturn_t mtk_ipi_mbox_isr(int irq, void *data)
{
	u32 rx_status;
	struct mbox_chan *chan = data;
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	const struct mtk_ipi_mbox_chan_desc *chrx = &pdata->rx_channels[mchan->desc_idx];

	rx_status = readl(ipi_mbox->base + MTK_IPI_MBOX_DB_MCU_TO_AP);
	if (rx_status & BIT(chrx->fw_table_index)) {
		/* Process received TX Done indication in ISR as that's fast */
		if (chrx->txdone_ack) {
			writel(BIT(chrx->fw_table_index),
			       ipi_mbox->base + MTK_IPI_MBOX_DB_MCU_TO_AP);

			mbox_chan_txdone(mchan->sibling_chan, 0);

			return IRQ_HANDLED;
		}

		/*
		 * Otherwise, some amount of data must be retrieved and that may
		 * be blocking for too much time: read that in a separate thread.
		 *
		 * The mailbox will hold the data until a RX Done signal is sent.
		 */
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t mtk_ipi_mbox_irq_thread(int irq, void *data)
{
	struct mbox_chan *chan = data;
	struct device *dev = chan->mbox->dev;
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	const struct mtk_ipi_mbox_chan_desc *chrx = &pdata->rx_channels[mchan->desc_idx];

	if (unlikely(mchan->chan_type != VCP_MBOX_CHAN_RX)) {
		dev_warn(dev, "Last TX check on RX channel!\n");
		return IRQ_NONE;
	}

	/* Copy the data from this channel */
	memcpy_fromio(mchan->rx_buf, ipi_mbox->base + chrx->offset, chrx->len);

	/* Signal AP RX done to MCU to unblock the state machine */
	writel(BIT(chrx->fw_table_index), ipi_mbox->base + MTK_IPI_MBOX_DB_MCU_TO_AP);

	mbox_chan_received_data(chan, mchan->rx_buf);
	return IRQ_HANDLED;
}

static int mtk_ipi_mbox_send_data(struct mbox_chan *chan, void *data)
{
	u32 status;
	u32 *val = data;
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	const struct mtk_ipi_mbox_chan_desc *chtx = &pdata->tx_channels[mchan->desc_idx];

	/* Make sure this channel is not busy (MCU has read the last sent data) */
	status = readl(ipi_mbox->base + MTK_IPI_MBOX_DB_AP_TO_MCU);
	if (status & BIT(chtx->fw_table_index))
		return -EBUSY;

	/* Write the data to the correct offset */
	for (int i = 0; i < chtx->len; i += 4)
		writel(val[i / 4], ipi_mbox->base + chtx->offset + i);

	/* Ring the doorbell! Signal AP TX done to MCU for it to start reading */
	writel(BIT(chtx->fw_table_index), ipi_mbox->base + MTK_IPI_MBOX_DB_AP_TO_MCU);
	return 0;
}

static bool mtk_ipi_mbox_last_tx_done(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	const struct mtk_ipi_mbox_chan_desc *chtx;
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;

	if (unlikely(mchan->chan_type != VCP_MBOX_CHAN_TX)) {
		dev_warn(dev, "Last TX check on RX channel!\n");
		return false;
	}
	chtx = &pdata->tx_channels[mchan->desc_idx];

	return !(readl(ipi_mbox->base + MTK_IPI_MBOX_DB_AP_TO_MCU) &
		 BIT(chtx->fw_table_index));
}

static int mtk_ipi_mbox_startup(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	int ret;

	/* Clear all interrupts if any fired before startup */
	writel(~0, ipi_mbox->base + MTK_IPI_MBOX_DB_MCU_TO_AP);

	if (mchan->chan_type == VCP_MBOX_CHAN_TX) {
		/* TX Done indication by IRQ on RX Channel */
		if (pdata->tx_channels[mchan->desc_idx].txdone_ack)
			chan->txdone_method = MBOX_TXDONE_BY_IRQ;
	} else {
		mchan->rx_buf = kmalloc(pdata->rx_channels[mchan->desc_idx].len,
					GFP_KERNEL);
		if (!mchan->rx_buf)
			return -ENOMEM;

		/* Format a nice and descriptive name like "vcp-ipi0:c-sleep-1" */
		mchan->full_name = kasprintf(GFP_KERNEL, "%s%d:%s",
					     pdata->mbox_type, ipi_mbox->mbox_num,
					     pdata->rx_channels[mchan->desc_idx].name);
		if (!mchan->full_name) {
			kfree(mchan->rx_buf);
			return -ENOMEM;
		}

		ret = request_threaded_irq(ipi_mbox->irq, mtk_ipi_mbox_isr,
					   mtk_ipi_mbox_irq_thread,
					   IRQF_SHARED | IRQF_ONESHOT,
					   mchan->full_name, chan);
		if (ret) {
			dev_err(dev, "Failed to request IRQ: %pe\n", ERR_PTR(ret));
			kfree(mchan->full_name);
			kfree(mchan->rx_buf);
			return ret;
		}
	}

	return 0;
}

static void mtk_ipi_mbox_shutdown(struct mbox_chan *chan)
{
	struct mtk_ipi_mbox_chan *mchan = chan->con_priv;
	struct mtk_ipi_mbox *ipi_mbox = to_mtk_ipi_mbox(chan);

	if (mchan->chan_type == VCP_MBOX_CHAN_TX)
		return;

	free_irq(ipi_mbox->irq, chan);
	kfree(mchan->full_name);
	kfree(mchan->rx_buf);
}

static const struct mbox_chan_ops mtk_ipi_mbox_chan_ops = {
	.send_data	= mtk_ipi_mbox_send_data,
	.last_tx_done	= mtk_ipi_mbox_last_tx_done,
	.startup	= mtk_ipi_mbox_startup,
	.shutdown	= mtk_ipi_mbox_shutdown,
};

static struct mbox_chan *mtk_ipi_mbox_chan_of_xlate(struct mbox_controller *mbox,
						    const struct of_phandle_args *spec)
{
	struct mtk_ipi_mbox *ipi_mbox = container_of(mbox, struct mtk_ipi_mbox, mbox);
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	const struct mtk_ipi_mbox_chan_desc *ch;
	unsigned int direction, ipi_channel;

	if (spec->args_count != MTK_IPI_MBOX_CELLS)
		return ERR_PTR(-EINVAL);

	/* TX or RX */
	direction = spec->args[MTK_IPI_MBOX_CELL_DIRECTION];
	switch (direction) {
	case VCP_MBOX_CHAN_RX:
		ch = pdata->rx_channels;
		break;
	case VCP_MBOX_CHAN_TX:
		ch = pdata->tx_channels;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ipi_channel = spec->args[MTK_IPI_MBOX_CELL_IPI_NUMBER];

	for (int i = 0; i < mbox->num_chans; i++) {
		struct mtk_ipi_mbox_chan *mchan = mbox->chans[i].con_priv;

		/* Should that happen, something is really wrong */
		if (unlikely(mchan->chan_type != direction))
			continue;

		if (ch[mchan->desc_idx].ipi_num != ipi_channel)
			continue;

		return &mbox->chans[i];
	}

	return ERR_PTR(-ENOENT);
}

static unsigned int mtk_ipi_mbox_get_num_ch(const struct mtk_ipi_mbox_chan_desc *desc,
					    unsigned int num_descs, u8 mbox_num)
{
	unsigned int num_mbox_channels = 0;

	for (int i = 0; i < num_descs; i++)
		if (desc[i].mbox_num == mbox_num)
			num_mbox_channels++;

	return num_mbox_channels;
}

static int mtk_ipi_mbox_probe(struct mtk_ipi_mbox *ipi_mbox, struct device_node *np)
{
	const struct mtk_ipi_mbox_variant *pdata = ipi_mbox->priv->pdata;
	struct mbox_controller *mbox = &ipi_mbox->mbox;
	unsigned int num_rx_channels, num_tx_channels;
	bool siblings_present = false;
	unsigned int mbox_chans = 0;
	int remaining_channels;
	int i, ret;

	mbox->dev = ipi_mbox->priv->dev;
	mbox->txpoll_period = 0;
	mbox->txdone_poll = true;
	mbox->ops = &mtk_ipi_mbox_chan_ops;
	mbox->of_xlate = mtk_ipi_mbox_chan_of_xlate;

	num_rx_channels = mtk_ipi_mbox_get_num_ch(pdata->rx_channels,
						  pdata->num_rx_channels,
						  ipi_mbox->mbox_num);
	num_tx_channels = mtk_ipi_mbox_get_num_ch(pdata->tx_channels,
						  pdata->num_tx_channels,
						  ipi_mbox->mbox_num);
	if (!num_rx_channels && !num_tx_channels)
		return dev_err_probe(mbox->dev, -ENOENT, "No mailbox channels!\n");

	mbox->num_chans = num_rx_channels + num_tx_channels;
	mbox->chans = devm_kcalloc(mbox->dev, mbox->num_chans,
				   sizeof(*mbox->chans), GFP_KERNEL);
	if (!mbox->chans)
		return -ENOMEM;

	ipi_mbox->mchans = devm_kmalloc_array(mbox->dev, mbox->num_chans,
					      sizeof(*ipi_mbox->mchans), GFP_KERNEL);
	if (!ipi_mbox->mchans)
		return -ENOMEM;

	/* Initialize RX channels */
	remaining_channels = num_rx_channels;
	for (i = 0; i < pdata->num_rx_channels; i++) {
		struct mtk_ipi_mbox_chan *mchans;

		if (remaining_channels <= 0)
			break;

		if (pdata->rx_channels[i].mbox_num != ipi_mbox->mbox_num)
			continue;

		mchans = &ipi_mbox->mchans[mbox_chans];

		mchans->chan_type = VCP_MBOX_CHAN_RX;
		mchans->desc_idx = i;

		spin_lock_init(&mbox->chans[i].lock);
		mbox->chans[mbox_chans].con_priv = mchans;

		if (pdata->rx_channels[i].txdone_ack)
			siblings_present = true;

		remaining_channels--;
		mbox_chans++;
	}

	/* ...and TX channels as well */
	remaining_channels = num_tx_channels;
	for (i = 0; i < pdata->num_tx_channels; i++) {
		struct mtk_ipi_mbox_chan *mchans;

		if (remaining_channels <= 0)
			break;

		if (pdata->tx_channels[i].mbox_num != ipi_mbox->mbox_num)
			continue;

		mchans = &ipi_mbox->mchans[mbox_chans];

		mchans->chan_type = VCP_MBOX_CHAN_TX;
		mchans->desc_idx = i;

		spin_lock_init(&mbox->chans[i].lock);
		mbox->chans[mbox_chans].con_priv = mchans;

		remaining_channels--;
		mbox_chans++;
	}

	/*
	 * Some TX channels have got a sibling RX channel that is used to send
	 * a TX Done (MCU RX Done) indication to the AP through an IPI interrupt
	 * on a RX Channel: now that all channel arrays are initialized, check
	 * if any was found with siblings and store a pointer to the TX sibling
	 * in the RX channel entry.
	 *
	 * Note that those channels are a bit special as the indication is used
	 * to ensure atomicity, hence synchronization between AP and MCU(s).
	 */
	if (siblings_present) {
		for (i = 0; i < num_rx_channels; i++) {
			struct mtk_ipi_mbox_chan *mchan_rx, *mchan_tx;
			const struct mtk_ipi_mbox_chan_desc *chd_rx, *chd_tx;
			bool sibling_found;

			mchan_rx = &ipi_mbox->mchans[i];
			chd_rx = &pdata->rx_channels[mchan_rx->desc_idx];

			if (!chd_rx->txdone_ack)
				continue;

			sibling_found = false;

			for (int j = 0; j < num_tx_channels; j++) {
				int tx_chan_idx = j + num_rx_channels;

				mchan_tx = &ipi_mbox->mchans[tx_chan_idx];
				chd_tx = &pdata->tx_channels[mchan_tx->desc_idx];

				if (chd_rx->ipi_num != chd_tx->ipi_num)
					continue;

				sibling_found = true;
				mchan_rx->sibling_chan = &mbox->chans[tx_chan_idx];
			}

			if (!sibling_found)
				return dev_err_probe(mbox->dev, ret,
						     "Could not find TX Done sibling for %s!\n",
						     pdata->rx_channels[i].name);
		}
	}

	return devm_mbox_controller_register(mbox->dev, mbox);
}

static int mtk_ipi_probe(struct platform_device *pdev)
{
	const struct mtk_ipi_mbox_variant *pdata;
	struct device *dev = &pdev->dev;
	struct mtk_ipi_mbox *ipi_mbox;
	struct mtk_ipi_main *priv;
	int num_mboxes, ret;
	u8 mbox_num;

	num_mboxes = of_get_available_child_count(dev->of_node);
	if (!num_mboxes)
		return dev_err_probe(dev, -ENODEV, "No mailboxes found!\n");

	pdata = device_get_match_data(dev);
	if (!pdata)
		return -EINVAL;

	priv = devm_kzalloc(dev, struct_size(priv, ipi_mboxes, num_mboxes), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->num_mboxes = num_mboxes;
	priv->pdata = pdata;

	ipi_mbox = priv->ipi_mboxes;

	mbox_num = 0;
	for_each_available_child_of_node_scoped(dev->of_node, np) {
		ipi_mbox->priv = priv;

		ipi_mbox->base = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(ipi_mbox->base))
			return PTR_ERR(ipi_mbox->base);

		ipi_mbox->irq = platform_get_irq(pdev, 0);
		if (ipi_mbox->irq < 0)
			return ipi_mbox->irq;

		ipi_mbox->mbox_num = mbox_num;

		ret = mtk_ipi_mbox_probe(ipi_mbox, np);
		if (ret)
			return ret;

		ipi_mbox++;
		mbox_num++;
	}

	if (mbox_num < priv->num_mboxes)
		return dev_err_probe(dev, -EINVAL,
				     "Number of mailboxes should be %u, found %u.\n",
				     mbox_num, priv->num_mboxes);

	platform_set_drvdata(pdev, priv);

	return 0;
}
/*
const struct mtk_ipi_mbox_chan_desc mtk_ipi_mbox_rx_channels_mt8196[] = {
	{ "vdec",             0,  1, 0x0012, 72 },
	{ "c-sleep-0",        2,  2, 0x002e,  2, true },
	{ "vcp-ready-0",      2,  5, 0x002c,  2 },
	{ "mmdvfs-vcp",       1, 10, 0x0036,  4 },
	{ "mmqos",            2, 12, 0x001a, 72 },
	{ "mmdebug",          3, 14, 0x0008,  4 },
	{ "c-vcp-hwv-debug",  1, 15, 0x001c, 32, true },
	{ "venc",             1, 17, 0x0024, 72 },
	{ "c-sleep-1",        4, 20, 0x000a,  2, true },
	{ "vcp-ready-1",      4, 26, 0x0008,  2 },
	{ "mmdvfs-mmup",      3, 34, 0x0006,  4 },
};

const struct mtk_ipi_mbox_chan_desc mtk_ipi_mbox_tx_channels_mt8196[] = {
	{ "vdec",             0,  0, 0x0000, 72 },
	{ "c-sleep-0",        2,  2, 0x0012,  4 },
	{ "test-0",           2,  3, 0x0014, 12 },
	{ "mmdvfs-vcp",       1,  9, 0x001a,  4 },
	{ "mmqos",            2, 11, 0x0000, 72 },
	{ "mmdebug",          3, 13, 0x0002,  4 },
	{ "c-vcp-hwv-debug",  1, 15, 0x0000, 32 },
	{ "venc",             1, 16, 0x0008, 72 },
	{ "c-sleep-1",        4, 20, 0x0000,  4 },
	{ "test-1",           4, 21, 0x0002, 12 },
	{ "vcpctl-1",         4, 23, 0x0006,  4 },
	{ "vcpctl-0",         2, 32, 0x0018,  4 },
	{ "mmdvfs-mmup",      3, 33, 0x0000,  4 },
	{ "vdisp",            3, 35, 0x0004,  4 },
}
*/

/*
<6>[    6.614925] mtk-vcp 31800000.vcp: creating channel 31800000.vcp addr 0x0
<3>[    6.627016] mtk-vcp 31800000.vcp: [TX] mbox=0 ch=0 offset=0x0 sz=18 pin_index=0x0
<3>[    6.642689] mtk-vcp 31800000.vcp: [TX] mbox=1 ch=15 offset=0x0 sz=8 pin_index=0x0
<3>[    6.650444] mtk-vcp 31800000.vcp: [TX] mbox=1 ch=16 offset=0x8 sz=18 pin_index=0x4
<3>[    6.658276] mtk-vcp 31800000.vcp: [TX] mbox=1 ch=9 offset=0x1a sz=2 pin_index=0xd
<3>[    6.689799] mtk-vcp 31800000.vcp: [TX] mbox=2 ch=11 offset=0x0 sz=18 pin_index=0x0
<3>[    6.697627] mtk-vcp 31800000.vcp: [TX] mbox=2 ch=2 offset=0x12 sz=2 pin_index=0x9
<3>[    6.705366] mtk-vcp 31800000.vcp: [TX] mbox=2 ch=3 offset=0x14 sz=3 pin_index=0xa
<3>[    6.713101] mtk-vcp 31800000.vcp: [TX] mbox=2 ch=32 offset=0x18 sz=2 pin_index=0xc
<3>[    6.744503] mtk-vcp 31800000.vcp: [TX] mbox=3 ch=33 offset=0x0 sz=2 pin_index=0x0
<3>[    6.752239] mtk-vcp 31800000.vcp: [TX] mbox=3 ch=13 offset=0x2 sz=2 pin_index=0x1
<3>[    6.759975] mtk-vcp 31800000.vcp: [TX] mbox=3 ch=35 offset=0x4 sz=2 pin_index=0x2
<3>[    6.783194] mtk-vcp 31800000.vcp: [TX] mbox=4 ch=20 offset=0x0 sz=2 pin_index=0x0
<3>[    6.790934] mtk-vcp 31800000.vcp: [TX] mbox=4 ch=21 offset=0x2 sz=3 pin_index=0x1
<3>[    6.798691] mtk-vcp 31800000.vcp: [TX] mbox=4 ch=23 offset=0x6 sz=2 pin_index=0x3

<3>[    6.634788] mtk-vcp 31800000.vcp: [RX] mbox=0 ch=1 offset=0x12 sz=18 pin_index=0x9
<3>[    6.666022] mtk-vcp 31800000.vcp: [RX] mbox=1 ch=15 offset=0x1c sz=8 pin_index=0xe
<3>[    6.673854] mtk-vcp 31800000.vcp: [RX] mbox=1 ch=17 offset=0x24 sz=18 pin_index=0x12
<3>[    6.681854] mtk-vcp 31800000.vcp: [RX] mbox=1 ch=10 offset=0x36 sz=2 pin_index=0x1b
<3>[    6.720926] mtk-vcp 31800000.vcp: [RX] mbox=2 ch=12 offset=0x1a sz=18 pin_index=0xd
<3>[    6.728836] mtk-vcp 31800000.vcp: [RX] mbox=2 ch=5 offset=0x2c sz=1 pin_index=0x16
<3>[    6.736663] mtk-vcp 31800000.vcp: [RX] mbox=2 ch=2 offset=0x2e sz=1 pin_index=0x17
<3>[    6.767710] mtk-vcp 31800000.vcp: [RX] mbox=3 ch=34 offset=0x6 sz=2 pin_index=0x3
<3>[    6.775443] mtk-vcp 31800000.vcp: [RX] mbox=3 ch=14 offset=0x8 sz=2 pin_index=0x4
<3>[    6.806446] mtk-vcp 31800000.vcp: [RX] mbox=4 ch=26 offset=0x8 sz=1 pin_index=0x4
<3>[    6.814180] mtk-vcp 31800000.vcp: [RX] mbox=4 ch=20 offset=0xa sz=1 pin_index=0x5
*/

/* Channel descriptors are ordered by IPI ID */
const struct mtk_ipi_mbox_chan_desc mtk_vcp_mbox_rx_channels_mt8196[] = {
	{ "vdec",             0,  9,  1, 0x0012, 72 },
	{ "c-sleep-0",        2, 23,  2, 0x002e,  2, true },
	{ "vcp-ready-0",      2, 22,  5, 0x002c,  2 },
	{ "mmdvfs",           1, 27, 10, 0x0036,  4 },
	{ "mmqos",            2, 13, 12, 0x001a, 72 },
	{ "c-vcp-hwv-debug",  1, 14, 15, 0x001c, 32, true },
	{ "venc",             1, 18, 17, 0x0024, 72 },
};

const struct mtk_ipi_mbox_chan_desc mtk_vcp_mbox_tx_channels_mt8196[] = {
	{ "vdec",             0,  0,  0, 0x0000, 72 },
	{ "c-sleep-0",        2,  9,  2, 0x0012,  4, true },
	{ "test-0",           2, 10,  3, 0x0014, 12 },
	{ "mmdvfs-vcp",       1, 13,  9, 0x001a,  4 },
	{ "mmqos",            2,  0, 11, 0x0000, 72 },
	{ "c-vcp-hwv-debug",  1,  0, 15, 0x0000, 32, true },
	{ "venc",             1,  4, 16, 0x0008, 72 },
	{ "vcpctl-0",         2, 12, 32, 0x0018,  4 },
};

const struct mtk_ipi_mbox_chan_desc mtk_mmup_mbox_rx_channels_mt8196[] = {
	{ "mmdebug",          0,  4, 14, 0x0008,  4 },
	{ "c-sleep-1",        1,  5, 20, 0x000a,  2, true },
	{ "vcp-ready-1",      1,  4, 26, 0x0008,  2 },
	{ "mmdvfs-mmup",      0,  3, 34, 0x0006,  4 },
};

const struct mtk_ipi_mbox_chan_desc mtk_mmup_mbox_tx_channels_mt8196[] = {
	{ "mmdebug",          0,  1, 13, 0x0002,  4 },
	{ "c-sleep-1",        1,  0, 20, 0x0000,  4, true },
	{ "test-1",           1,  1, 21, 0x0002, 12 },
	{ "vcpctl-1",         1,  3, 23, 0x0006,  4 },
	{ "mmdvfs-mmup",      0,  0, 33, 0x0000,  4 },
	{ "vdisp",            0,  2, 35, 0x0004,  4 },
};

static const struct mtk_ipi_mbox_variant mtk_vcp_mbox_mt8196 = {
	.mbox_type = "vcp-ipi",
	.rx_channels = mtk_vcp_mbox_rx_channels_mt8196,
	.tx_channels = mtk_vcp_mbox_tx_channels_mt8196,
	.num_rx_channels = ARRAY_SIZE(mtk_vcp_mbox_rx_channels_mt8196),
	.num_tx_channels = ARRAY_SIZE(mtk_vcp_mbox_tx_channels_mt8196),
	.num_mboxes = 3,
};

static const struct mtk_ipi_mbox_variant mtk_mmup_mbox_mt8196 = {
	.mbox_type = "mmup-ipi",
	.rx_channels = mtk_mmup_mbox_rx_channels_mt8196,
	.tx_channels = mtk_mmup_mbox_tx_channels_mt8196,
	.num_rx_channels = ARRAY_SIZE(mtk_mmup_mbox_rx_channels_mt8196),
	.num_tx_channels = ARRAY_SIZE(mtk_mmup_mbox_tx_channels_mt8196),
	.num_mboxes = 2,
};

static const struct of_device_id mtk_ipi_mbox_of_match[] = {
	{ .compatible = "mediatek,mt8196-vcp-ipi-mbox", .data = &mtk_vcp_mbox_mt8196 },
	{ .compatible = "mediatek,mt8196-mmup-ipi-mbox", .data = &mtk_mmup_mbox_mt8196 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mtk_ipi_mbox_of_match);

static struct platform_driver mtk_ipi_mbox_driver = {
	.probe		= mtk_ipi_probe,
	.driver = {
		.name	= "mediatek-ipi-mailbox",
		.of_match_table = mtk_ipi_mbox_of_match,
	},
};
module_platform_driver(mtk_ipi_mbox_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_DESCRIPTION("MediaTek TinySYS IPI Mailbox Controller");
MODULE_LICENSE("GPL");
