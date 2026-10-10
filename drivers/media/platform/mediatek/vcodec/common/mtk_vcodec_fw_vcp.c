// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Kyrie Wu <kyrie.wu@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/remoteproc/mtk_vcp_public.h>
#include <linux/firmware/mediatek/mtk-vcp-ipc.h>

#include "../decoder/mtk_vcodec_dec_drv.h"
#include "../decoder/vdec_ipi_msg.h"
#include "../encoder/mtk_vcodec_enc.h"
#include "../encoder/mtk_vcodec_enc_drv.h"
#include "mtk_vcodec_fw_priv.h"

#define IPI_SEND_TIMEOUT_MS  100U
#define IPI_TIMEOUT_MS       100U

#define VCP_IPI_HEADER_SIZE (sizeof(u32) * 2)
#define VCP_IPI_ALIGN (4)

static bool mtk_vcodec_vcp_ipi_id_valid(unsigned int ipi_id)
{
	return ipi_id < VCP_IPI_MAX;
}

static void mtk_vcodec_vcp_ipi_lock(struct mtk_vcp *vcp, u32 ipi_id)
{
	mutex_lock(&vcp->ipi_desc[ipi_id].lock);
}

static void mtk_vcodec_vcp_ipi_unlock(struct mtk_vcp *vcp, u32 ipi_id)
{
	lockdep_assert_held(&vcp->ipi_desc[ipi_id].lock);
	mutex_unlock(&vcp->ipi_desc[ipi_id].lock);
}

static void mtk_vcodec_vcp_msq_queue_lock(struct mtk_vcodec_fw *fw, unsigned long *flags)
{
	spin_lock_irqsave(&fw->vcp->msg_queue.lock, *flags);
}

static void mtk_vcodec_vcp_msq_queue_unlock(struct mtk_vcodec_fw *fw, unsigned long *flags)
{
	spin_unlock_irqrestore(&fw->vcp->msg_queue.lock, *flags);
}

static int mtk_vcodec_vcp_notifier(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct mtk_vcp *vcp = container_of(nb, struct mtk_vcp, vcp_notify);

	switch (event) {
	case VCP_EVENT_SUSPEND:
	case VCP_EVENT_STOP:
		dev_dbg(&vcp->pdev->dev, "vcp notifier suspend");
		break;
	case VCP_EVENT_READY:
	case VCP_EVENT_RESUME:
		dev_dbg(&vcp->pdev->dev, "vcp notifier ready");
		break;
	}

	return NOTIFY_DONE;
}

static void mtk_vcodec_vcp_free_msg_node(struct mtk_vcodec_fw *fw,
					 struct mtk_vcp_msg_node *msg_node)
{
	unsigned long flags;

	mtk_vcodec_vcp_msq_queue_lock(fw, &flags);
	list_add(&msg_node->list, &fw->vcp->msg_queue.node_list);
	mtk_vcodec_vcp_msq_queue_unlock(fw, &flags);
}

static int mtk_vcodec_vcp_ipi_register(struct mtk_vcp *vcp, u32 ipi_id, vcp_ipi_handler_t handler,
				       void *priv)
{
	if (!vcp)
		return -EPROBE_DEFER;

	if (WARN_ON(!mtk_vcodec_vcp_ipi_id_valid(ipi_id)) || WARN_ON(!handler))
		return -EINVAL;

	mtk_vcodec_vcp_ipi_lock(vcp, ipi_id);
	vcp->ipi_desc[ipi_id].handler = handler;
	vcp->ipi_desc[ipi_id].priv = priv;
	mtk_vcodec_vcp_ipi_unlock(vcp, ipi_id);

	return 0;
}

