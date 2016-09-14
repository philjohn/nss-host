/*
 **************************************************************************
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/*
 * nss_ipv4.c
 *	NSS IPv4 APIs
 */
#include <linux/sysctl.h>
#include "nss_tx_rx_common.h"

#define NSS_IPV4_TX_MSG_TIMEOUT 1000	/* 1 sec timeout for IPv4 messages */

/*
 * Private data structure for ipv4 configure messages
 */
struct nss_ipv4_cfg_pvt {
	struct semaphore sem;			/* Semaphore structure */
	struct completion complete;		/* completion structure */
	int current_value;			/* valid entry */
	int response;				/* Response from FW */
};

int nss_ipv4_conn_cfg __read_mostly = NSS_DEFAULT_NUM_CONN;
int nss_ipv4_accel_mode_cfg __read_mostly = 1;

static struct nss_ipv4_cfg_pvt i4_conn_cfgp;
static struct nss_ipv4_cfg_pvt i4_accel_mode_cfgp;

/*
 * Callback for conn_sync_many request message.
 */
nss_ipv4_msg_callback_t nss_ipv4_conn_sync_many_msg_cb = NULL;

/*
 * nss_ipv4_max_conn_count()
 *	Return the maximum number of IPv4 connections that the NSS acceleration engine supports.
 */
int nss_ipv4_max_conn_count(void)
{
	return nss_core_max_ipv4_conn_get();
}
EXPORT_SYMBOL(nss_ipv4_max_conn_count);

/*
 * nss_ipv4_driver_conn_sync_update()
 *	Update driver specific information from the messsage.
 */
static void nss_ipv4_driver_conn_sync_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv4_conn_sync *nirs)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;

	/*
	 * Update statistics maintained by NSS driver
	 */
	spin_lock_bh(&nss_top->stats_lock);
	nss_top->stats_ipv4[NSS_STATS_IPV4_ACCELERATED_RX_PKTS] += nirs->flow_rx_packet_count + nirs->return_rx_packet_count;
	nss_top->stats_ipv4[NSS_STATS_IPV4_ACCELERATED_RX_BYTES] += nirs->flow_rx_byte_count + nirs->return_rx_byte_count;
	nss_top->stats_ipv4[NSS_STATS_IPV4_ACCELERATED_TX_PKTS] += nirs->flow_tx_packet_count + nirs->return_tx_packet_count;
	nss_top->stats_ipv4[NSS_STATS_IPV4_ACCELERATED_TX_BYTES] += nirs->flow_tx_byte_count + nirs->return_tx_byte_count;
	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_ipv4_driver_conn_sync_many_update()
 *	Update driver specific information from the conn_sync_many messsage.
 */
static void nss_ipv4_driver_conn_sync_many_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv4_conn_sync_many_msg *nicsm)
{
	int i;

	/*
	 * Sanity check for the stats count
	 */
	if (nicsm->count * sizeof(struct nss_ipv4_conn_sync) >= nicsm->size) {
		nss_warning("%p: stats sync count %u exceeds the size of this msg %u", nss_ctx, nicsm->count, nicsm->size);
		return;
	}

	for (i = 0; i < nicsm->count; i++) {
		nss_ipv4_driver_conn_sync_update(nss_ctx, &nicsm->conn_sync[i]);
	}
}

/*
 * nss_ipv4_driver_node_sync_update)
 *	Update driver specific information from the messsage.
 */
