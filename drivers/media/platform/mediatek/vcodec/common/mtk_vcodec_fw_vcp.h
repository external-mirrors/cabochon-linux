/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Kyrie Wu <kyrie.wu@mediatek.com>
 */

#ifndef _MTK_VCODEC_FW_VCP_H_
#define _MTK_VCODEC_FW_VCP_H_

typedef void (*vcp_ipi_handler_t) (void *data, unsigned int len, void *priv);

#define VCP_MAX_MQ_NODE_CNT  6
#define VCP_SHARE_BUF_SIZE 64

#define VCODEC_VSI_LEN (0x2000)

/* enum mtk_vcp_ipi_index - index used to separate different hardware */
enum mtk_vcp_ipi_index {
	VCP_IPI_ENCODER,
	VCP_IPI_LAT_DECODER,
	VCP_IPI_CORE_DECODER,
	VCP_IPI_MAX,
};

/**
 * struct mtk_vcp_msg_queue - process the vcp message between kernel with vcp
 *
 * @msg_list:  store share buffer list which from vcp to kernel
 * @wq:        waitqueue that can be used to wait for vcp message
 * @lock:      protect msg list
 * @cnt:       the count of share obj in msg list
 * @node_list: share obj list
 */
struct mtk_vcp_msg_queue {
	struct list_head msg_list;
	wait_queue_head_t wq;
	/* protect msg_list and node_list */
	spinlock_t lock;
	atomic_t cnt;
	struct list_head node_list;
};

/**
 * struct mtk_vcp_ipi_desc - store the ack handler
 *
 * @lock:    protect ack handler data
 * @handler: calling this handler when kernel receive ack
 * @priv:    private data when calling handler to process
 */
struct mtk_vcp_ipi_desc {
	/* protect handler and priv */
	struct mutex lock;
	vcp_ipi_handler_t handler;
	void *priv;
};

/**
 * struct mtk_vcp_share_obj - share buffer used to send data to vcp
 *
 * @id:        message index
 * @len:       message size
 * @share_buf: message data
 */
struct mtk_vcp_share_obj {
	unsigned int id;
	unsigned int len;
	unsigned char share_buf[VCP_SHARE_BUF_SIZE];
};

/* enum mtk_vcp_ipi_msg_status - the status when send message to vcp */
enum mtk_vcp_ipi_msg_status {
	VCODEC_IPI_MSG_STATUS_OK         = 0,
	VCODEC_IPI_MSG_STATUS_FAIL       = -1,
	VCODEC_IPI_MSG_STATUS_MAX_INST   = -2,
	VCODEC_IPI_MSG_STATUS_ILSEQ      = -3,
	VCODEC_IPI_MSG_STATUS_INVALID_ID = -4,
	VCODEC_IPI_MSG_STATUS_DMA_FAIL   = -5,
};

/**
 * struct mtk_vcp_msg_node - share buffer used to send data to vcp
 *
 * @ipi_data: share obj data
 * @list:     list to store msg node
 */
struct mtk_vcp_msg_node {
	struct mtk_vcp_share_obj ipi_data;
	struct list_head list;
};

/**
 * struct mtk_vcp - vcp firmware private data
 *
 * @is_init_done:  vcp is ready to use
 *
 * @ipi_mutex:     used to protect ipi data
 * @msg_signaled:  whether receive ack from vcp
 * @msg_wq:        wake message queue
 *
 * @ipi_desc:      store ack handler
 * @ipi_id_ack:    the ack handler status
 *
 * @msg_queue:     process vcp message
 * @share_data:    temp share obj data
 *
 * @vcp_notify:    register notifier to vcp
 * @msg_thread:    process VCP message queue
 *
 * @vsi_addr:      vsi virtual data address
 * @vsi_core_addr: vsi core virtual data address
 * @iova_addr:     vsi iova address
 * @vsi_size:      vsi size
 *
 * @pdev:          platform device
 * @vcp_device:    vcp private data
 * @feature_id:     registered VCP feature id
 * @mailbox_id:     registered VCP mailbox id
 * @is_register_done: feature registration state
 * @is_ipi_registered: mailbox IPC registration state
 * @is_notify_registered: notifier registration state
 */
struct mtk_vcp {
	bool is_init_done;

	/* serialize ipi message send/receive */
	struct mutex ipi_mutex;
	bool msg_signaled[VCP_IPI_MAX];
	wait_queue_head_t msg_wq[VCP_IPI_MAX];

	struct mtk_vcp_ipi_desc ipi_desc[VCP_IPI_MAX];
	int ipi_id_ack[VCP_IPI_MAX];

	struct mtk_vcp_msg_queue msg_queue;
	struct mtk_vcp_share_obj share_data;

	struct notifier_block vcp_notify;
	struct task_struct *msg_thread;

	void *vsi_addr;
	void *vsi_core_addr;
	dma_addr_t iova_addr;
	int vsi_size;

	struct platform_device *pdev;
	struct mtk_vcp_device *vcp_device;
	int feature_id;
	int mailbox_id;
	bool is_register_done;
	bool is_ipi_registered;
	bool is_notify_registered;
};

#endif