static int mtk_vcodec_vcp_msg_process_thread(void *arg)
{
	struct mtk_vcodec_fw *fw = arg;
	struct vdec_vpu_ipi_ack *msg = NULL;
	struct mtk_vcp_share_obj *obj;
	struct mtk_vcp_msg_node *msg_node;
	vcp_ipi_handler_t handler;
	unsigned long flags;
	int ret = 0;

	do {
		ret = wait_event_interruptible(fw->vcp->msg_queue.wq,
					       atomic_read(&fw->vcp->msg_queue.cnt) > 0 ||
					       kthread_should_stop());
		if (ret < 0) {
			dev_err(&fw->pdev->dev, "wait msg queue ack timeout %d %d\n",
				ret, atomic_read(&fw->vcp->msg_queue.cnt));
			continue;
		}
		if (kthread_should_stop())
			break;

		mtk_vcodec_vcp_msq_queue_lock(fw, &flags);
		msg_node = list_entry(fw->vcp->msg_queue.msg_list.next,
				      struct mtk_vcp_msg_node, list);
		list_del(&msg_node->list);
		atomic_dec(&fw->vcp->msg_queue.cnt);
		mtk_vcodec_vcp_msq_queue_unlock(fw, &flags);

		obj = &msg_node->ipi_data;
		msg = (struct vdec_vpu_ipi_ack *)obj->share_buf;

		if (!msg->ap_inst_addr) {
			dev_err(&fw->pdev->dev, "invalid message address\n");
			mtk_vcodec_vcp_free_msg_node(fw, msg_node);
			continue;
		}

		dev_dbg(&fw->pdev->dev, "msg ack id %d len %d msg_id 0x%x\n", obj->id, obj->len,
			msg->msg_id);

		if (!mtk_vcodec_vcp_ipi_id_valid(obj->id)) {
			dev_err(&fw->pdev->dev, "invalid ack ipi id %u\n", obj->id);
			mtk_vcodec_vcp_free_msg_node(fw, msg_node);
			continue;
		}

		mtk_vcodec_vcp_ipi_lock(fw->vcp, obj->id);
		handler = fw->vcp->ipi_desc[obj->id].handler;
		if (!handler) {
			dev_err(&fw->pdev->dev, "invalid ack ipi handler id = %d\n", obj->id);
			mtk_vcodec_vcp_ipi_unlock(fw->vcp, obj->id);
			mtk_vcodec_vcp_free_msg_node(fw, msg_node);
			continue;
		}

		handler(msg, obj->len, fw->vcp->ipi_desc[obj->id].priv);
		mtk_vcodec_vcp_ipi_unlock(fw->vcp, obj->id);

		fw->vcp->msg_signaled[obj->id] = true;
		wake_up(&fw->vcp->msg_wq[obj->id]);

		mtk_vcodec_vcp_free_msg_node(fw, msg_node);
	} while (!kthread_should_stop());

	return ret;
}

static int mtk_vcodec_vcp_msg_ack_isr(unsigned int id, void *prdata, void *data, unsigned int len)
{
	struct mtk_vcodec_fw *fw = prdata;
	struct mtk_vcp_msg_queue *msg_queue = &fw->vcp->msg_queue;
	struct mtk_vcp_msg_node *msg_node;
	struct vdec_vpu_ipi_ack *msg = NULL;
	struct mtk_vcp_share_obj *obj = data;
	unsigned long flags;

	msg = (struct vdec_vpu_ipi_ack *)obj->share_buf;

	if (!mtk_vcodec_vcp_ipi_id_valid(obj->id)) {
		dev_err(&fw->pdev->dev, "invalid ack ipi id %u\n", obj->id);
		return -EINVAL;
	}

	mtk_vcodec_vcp_msq_queue_lock(fw, &flags);
	if (!list_empty(&msg_queue->node_list)) {
		msg_node = list_entry(msg_queue->node_list.next, struct mtk_vcp_msg_node, list);

		memcpy(&msg_node->ipi_data, obj, sizeof(*obj));
		list_move_tail(&msg_node->list, &msg_queue->msg_list);
		atomic_inc(&msg_queue->cnt);
		mtk_vcodec_vcp_msq_queue_unlock(fw, &flags);

		dev_dbg(&fw->pdev->dev, "push ipi_id %x msg_id %x, msg cnt %d\n",
			obj->id, msg->msg_id, atomic_read(&msg_queue->cnt));

		wake_up(&msg_queue->wq);
	} else {
		mtk_vcodec_vcp_msq_queue_unlock(fw, &flags);
		dev_err(&fw->pdev->dev, "no free nodes in msg queue\n");
	}

	return 0;
}