static void nss_ipv4_driver_node_sync_update(struct nss_ctx_instance *nss_ctx, struct nss_ipv4_node_sync *nins)
{
	struct nss_top_instance *nss_top = nss_ctx->nss_top;
	uint32_t i;

	/*
	 * Update statistics maintained by NSS driver
	 */
	spin_lock_bh(&nss_top->stats_lock);
	nss_top->stats_node[NSS_IPV4_RX_INTERFACE][NSS_STATS_NODE_RX_PKTS] += nins->node_stats.rx_packets;
	nss_top->stats_node[NSS_IPV4_RX_INTERFACE][NSS_STATS_NODE_RX_BYTES] += nins->node_stats.rx_bytes;
	nss_top->stats_node[NSS_IPV4_RX_INTERFACE][NSS_STATS_NODE_RX_DROPPED] += nins->node_stats.rx_dropped;
	nss_top->stats_node[NSS_IPV4_RX_INTERFACE][NSS_STATS_NODE_TX_PKTS] += nins->node_stats.tx_packets;
	nss_top->stats_node[NSS_IPV4_RX_INTERFACE][NSS_STATS_NODE_TX_BYTES] += nins->node_stats.tx_bytes;

	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_CREATE_REQUESTS] += nins->ipv4_connection_create_requests;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_CREATE_COLLISIONS] += nins->ipv4_connection_create_collisions;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_CREATE_INVALID_INTERFACE] += nins->ipv4_connection_create_invalid_interface;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_DESTROY_REQUESTS] += nins->ipv4_connection_destroy_requests;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_DESTROY_MISSES] += nins->ipv4_connection_destroy_misses;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_HASH_HITS] += nins->ipv4_connection_hash_hits;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_HASH_REORDERS] += nins->ipv4_connection_hash_reorders;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_FLUSHES] += nins->ipv4_connection_flushes;
	nss_top->stats_ipv4[NSS_STATS_IPV4_CONNECTION_EVICTIONS] += nins->ipv4_connection_evictions;
	nss_top->stats_ipv4[NSS_STATS_IPV4_FRAGMENTATIONS] += nins->ipv4_fragmentations;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_CREATE_REQUESTS] += nins->ipv4_mc_connection_create_requests;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_UPDATE_REQUESTS] += nins->ipv4_mc_connection_update_requests;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_CREATE_INVALID_INTERFACE] += nins->ipv4_mc_connection_create_invalid_interface;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_DESTROY_REQUESTS] += nins->ipv4_mc_connection_destroy_requests;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_DESTROY_MISSES] += nins->ipv4_mc_connection_destroy_misses;
	nss_top->stats_ipv4[NSS_STATS_IPV4_MC_CONNECTION_FLUSHES] += nins->ipv4_mc_connection_flushes;

	for (i = 0; i < NSS_EXCEPTION_EVENT_IPV4_MAX; i++) {
		 nss_top->stats_if_exception_ipv4[i] += nins->exception_events[i];
	}
	spin_unlock_bh(&nss_top->stats_lock);
}

/*
 * nss_ipv4_rx_msg_handler()
 *	Handle NSS -> HLOS messages for IPv4 bridge/route
 */
static void nss_ipv4_rx_msg_handler(struct nss_ctx_instance *nss_ctx, struct nss_cmn_msg *ncm, __attribute__((unused))void *app_data)
{
	struct nss_ipv4_msg *nim = (struct nss_ipv4_msg *)ncm;
	nss_ipv4_msg_callback_t cb;

	BUG_ON(ncm->interface != NSS_IPV4_RX_INTERFACE);

	/*
	 * Sanity check the message type
	 */
	if (ncm->type >= NSS_IPV4_MAX_MSG_TYPES) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_ipv4_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return;
	}

	/*
	 * Trace messages.
	 */
	nss_ipv4_log_rx_msg(nim);

	switch (nim->cm.type) {
	case NSS_IPV4_RX_NODE_STATS_SYNC_MSG:
		/*
		* Update driver statistics on node sync.
		*/
		nss_ipv4_driver_node_sync_update(nss_ctx, &nim->msg.node_stats);
		break;

	case NSS_IPV4_RX_CONN_STATS_SYNC_MSG:
		/*
		 * Update driver statistics on connection sync.
		 */
		nss_ipv4_driver_conn_sync_update(nss_ctx, &nim->msg.conn_stats);
		break;

	case NSS_IPV4_TX_CONN_STATS_SYNC_MANY_MSG:
		/*
		 * Update driver statistics on connection sync many.
		 */
		nss_ipv4_driver_conn_sync_many_update(nss_ctx, &nim->msg.conn_stats_many);
		ncm->cb = (uint32_t)nss_ipv4_conn_sync_many_msg_cb;
		break;
	}

	/*
	 * Update the callback and app_data for NOTIFY messages, IPv4 sends all notify messages
	 * to the same callback/app_data.
	 */
	if (nim->cm.response == NSS_CMM_RESPONSE_NOTIFY) {
		ncm->cb = (uint32_t)nss_ctx->nss_top->ipv4_callback;
		ncm->app_data = (uint32_t)nss_ctx->nss_top->ipv4_ctx;
	}

	/*
	 * Do we have a callback?
	 */
	if (!ncm->cb) {
		return;
	}

	/*
	 * Callback
	 */
	cb = (nss_ipv4_msg_callback_t)ncm->cb;
	cb((void *)ncm->app_data, nim);
}

/*
 * nss_ipv4_tx_with_size()
 *	Transmit an ipv4 message to the FW with a specified size.
 */
nss_tx_status_t nss_ipv4_tx_with_size(struct nss_ctx_instance *nss_ctx, struct nss_ipv4_msg *nim, uint32_t size)
{
	struct nss_ipv4_msg *nim2;
	struct nss_cmn_msg *ncm = &nim->cm;
	struct sk_buff *nbuf;
	int32_t status;

	NSS_VERIFY_CTX_MAGIC(nss_ctx);
	if (unlikely(nss_ctx->state != NSS_CORE_STATE_INITIALIZED)) {
		nss_warning("%p: ipv4 msg dropped as core not ready", nss_ctx);
		return NSS_TX_FAILURE_NOT_READY;
	}

	/*
	 * Sanity check the message
	 */
	if (ncm->interface != NSS_IPV4_RX_INTERFACE) {
		nss_warning("%p: tx request for another interface: %d", nss_ctx, ncm->interface);
		return NSS_TX_FAILURE;
	}

	if (ncm->type >= NSS_IPV4_MAX_MSG_TYPES) {
		nss_warning("%p: message type out of range: %d", nss_ctx, ncm->type);
		return NSS_TX_FAILURE;
	}

	if (nss_cmn_get_msg_len(ncm) > sizeof(struct nss_ipv4_msg)) {
		nss_warning("%p: message length is invalid: %d", nss_ctx, nss_cmn_get_msg_len(ncm));
		return NSS_TX_FAILURE;
	}

	if(size > PAGE_SIZE) {
		nss_warning("%p: tx request size too large: %u", nss_ctx, size);
		return NSS_TX_FAILURE;
	}

	nbuf = dev_alloc_skb(size);
	if (unlikely(!nbuf)) {
		NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_NBUF_ALLOC_FAILS]);
		nss_warning("%p: msg dropped as command allocation failed", nss_ctx);
		return NSS_TX_FAILURE;
	}

	/*
	 * Copy the message to our skb.
	 */
	nim2 = (struct nss_ipv4_msg *)skb_put(nbuf, size);
	memcpy(nim2, nim, sizeof(struct nss_ipv4_msg));

	/*
	 * Trace messages.
	 */
	nss_ipv4_log_tx_msg(nim);

	status = nss_core_send_buffer(nss_ctx, 0, nbuf, NSS_IF_CMD_QUEUE, H2N_BUFFER_CTRL, 0);
	if (status != NSS_CORE_STATUS_SUCCESS) {
		dev_kfree_skb_any(nbuf);
		nss_warning("%p: unable to enqueue IPv4 msg\n", nss_ctx);
		return NSS_TX_FAILURE;
	}

	nss_hal_send_interrupt(nss_ctx, NSS_H2N_INTR_DATA_COMMAND_QUEUE);

	NSS_PKT_STATS_INCREMENT(nss_ctx, &nss_ctx->nss_top->stats_drv[NSS_STATS_DRV_TX_CMD_REQ]);
	return NSS_TX_SUCCESS;
}