static int mtk_vcodec_vcp_msg_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
				       unsigned int len, unsigned int wait)
{
	struct mtk_vcp *vcp = fw->vcp;
	struct mtk_vcp_device *vcp_device = vcp->vcp_device;
	bool *msg_signaled;
	wait_queue_head_t *msg_wq;
	int ret, ipi_size, feature_id, mailbox_id, retry_cnt = 0;
	unsigned long timeout_jiffies = 0;
	struct mtk_vcp_share_obj obj = {0};
	unsigned int *data;

	if (id < 0 || !mtk_vcodec_vcp_ipi_id_valid(id)) {
		dev_err(&fw->pdev->dev, "invalid ipi id %d\n", id);
		return -EINVAL;
	}

	msg_signaled = &vcp->msg_signaled[id];
	msg_wq = &vcp->msg_wq[id];

	if (!vcp_device) {
		dev_dbg(&fw->pdev->dev, "vcp device is null\n");
		return -EINVAL;
	}

	mutex_lock(&vcp->ipi_mutex);
	if (fw->fw_use == ENCODER) {
		feature_id = VENC_FEATURE_ID;
		mailbox_id = IPI_OUT_VENC_0;
	} else {
		feature_id = VDEC_FEATURE_ID;
		mailbox_id = IPI_OUT_VDEC_1;
	}

	timeout_jiffies = jiffies + msecs_to_jiffies(VCP_SYNC_TIMEOUT_MS);
	while (!vcp_device->ops->vcp_is_ready(vcp_device, feature_id)) {
		if (time_after(jiffies, timeout_jiffies)) {
			vcp->ipi_id_ack[id] = -EINVAL;
			ret = -EINVAL;
			goto error;
		}
		usleep_range(1000, 2000);
	}

	if (len > VCP_SHARE_BUF_SIZE) {
		vcp->ipi_id_ack[id] = -EINVAL;
		ret = -EINVAL;
		goto error;
	}

	obj.id = id;
	obj.len = len;
	memcpy(obj.share_buf, buf, len);

	ipi_size = round_up(VCP_IPI_HEADER_SIZE + len, VCP_IPI_ALIGN);
	data = (unsigned int *)obj.share_buf;
	dev_dbg(&fw->pdev->dev, "vcp send message: id %d len %d data 0x%x\n",
		obj.id, obj.len, data[0]);

	*msg_signaled = false;
	vcp->ipi_id_ack[id] = VCODEC_IPI_MSG_STATUS_OK;

	ret = mtk_vcp_ipc_send(vcp_get_ipidev(vcp_device), mailbox_id, &obj, ipi_size);
	if (ret != IPI_ACTION_DONE) {
		vcp->ipi_id_ack[id] = -EIO;
		ret = -EIO;
		goto error;
	}

wait_ack:
	/* wait for VCP's ACK */
	ret = wait_event_interruptible_timeout(*msg_wq, *msg_signaled,
					       msecs_to_jiffies(IPI_TIMEOUT_MS));
	if (!ret) {
		vcp->ipi_id_ack[id] = VCODEC_IPI_MSG_STATUS_FAIL;
		dev_err(&fw->pdev->dev, "wait ipi ack timeout! %d %d\n", ret, vcp->ipi_id_ack[id]);
	} else if (ret == -ERESTARTSYS) {
		if (retry_cnt++ < 5)
			goto wait_ack;

		dev_err(&fw->pdev->dev, "wait ipi ack err (%d)\n", vcp->ipi_id_ack[id]);
		vcp->ipi_id_ack[id] = VCODEC_IPI_MSG_STATUS_FAIL;
	} else if (ret < 0) {
		dev_err(&fw->pdev->dev, "wait ipi ack fail ret %d %d\n", ret, vcp->ipi_id_ack[id]);
		vcp->ipi_id_ack[id] = VCODEC_IPI_MSG_STATUS_FAIL;
	}

	dev_dbg(&fw->pdev->dev, "receive message: id %d len %d data 0x%x\n",
		obj.id, obj.len, data[0]);

	mutex_unlock(&vcp->ipi_mutex);

	return vcp->ipi_id_ack[id];

error:
	mutex_unlock(&vcp->ipi_mutex);
	dev_err(&fw->pdev->dev, "send msg error type:%d msg:%d > %d ret:%d\n", fw->type, len,
		VCP_SHARE_BUF_SIZE, ret);

	return ret;
}