/*
 * nss_ipv4_tx()
 *	Transmit an ipv4 message to the FW.
 */
nss_tx_status_t nss_ipv4_tx(struct nss_ctx_instance *nss_ctx, struct nss_ipv4_msg *nim)
{
	return nss_ipv4_tx_with_size(nss_ctx, nim, NSS_NBUF_PAYLOAD_SIZE);
}

/*
 **********************************
 Register/Unregister/Miscellaneous APIs
 **********************************
 */

/*
 * nss_ipv4_notify_register()
 *	Register to received IPv4 events.
 *
 * NOTE: Do we want to pass an nss_ctx here so that we can register for ipv4 on any core?
 */
struct nss_ctx_instance *nss_ipv4_notify_register(nss_ipv4_msg_callback_t cb, void *app_data)
{
	/*
	 * TODO: We need to have a new array in support of the new API
	 * TODO: If we use a per-context array, we would move the array into nss_ctx based.
	 */
	nss_top_main.ipv4_callback = cb;
	nss_top_main.ipv4_ctx = app_data;
	return &nss_top_main.nss[nss_top_main.ipv4_handler_id];
}

/*
 * nss_ipv4_notify_unregister()
 *	Unregister to received IPv4 events.
 *
 * NOTE: Do we want to pass an nss_ctx here so that we can register for ipv4 on any core?
 */
void nss_ipv4_notify_unregister(void)
{
	nss_top_main.ipv4_callback = NULL;
}

/*
 * nss_ipv4_conn_sync_many_notify_register()
 *	Register to receive IPv4 conn_sync_many message response.
 */
void nss_ipv4_conn_sync_many_notify_register(nss_ipv4_msg_callback_t cb)
{
	nss_ipv4_conn_sync_many_msg_cb = cb;
}

/*
 * nss_ipv4_conn_sync_many_notify_unregister()
 *	Unregister to receive IPv4 conn_sync_many message response.
 */
void nss_ipv4_conn_sync_many_notify_unregister(void)
{
	nss_ipv4_conn_sync_many_msg_cb = NULL;
}

/*
 * nss_ipv4_get_mgr()
 *
 * TODO: This only suppports a single ipv4, do we ever want to support more?
 */
struct nss_ctx_instance *nss_ipv4_get_mgr(void)
{
	return (void *)&nss_top_main.nss[nss_top_main.ipv4_handler_id];
}

/*
 * nss_ipv4_register_handler()
 *	Register our handler to receive messages for this interface
 */
void nss_ipv4_register_handler(void)
{
	if (nss_core_register_handler(NSS_IPV4_RX_INTERFACE, nss_ipv4_rx_msg_handler, NULL) != NSS_CORE_STATUS_SUCCESS) {
		nss_warning("IPv4 handler failed to register");
	}
}

/*
 * nss_ipv4_conn_cfg_process()
 *	Process request to configure number of ipv4 connections
 */
static int nss_ipv4_conn_cfg_process(struct nss_ctx_instance *nss_ctx, int conn,
				     void (*cfg_cb)(void *app_data, struct nss_ipv4_msg *nim))
{
	struct nss_ipv4_msg nim;
	struct nss_ipv4_rule_conn_cfg_msg *nirccm;
	nss_tx_status_t nss_tx_status;
	uint32_t sum_of_conn;

	/*
	 * The input should be multiple of 1024.
	 * Input for ipv4 and ipv6 sum together should not exceed 8k
	 * Min. value should be at least 256 connections. This is the
	 * minimum connections we will support for each of them.
	 */
	sum_of_conn = conn + nss_ipv6_conn_cfg;
	if ((conn & NSS_NUM_CONN_QUANTA_MASK) ||
		(sum_of_conn > NSS_MAX_TOTAL_NUM_CONN_IPV4_IPV6) ||
		(conn < NSS_MIN_NUM_CONN)) {
		nss_warning("%p: input supported connections (%d) does not adhere\
				specifications\n1) not multiple of 1024,\n2) is less than \
				min val: %d, OR\n 	IPv4/6 total exceeds %d\n",
				nss_ctx,
				conn,
				NSS_MIN_NUM_CONN,
				NSS_MAX_TOTAL_NUM_CONN_IPV4_IPV6);
		return -EINVAL;
	}

	nss_info("%p: IPv4 supported connections: %d\n", nss_ctx, conn);

	memset(&nim, 0, sizeof(struct nss_ipv4_msg));
	nss_ipv4_msg_init(&nim, NSS_IPV4_RX_INTERFACE, NSS_IPV4_TX_CONN_CFG_RULE_MSG,
		sizeof(struct nss_ipv4_rule_conn_cfg_msg), cfg_cb, NULL);

	nirccm = &nim.msg.rule_conn_cfg;
	nirccm->num_conn = htonl(conn);
	nss_tx_status = nss_ipv4_tx(nss_ctx, &nim);

	if (nss_tx_status != NSS_TX_SUCCESS) {
		nss_warning("%p: nss_tx error setting IPv4 Connections: %d\n",
							nss_ctx,
							conn);
		return -EIO;
	}

	return 0;
}

/*
 * nss_ipv4_conn_cfg_callback()
 *	call back function for the ipv4 connection configuration handler
 */
static void nss_ipv4_conn_cfg_callback(void *app_data, struct nss_ipv4_msg *nim)
{

	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("IPv4 connection configuration failed with error: %d\n", nim->cm.error);
		/*
		 * Error, hence we are not updating the nss_ipv4_conn_cfg
		 * Restore the current_value to its previous state
		 */
		i4_conn_cfgp.response = NSS_FAILURE;
		complete(&i4_conn_cfgp.complete);
		return;
	}

	/*
	 * Sucess at NSS FW, hence updating nss_ipv4_conn_cfg, with the valid value
	 * saved at the sysctl handler.
	 */
	nss_info("IPv4 connection configuration success: %d\n", nim->cm.error);
	i4_conn_cfgp.response = NSS_SUCCESS;
	complete(&i4_conn_cfgp.complete);
}

/*
 * nss_ipv4_conn_cfg_handler()
 *	Sets the number of connections for IPv4
 */
static int nss_ipv4_conn_cfg_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[0];
	int ret = NSS_FAILURE;

	/*
	 * Acquiring semaphore
	 */
	down(&i4_conn_cfgp.sem);

	/*
	 * Take snap shot of current value
	 */
	i4_conn_cfgp.current_value = nss_ipv4_conn_cfg;

	/*
	 * Write the variable with user input
	 */
	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret || (!write)) {
		up(&i4_conn_cfgp.sem);
		return ret;
	}

	/*
	 * Process request to change number of IPv4 connections
	 */
	ret = nss_ipv4_conn_cfg_process(nss_ctx, nss_ipv4_conn_cfg, nss_ipv4_conn_cfg_callback);
	if (ret != 0) {
		goto failure;
	}

	/*
	 * Blocking call, wait till we get ACK for this msg.
	 */
	ret = wait_for_completion_timeout(&i4_conn_cfgp.complete, msecs_to_jiffies(NSS_CONN_CFG_TIMEOUT));
	if (ret == 0) {
		nss_warning("%p: Waiting for ack timed out\n", nss_ctx);
		goto failure;
	}

	/*
	 * ACK/NACK received from NSS FW
	 * If ACK: Callback function will update nss_ipv4_conn_cfg with
	 * i4_conn_cfgp.num_conn_valid, which holds the user input
	 */
	if (NSS_FAILURE == i4_conn_cfgp.response) {
		goto failure;
	}

	up(&i4_conn_cfgp.sem);
	return 0;