static bool mtk_vcodec_vcp_driver_loaded(struct mtk_vcodec_fw *fw)
{
	struct device *dev = &fw->pdev->dev;
	struct device_driver *drv;

	drv = driver_find("mtk-vcp", &platform_bus_type);
	if (!drv) {
		dev_dbg(dev, "find mtk-vcp driver failed, need to reload.");
		return false;
	}

	return true;
}

static int mtk_vcodec_vcp_get_vcp_device(struct mtk_vcodec_fw *fw)
{
	struct device *dev = &fw->pdev->dev;
	int retry = 0, retry_cnt = 10000;
	phandle vcp_phandle;

	while (!try_then_request_module(mtk_vcodec_vcp_driver_loaded(fw), "mtk-vcp")) {
		if (++retry > retry_cnt) {
			dev_err(dev, "failed to load mtk-vcp module");
			return -EPROBE_DEFER;
		}
		usleep_range(1000, 2000);
	}

	if (of_property_read_u32(dev->of_node, "mediatek,vcp", &vcp_phandle)) {
		dev_err(dev, "can't get vcp handle.\n");
		return -ENODEV;
	}

	fw->vcp->vcp_device = mtk_vcp_get_by_phandle(vcp_phandle);
	if (!fw->vcp->vcp_device) {
		dev_err(dev, "get vcp device failed\n");
		return -ENODEV;
	}

	return 0;
}

static void mtk_vcodec_vcp_put_device(struct mtk_vcodec_fw *fw)
{
	if (!fw->vcp->vcp_device)
		return;

	rproc_put(fw->vcp->vcp_device->rproc);
	fw->vcp->vcp_device = NULL;
}

static int mtk_vcodec_vcp_load_firmware(struct mtk_vcodec_fw *fw)
{
	struct mtk_vcp_device *vcp_device;
	int ret, feature_id, mem_id, mailbox_id;
	int i;

	if (fw->vcp->is_init_done) {
		dev_dbg(&fw->pdev->dev, "vcp has already been initialized done.\n");
		return 0;
	}

	if (mtk_vcodec_vcp_get_vcp_device(fw) < 0) {
		dev_err(&fw->pdev->dev, "vcp device is null.\n");
		return -EINVAL;
	}

	vcp_device = fw->vcp->vcp_device;

	if (fw->fw_use == ENCODER) {
		feature_id = VENC_FEATURE_ID;
		mem_id = VENC_MEM_ID;
		mailbox_id = IPI_IN_VENC_0;
	} else {
		feature_id = VDEC_FEATURE_ID;
		mem_id = VDEC_MEM_ID;
		mailbox_id = IPI_IN_VDEC_1;
	}

	ret = mtk_vcp_mbox_ipc_register(vcp_get_ipidev(vcp_device), mailbox_id,
					mtk_vcodec_vcp_msg_ack_isr, fw, &fw->vcp->share_data);
	if (ret) {
		dev_dbg(&fw->pdev->dev, "ipi register fail %d %d %d %d\n", ret, feature_id,
			mem_id, mailbox_id);
		ret = -EINVAL;
		goto err_put_device;
	}
	fw->vcp->feature_id = feature_id;
	fw->vcp->mailbox_id = mailbox_id;
	fw->vcp->is_ipi_registered = true;

	fw->vcp->vcp_notify.notifier_call = mtk_vcodec_vcp_notifier;
	fw->vcp->vcp_notify.priority = 1;
	vcp_device->ops->register_notify(vcp_device, feature_id, &fw->vcp->vcp_notify);
	fw->vcp->is_notify_registered = true;

	if (!fw->vcp->is_register_done) {
		ret = vcp_device->ops->register_feature(vcp_device, feature_id);
		if (ret < 0) {
			dev_err(&fw->pdev->dev, "%d register to vcp fail(%d)\n", feature_id, ret);
			ret = -EINVAL;
			goto err_unregister_notify;
		}

		fw->vcp->is_register_done = true;
	}

	fw->vcp->is_init_done = true;

	for (i = 0; i < VCP_IPI_MAX; i++)
		mutex_init(&fw->vcp->ipi_desc[i].lock);
	mutex_init(&fw->vcp->ipi_mutex);

	if (fw->fw_use == ENCODER) {
		init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_ENCODER]);
		fw->vcp->msg_thread =
			kthread_run(mtk_vcodec_vcp_msg_process_thread, fw, "vcp_enc_msq_thread");
		if (IS_ERR(fw->vcp->msg_thread)) {
			ret = PTR_ERR(fw->vcp->msg_thread);
			fw->vcp->msg_thread = NULL;
			goto err_deregister_feature;
		}

		fw->vcp->vsi_addr = vcp_device->ops->get_mem_virt(vcp_device, mem_id);
		fw->vcp->vsi_size = vcp_device->ops->get_mem_size(vcp_device, mem_id);
		fw->vcp->iova_addr = vcp_device->ops->get_mem_iova(vcp_device, mem_id);

		dev_dbg(&fw->pdev->dev, "enc vcp init done => va: %p size:0x%x iova:%pad.\n",
			fw->vcp->vsi_addr, fw->vcp->vsi_size, &fw->vcp->iova_addr);

		return 0;
	}

	init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_LAT_DECODER]);
	init_waitqueue_head(&fw->vcp->msg_wq[VCP_IPI_CORE_DECODER]);
	fw->vcp->msg_thread =
		kthread_run(mtk_vcodec_vcp_msg_process_thread, fw, "vcp_vdec_msq_thread");
	if (IS_ERR(fw->vcp->msg_thread)) {
		ret = PTR_ERR(fw->vcp->msg_thread);
		fw->vcp->msg_thread = NULL;
		goto err_deregister_feature;
	}

	fw->vcp->vsi_addr = vcp_device->ops->get_mem_virt(vcp_device, mem_id);
	fw->vcp->vsi_core_addr = fw->vcp->vsi_addr + VCODEC_VSI_LEN;
	fw->vcp->vsi_size = vcp_device->ops->get_mem_size(vcp_device, mem_id);
	fw->vcp->iova_addr = vcp_device->ops->get_mem_iova(vcp_device, mem_id);

	dev_dbg(&fw->pdev->dev, "vdec vcp init done => va: %p size:0x%x iova:%p.\n",
		fw->vcp->vsi_addr, fw->vcp->vsi_size, &fw->vcp->iova_addr);

	return 0;

err_deregister_feature:
	if (fw->vcp->is_register_done) {
		vcp_device->ops->deregister_feature(vcp_device, feature_id);
		fw->vcp->is_register_done = false;
	}
err_unregister_notify:
	if (fw->vcp->is_notify_registered) {
		vcp_device->ops->unregister_notify(vcp_device, feature_id,
						       &fw->vcp->vcp_notify);
		fw->vcp->is_notify_registered = false;
	}
	if (fw->vcp->is_ipi_registered) {
		mtk_vcp_mbox_ipc_unregister(vcp_get_ipidev(vcp_device), mailbox_id);
		fw->vcp->is_ipi_registered = false;
	}
	fw->vcp->is_init_done = false;

err_put_device:
	mtk_vcodec_vcp_put_device(fw);
	return ret;
}

static unsigned int mtk_vcodec_vcp_get_vdec_capa(struct mtk_vcodec_fw *fw)
{
	return MTK_VDEC_FORMAT_MM21 | MTK_VDEC_FORMAT_H264_SLICE | MTK_VDEC_FORMAT_VP9_FRAME |
	       MTK_VDEC_FORMAT_AV1_FRAME | MTK_VDEC_FORMAT_HEVC_FRAME |
	       MTK_VDEC_IS_SUPPORT_10BIT | MTK_VDEC_IS_SUPPORT_EXT;
}

static void *mtk_vcodec_vcp_dm_addr(struct mtk_vcodec_fw *fw, u32 dtcm_dmem_addr)
{
	return NULL;
}

static int mtk_vcodec_vcp_set_ipi_register(struct mtk_vcodec_fw *fw, int id,
					   mtk_vcodec_ipi_handler handler,
					   const char *name, void *priv)
{
	return mtk_vcodec_vcp_ipi_register(fw->vcp, id, handler, priv);
}

static int mtk_vcodec_vcp_ipi_send(struct mtk_vcodec_fw *fw, int id, void *buf,
				   unsigned int len, unsigned int wait)
{
	return mtk_vcodec_vcp_msg_ipi_send(fw, id, buf, len, wait);
}