failure:
	/*
	 * Restore the current_value to its previous state
	 */
	nss_ipv4_conn_cfg = i4_conn_cfgp.current_value;
	up(&i4_conn_cfgp.sem);
	return -EINVAL;
}

/*
 * nss_ipv4_update_conn_count_cb()
 *	call back function for the ipv4 connection count update handler
 */
static void nss_ipv4_update_conn_count_cb(void *app_data, struct nss_ipv4_msg *nim)
{
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("IPv4 connection count update failed with error: %d\n", nim->cm.error);
		return;
	}

	nss_warning("IPv4 connection count update success: %d\n", nim->cm.error);
}

/*
 * nss_ipv4_update_conn_count()
 *	Sets the maximum number of connections for IPv4
 */
int nss_ipv4_update_conn_count(int ipv4_num_conn)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[0];
	int saved_nss_ipv4_conn_cfg = nss_ipv4_conn_cfg;
	int ret = 0;

	nss_ipv4_conn_cfg = ipv4_num_conn;

	/*
	 * Process request to change number of IPv4 connections
	 */
	ret = nss_ipv4_conn_cfg_process(nss_ctx, nss_ipv4_conn_cfg,
					nss_ipv4_update_conn_count_cb);
	if (ret != 0) {
		nss_ipv4_conn_cfg = saved_nss_ipv4_conn_cfg;
		return ret;
	}

	return 0;
}

/*
 * nss_ipv4_accel_mode_cfg_callback()
 *	call back function for the ipv4 acceleration mode configurate handler
 */
static void nss_ipv4_accel_mode_cfg_callback(void *app_data, struct nss_ipv4_msg *nim)
{
	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		nss_warning("IPv4 acceleration mode configuration failed with error: %d\n", nim->cm.error);
		i4_accel_mode_cfgp.response = NSS_FAILURE;
		complete(&i4_accel_mode_cfgp.complete);
		return;
	}

	nss_info("IPv4 acceleration mode configuration success\n");
	i4_accel_mode_cfgp.response = NSS_SUCCESS;
	complete(&i4_accel_mode_cfgp.complete);
}

/*
 * nss_ipv4_accel_mode_cfg_handler()
 *	Configure acceleration mode for IPv4
 */
static int nss_ipv4_accel_mode_cfg_handler(struct ctl_table *ctl, int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct nss_top_instance *nss_top = &nss_top_main;
	struct nss_ctx_instance *nss_ctx = &nss_top->nss[0];
	struct nss_ipv4_msg nim;
	struct nss_ipv4_accel_mode_cfg_msg *nipcm;
	nss_tx_status_t nss_tx_status;
	int ret = NSS_FAILURE;

	/*
	 * Acquiring semaphore
	 */
	down(&i4_accel_mode_cfgp.sem);

	/*
	 * Take snap shot of current value
	 */
	i4_accel_mode_cfgp.current_value = nss_ipv4_accel_mode_cfg;

	/*
	 * Write the variable with user input
	 */
	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret || (!write)) {
		up(&i4_accel_mode_cfgp.sem);
		return ret;
	}

	memset(&nim, 0, sizeof(struct nss_ipv4_msg));
	nss_ipv4_msg_init(&nim, NSS_IPV4_RX_INTERFACE, NSS_IPV4_TX_ACCEL_MODE_CFG_MSG,
		sizeof(struct nss_ipv4_accel_mode_cfg_msg), nss_ipv4_accel_mode_cfg_callback, NULL);

	nipcm = &nim.msg.accel_mode_cfg;
	nipcm->mode = htonl(nss_ipv4_accel_mode_cfg);
	nss_tx_status = nss_ipv4_tx(nss_ctx, &nim);

	if (nss_tx_status != NSS_TX_SUCCESS) {
		nss_warning("%p: Send acceleration mode message failed\n", nss_ctx);
		goto fail;
	}

	/*
	 * Blocking call, wait till we get ACK for this msg.
	 */
	ret = wait_for_completion_timeout(&i4_accel_mode_cfgp.complete, msecs_to_jiffies(NSS_IPV4_TX_MSG_TIMEOUT));
	if (ret == 0) {
		nss_warning("%p: Waiting for ack timed out\n", nss_ctx);
		goto fail;
	}

	if (NSS_FAILURE == i4_accel_mode_cfgp.response) {
		nss_warning("%p: accel mode configure failed\n", nss_ctx);
		goto fail;
	}

	up(&i4_accel_mode_cfgp.sem);
	return 0;

fail:
	nss_ipv4_accel_mode_cfg = i4_accel_mode_cfgp.current_value;
	up(&i4_accel_mode_cfgp.sem);
	return -EIO;
}

static struct ctl_table nss_ipv4_table[] = {
	{
		.procname		= "ipv4_conn",
		.data			= &nss_ipv4_conn_cfg,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler		= &nss_ipv4_conn_cfg_handler,
	},
	{
		.procname		= "ipv4_accel_mode",
		.data			= &nss_ipv4_accel_mode_cfg,
		.maxlen			= sizeof(int),
		.mode			= 0644,
		.proc_handler		= &nss_ipv4_accel_mode_cfg_handler,
	},
	{ }
};

static struct ctl_table nss_ipv4_dir[] = {
	{
		.procname		= "ipv4cfg",
		.mode			= 0555,
		.child			= nss_ipv4_table,
	},
	{ }
};


static struct ctl_table nss_ipv4_root_dir[] = {
	{
		.procname		= "nss",
		.mode			= 0555,
		.child			= nss_ipv4_dir,
	},
	{ }
};

static struct ctl_table nss_ipv4_root[] = {
	{
		.procname		= "dev",
		.mode			= 0555,
		.child			= nss_ipv4_root_dir,
	},
	{ }
};

static struct ctl_table_header *nss_ipv4_header;

/*
 * nss_ipv4_register_sysctl()
 *	Register sysctl specific to ipv4
 */
void nss_ipv4_register_sysctl(void)
{
	sema_init(&i4_conn_cfgp.sem, 1);
	init_completion(&i4_conn_cfgp.complete);

	sema_init(&i4_accel_mode_cfgp.sem, 1);
	init_completion(&i4_accel_mode_cfgp.complete);

	/*
	 * Register sysctl table.
	 */
	nss_ipv4_header = register_sysctl_table(nss_ipv4_root);
}

/*
 * nss_ipv4_unregister_sysctl()
 *	Unregister sysctl specific to ipv4
 */
void nss_ipv4_unregister_sysctl(void)
{
	/*
	 * Unregister sysctl table.
	 */
	if (nss_ipv4_header) {
		unregister_sysctl_table(nss_ipv4_header);
	}
}

/*
 * nss_ipv4_msg_init()
 *	Initialize IPv4 message.
 */
void nss_ipv4_msg_init(struct nss_ipv4_msg *nim, uint16_t if_num, uint32_t type, uint32_t len,
			nss_ipv4_msg_callback_t cb, void *app_data)
{
	nss_cmn_msg_init(&nim->cm, if_num, type, len, (void *)cb, app_data);
}

EXPORT_SYMBOL(nss_ipv4_tx);
EXPORT_SYMBOL(nss_ipv4_tx_with_size);
EXPORT_SYMBOL(nss_ipv4_notify_register);
EXPORT_SYMBOL(nss_ipv4_notify_unregister);
EXPORT_SYMBOL(nss_ipv4_conn_sync_many_notify_register);
EXPORT_SYMBOL(nss_ipv4_conn_sync_many_notify_unregister);
EXPORT_SYMBOL(nss_ipv4_get_mgr);
EXPORT_SYMBOL(nss_ipv4_register_sysctl);
EXPORT_SYMBOL(nss_ipv4_unregister_sysctl);
EXPORT_SYMBOL(nss_ipv4_msg_init);
EXPORT_SYMBOL(nss_ipv4_update_conn_count);