static void mtk_vcodec_vcp_release(struct mtk_vcodec_fw *fw)
{
	struct mtk_vcp_device *vcp_device = fw->vcp->vcp_device;
	struct device *dev = &fw->pdev->dev;
	int ret;

	if (!fw->vcp->vcp_device) {
		dev_err(dev, "vcp device is null\n");
		return;
	}

	if (fw->vcp->is_ipi_registered) {
		mtk_vcp_mbox_ipc_unregister(vcp_get_ipidev(vcp_device), fw->vcp->mailbox_id);
		fw->vcp->is_ipi_registered = false;
	}

	if (fw->vcp->msg_thread) {
		kthread_stop(fw->vcp->msg_thread);
		fw->vcp->msg_thread = NULL;
	}

	if (fw->vcp->is_notify_registered) {
		vcp_device->ops->unregister_notify(vcp_device, fw->vcp->feature_id,
						       &fw->vcp->vcp_notify);
		fw->vcp->is_notify_registered = false;
	}

	if (!fw->vcp->is_register_done) {
		fw->vcp->is_init_done = false;
		goto put_device;
	}

	ret = vcp_device->ops->deregister_feature(vcp_device, fw->vcp->feature_id);
	if (ret < 0) {
		dev_err(dev, "deregister feature_id(%d) fail(%d)\n", fw->vcp->feature_id, ret);
		return;
	}

	fw->vcp->is_register_done = false;
	fw->vcp->is_init_done = false;

put_device:
	mtk_vcodec_vcp_put_device(fw);
}

static const struct mtk_vcodec_fw_ops mtk_vcodec_vcp_msg = {
	.load_firmware = mtk_vcodec_vcp_load_firmware,
	.get_vdec_capa = mtk_vcodec_vcp_get_vdec_capa,
	.map_dm_addr = mtk_vcodec_vcp_dm_addr,
	.ipi_register = mtk_vcodec_vcp_set_ipi_register,
	.ipi_send = mtk_vcodec_vcp_ipi_send,
	.release = mtk_vcodec_vcp_release,
};

struct mtk_vcodec_fw *mtk_vcodec_fw_vcp_init(void *priv, enum mtk_vcodec_fw_use fw_use)
{
	struct mtk_vcp_msg_node *msg_node;
	struct platform_device *plat_dev;
	struct mtk_vcodec_fw *fw;
	int i;

	if (fw_use == ENCODER) {
		struct mtk_vcodec_enc_dev *enc_dev = priv;

		plat_dev = enc_dev->plat_dev;
	} else if (fw_use == DECODER) {
		struct mtk_vcodec_dec_dev *dec_dev = priv;

		plat_dev = dec_dev->plat_dev;
	} else {
		pr_err("Invalid fw_use %d (use a reasonable fw id here)\n", fw_use);
		return ERR_PTR(-EINVAL);
	}

	fw = devm_kzalloc(&plat_dev->dev, sizeof(*fw), GFP_KERNEL);
	if (!fw)
		return ERR_PTR(-ENOMEM);

	fw->type = VCP;
	fw->pdev = plat_dev;
	fw->fw_use = fw_use;
	fw->ops = &mtk_vcodec_vcp_msg;
	fw->vcp = devm_kzalloc(&plat_dev->dev, sizeof(*fw->vcp), GFP_KERNEL);
	if (!fw->vcp)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&fw->vcp->msg_queue.msg_list);
	INIT_LIST_HEAD(&fw->vcp->msg_queue.node_list);
	spin_lock_init(&fw->vcp->msg_queue.lock);
	init_waitqueue_head(&fw->vcp->msg_queue.wq);
	atomic_set(&fw->vcp->msg_queue.cnt, 0);
	fw->vcp->pdev = plat_dev;

	for (i = 0; i < VCP_MAX_MQ_NODE_CNT; i++) {
		msg_node = devm_kzalloc(&plat_dev->dev, sizeof(*msg_node), GFP_KERNEL);
		if (!msg_node)
			return ERR_PTR(-ENOMEM);

		list_add(&msg_node->list, &fw->vcp->msg_queue.node_list);
	}

	return fw;
}
EXPORT_SYMBOL_GPL(mtk_vcodec_fw_vcp_init);
