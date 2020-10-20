/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2019-2020 Xilinx, Inc.
 * Copyright(c) 2019 Solarflare Communications Inc.
 *
 * This software was jointly developed between OKTET Labs (under contract
 * for Solarflare) and Solarflare Communications, Inc.
 */

#include <rte_service.h>
#include <rte_service_component.h>

#include "sfc_log.h"
#include "sfc_service.h"
#include "sfc_repr_proxy.h"
#include "sfc_repr_proxy_api.h"
#include "sfc.h"
#include "sfc_ev.h"
#include "sfc_rx.h"
#include "sfc_tx.h"

static struct sfc_repr_proxy *
sfc_repr_proxy_by_adapter(struct sfc_adapter *sa)
{
	return &sa->repr_proxy;
}

static struct sfc_adapter *
sfc_get_adapter_by_pf_port_id(uint16_t pf_port_id)
{
	struct rte_eth_dev *dev = &rte_eth_devices[pf_port_id];
	struct sfc_adapter *sa = sfc_adapter_by_eth_dev(dev);
	struct sfc_repr_proxy *rp = sfc_repr_proxy_by_adapter(sa);

	if (sfc_adapter_is_locked(sa)) {
		rp->lock_acquired = false;
	} else {
		sfc_adapter_lock(sa);
		rp->lock_acquired = true;
	}

	return sa;
}

static void
sfc_put_adapter(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = sfc_repr_proxy_by_adapter(sa);

	if (rp->lock_acquired)
		sfc_adapter_unlock(sa);

	rp->lock_acquired = false;
}

static int32_t
sfc_repr_proxy_routine(void *arg)
{
	struct sfc_repr_proxy *rp = arg;
	unsigned int i;

	for (i = 0; i < rp->num_ports; i++) {
		struct sfc_repr_proxy_port *port = &rp->ports[i];
		struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;

		/* FIXME: thread safety */
		if (port->txq[0].ring == NULL)
			continue;

		if (txq->available < RTE_DIM(txq->tx_pkts)) {
			txq->available += rte_ring_sc_dequeue_burst(port->txq[0].ring,
					(void **)(&txq->tx_pkts[txq->available]),
					RTE_DIM(txq->tx_pkts) - txq->available, NULL);
			if (txq->available == txq->transmitted)
				continue;
		}

		txq->transmitted += txq->pkt_burst(txq->dp,
				&txq->tx_pkts[txq->transmitted],
				txq->available - txq->transmitted);
		if (txq->available == txq->transmitted) {
			txq->available = 0;
			txq->transmitted = 0;
		}
	}

	return 0;
}

static struct sfc_txq_info *
sfc_repr_proxy_txq_info_get(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared *sas = sfc_sa2shared(sa);

	return &sas->txq_info[sa->repr_proxy.dp_txq.sw_index];
}

static int
sfc_repr_proxy_txq_attach(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared *sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;
	int sw_index = sfc_repr_txq_sw_index(sas);

	sfc_log_init(sa, "entry");

	SFC_ASSERT(sw_index >= 0);
	txq->sw_index = sw_index;

	return 0;
}

static void
sfc_repr_proxy_txq_detach(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;

	sfc_log_init(sa, "entry");

	txq->sw_index = 0;
}

int
sfc_repr_proxy_txq_init(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;
	const struct rte_eth_txconf tx_conf = {
		.tx_free_thresh = SFC_REPR_PROXY_TXQ_REFILL_LEVEL,
	};
	struct sfc_txq_info *txq_info;

	if (!sfc_repr_supported(sas))
		return 0;

	txq_info = &sfc_sa2shared(sa)->txq_info[txq->sw_index];
	if (txq_info->state == SFC_TXQ_INITIALIZED)
		return 0;

	sfc_log_init(sa, "entry");

	sfc_tx_qinit_info(sa, txq->sw_index);

	return sfc_tx_qinit(sa, txq->sw_index,
			    SFC_REPR_PROXY_TX_DESC_COUNT, sa->socket_id,
			    &tx_conf);
}

void
sfc_repr_proxy_txq_fini(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;
	struct sfc_txq_info *txq_info;

	if (!sfc_repr_supported(sas))
		return;

	txq_info = &sfc_sa2shared(sa)->txq_info[txq->sw_index];
	if (txq_info->state != SFC_TXQ_INITIALIZED)
		return;

	sfc_log_init(sa, "entry");

	sfc_tx_qfini(sa, txq->sw_index);
}

static int
sfc_repr_proxy_txq_start(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_txq *txq = &rp->dp_txq;

	sfc_log_init(sa, "entry");

	txq->dp = sfc_repr_proxy_txq_info_get(sa)->dp;
	txq->pkt_burst = sa->eth_dev->tx_pkt_burst;
	txq->available = 0;
	txq->transmitted = 0;

	return 0;
}

static void
sfc_repr_proxy_txq_stop(__rte_unused struct sfc_adapter *sa)
{
}

static int
sfc_repr_proxy_rxq_attach(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	int sw_index = sfc_repr_rxq_sw_index(sas);

	sfc_log_init(sa, "entry");

	SFC_ASSERT(sw_index >= 0);
	rp->dp_rxq.sw_index = sw_index;

	return 0;
}

static void
sfc_repr_proxy_rxq_detach(struct sfc_adapter *sa)
{
	sfc_log_init(sa, "entry");
}

int
sfc_repr_proxy_rxq_init(struct sfc_adapter *sa, struct rte_mempool *mp)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	uint16_t nb_rx_desc = SFC_REPR_PROXY_RX_DESC_COUNT;
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_rxq *rxq = &rp->dp_rxq;
	struct sfc_rxq_info *rxq_info;
	struct rte_eth_rxconf rxconf = {
		.rx_free_thresh = SFC_REPR_PROXY_RXQ_REFILL_LEVEL,
		.rx_drop_en = 1,
	};
	int rc;

	if (!sfc_repr_supported(sas))
		return 0;

	rxq_info = &sas->rxq_info[rxq->sw_index];
	if (rxq_info->state & SFC_RXQ_INITIALIZED)
		return 0;

	sfc_log_init(sa, "entry");

	nb_rx_desc = RTE_MIN(nb_rx_desc, sa->rxq_max_entries);
	nb_rx_desc = RTE_MAX(nb_rx_desc, sa->rxq_min_entries);

	rc = sfc_rx_qinit_info(sa, rxq->sw_index, EFX_RXQ_FLAG_INGRESS_MPORT);
	if (rc != 0)
		goto fail_repr_rxq_init_info;

	rc = sfc_rx_qinit(sa, rxq->sw_index, nb_rx_desc, sa->socket_id, &rxconf,
			  mp);
	if (rc != 0)
		goto fail_repr_rxq_init;

	return 0;

fail_repr_rxq_init:
fail_repr_rxq_init_info:
	sfc_log_init(sa, "failed %d", rc);

	return rc;
}

void
sfc_repr_proxy_rxq_fini(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_rxq *rxq = &rp->dp_rxq;
	struct sfc_rxq_info *rxq_info;

	if (!sfc_repr_supported(sas))
		return;

	rxq_info = &sas->rxq_info[rxq->sw_index];
	if (rxq_info->state != SFC_RXQ_INITIALIZED)
		return;

	sfc_rx_qfini(sa, rxq->sw_index);
}

static int
sfc_repr_proxy_rxq_start(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_dp_rxq *rxq = &rp->dp_rxq;
	int rc;

	sfc_log_init(sa, "entry");

	rc = sfc_repr_proxy_rxq_init(sa, rp->dp_rxq.mp);
	if (rc != 0)
		goto fail_init;

	rc = sfc_rx_qstart(sa, rxq->sw_index);
	if (rc != 0)
		goto fail_start;

	return 0;

fail_start:
	sfc_repr_proxy_rxq_fini(sa);

fail_init:
	return rc;
}

static void
sfc_repr_proxy_rxq_stop(struct sfc_adapter *sa)
{
	sfc_log_init(sa, "entry");

	sfc_rx_qstop(sa, sa->repr_proxy.dp_rxq.sw_index);
	sfc_repr_proxy_rxq_fini(sa);
}

static int
sfc_repr_proxy_mae_rule_insert(struct sfc_adapter *sa, uint16_t repr_id)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	efx_mport_sel_t mport_alias_selector;
	efx_mport_sel_t mport_vf_selector;
	struct sfc_mae_rule *mae_rule;
	int rc;

	rc = efx_mae_mport_by_id(&rp->ports[repr_id].egress_mport,
				 &mport_vf_selector);
	if (rc != 0)
		goto fail_get_vf;

	rc = efx_mae_mport_by_id(&rp->mport_alias, &mport_alias_selector);
	if (rc != 0)
		goto fail_get_alias;

	rc = sfc_mae_rule_add_mport_match_deliver(sa, &mport_vf_selector,
						  &mport_alias_selector, -1,
						  &mae_rule);
	if (rc != 0)
		goto fail_rule_add;

	rp->ports[repr_id].mae_rule = mae_rule;

	return 0;

fail_rule_add:
fail_get_alias:
fail_get_vf:
	return rc;
}

static void
sfc_repr_proxy_mae_rule_remove(struct sfc_adapter *sa, uint16_t repr_id)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_mae_rule *mae_rule = rp->ports[repr_id].mae_rule;

	sfc_mae_rule_del(sa, mae_rule);
}

static int
sfc_repr_proxy_mport_filter_insert(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_rxq *rxq_ctrl = &sa->rxq_ctrl[rp->dp_rxq.sw_index];
	struct sfc_repr_proxy_filter *filter = &rp->mport_filter;
	efx_mport_sel_t mport_alias_selector;
	static const efx_filter_match_flags_t flags[RTE_DIM(filter->specs)] = {
		EFX_FILTER_MATCH_UNKNOWN_UCAST_DST,
		EFX_FILTER_MATCH_UNKNOWN_MCAST_DST };
	unsigned int i;
	int rc;

	rc = efx_mae_mport_by_id(&rp->mport_alias, &mport_alias_selector);
	if (rc != 0)
		goto fail_get_selector;

	memset(filter->specs, 0, sizeof(filter->specs));
	for (i = 0; i < RTE_DIM(filter->specs); i++) {
		filter->specs[i].efs_priority = EFX_FILTER_PRI_MANUAL;
		filter->specs[i].efs_flags = EFX_FILTER_FLAG_RX;
		filter->specs[i].efs_dmaq_id = rxq_ctrl->hw_index;
		filter->specs[i].efs_match_flags = flags[i] |
				EFX_FILTER_MATCH_MPORT;
		filter->specs[i].efs_ingress_mport = mport_alias_selector.sel;

		rc = efx_filter_insert(sa->nic, &filter->specs[i]);
		if (rc != 0)
			goto fail_insert;
	}

	return 0;

fail_insert:
	while (i-- > 0)
		efx_filter_remove(sa->nic, &filter->specs[i]);

fail_get_selector:
	return rc;
}

static void
sfc_repr_proxy_mport_filter_remove(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_repr_proxy_filter *filter = &rp->mport_filter;
	unsigned int i;

	for (i = 0; i < RTE_DIM(filter->specs); i++)
		efx_filter_remove(sa->nic, &filter->specs[i]);
}

static int
sfc_repr_proxy_port_rule_insert(struct sfc_adapter *sa, uint16_t repr_id)
{
	int rc;

	rc = sfc_repr_proxy_mae_rule_insert(sa, repr_id);
	if (rc != 0)
		goto fail_mae_rule_insert;

	return 0;

fail_mae_rule_insert:
	return rc;
}

static void
sfc_repr_proxy_port_rule_remove(struct sfc_adapter *sa, uint16_t repr_id)
{
	sfc_repr_proxy_mae_rule_remove(sa, repr_id);
}

static int
sfc_repr_proxy_ports_init(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct sfc_sriov *sriov = &sa->sriov;
	unsigned int i;
	int rc;

	rp->ports = rte_calloc_socket("sfc-repr-proxy-ports", sriov->num_vfs,
				     sizeof(*rp->ports), 0, sa->socket_id);
	if (rp->ports == NULL) {
		rc = ENOMEM;
		goto fail_alloc_port;
	}
	rp->num_ports = sriov->num_vfs;

	for (i = 0; i < rp->num_ports; i++)
		rp->ports[i].rte_port_id = RTE_MAX_ETHPORTS;

	rc = efx_mae_mport_alloc_alias(sa->nic, &rp->mport_alias, NULL);
	if (rc != 0)
		goto fail_alloc_mport_alias;

	return 0;

fail_alloc_mport_alias:
	rte_free(rp->ports);
	rp->ports = NULL;

fail_alloc_port:
	return rc;
}

static void
sfc_repr_proxy_ports_fini(struct sfc_adapter *sa)
{
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	unsigned int i;

	for (i = 0; i < rp->num_ports; i++) {
		struct sfc_repr_proxy_port *port = &rp->ports[i];

		if (port->rte_port_id != RTE_MAX_ETHPORTS) {
			rte_eth_dev_stop(port->rte_port_id);
			rte_eth_dev_close(port->rte_port_id);
		}
	}

	efx_mae_mport_free(sa->nic, &rp->mport_alias);
	rte_free(rp->ports);
	rp->ports = NULL;
}

int
sfc_repr_proxy_attach(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	struct rte_service_spec service;
	uint32_t cid;
	uint32_t sid;
	int rc;

	if (!sfc_repr_supported(sas))
		return 0;

	rc = sfc_repr_proxy_rxq_attach(sa);
	if (rc != 0)
		goto fail_rxq_attach;

	rc = sfc_repr_proxy_txq_attach(sa);
	if (rc != 0)
		goto fail_txq_attach;

	rc = sfc_repr_proxy_ports_init(sa);
	if (rc != 0)
		goto fail_ports_init;

	cid = sfc_get_service_lcore(sa->socket_id);
	if (cid == RTE_MAX_LCORE && sa->socket_id != SOCKET_ID_ANY) {
		/* Warn and try to allocate on any NUMA node */
		sfc_warn(sa,
			"repr proxy: unable to get service lcore at socket %d",
			sa->socket_id);

		cid = sfc_get_service_lcore(SOCKET_ID_ANY);
	}
	if (cid == RTE_MAX_LCORE) {
		rc = ENOTSUP;
		sfc_err(sa, "repr proxy: failed to get service lcore");
		goto fail_get_service_lcore;
	}

	memset(&service, 0, sizeof(service));
	snprintf(service.name, sizeof(service.name),
		 "net_sfc_%hu_repr_proxy", sfc_sa2shared(sa)->port_id);
	service.socket_id = rte_lcore_to_socket_id(cid);
	service.callback = sfc_repr_proxy_routine;
	service.callback_userdata = rp;

	rc = rte_service_component_register(&service, &sid);
	if (rc != 0) {
		rc = ENOEXEC;
		sfc_err(sa, "repr proxy: failed to register service component");
		goto fail_register;
	}

	rc = rte_service_map_lcore_set(sid, cid, 1);
	if (rc != 0) {
		rc = -rc;
		sfc_err(sa, "repr proxy: failed to map lcore");
		goto fail_map_lcore;
	}

	rp->service_core_id = cid;
	rp->service_id = sid;

	return 0;

fail_map_lcore:
	rte_service_component_unregister(sid);

fail_register:
	/*
	 * No need to rollback service lcore get since
	 * it just makes socket_id based search and remembers it.
	 */

fail_get_service_lcore:
	sfc_repr_proxy_ports_fini(sa);

fail_ports_init:
	sfc_repr_proxy_txq_detach(sa);

fail_txq_attach:
	sfc_repr_proxy_rxq_detach(sa);

fail_rxq_attach:
	return rc;
}

void
sfc_repr_proxy_detach(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;

	if (!sfc_repr_supported(sas))
		return;

	rte_service_map_lcore_set(rp->service_id, rp->service_core_id, 0);
	rte_service_component_unregister(rp->service_id);
	sfc_repr_proxy_ports_fini(sa);
	sfc_repr_proxy_rxq_detach(sa);
	sfc_repr_proxy_txq_detach(sa);
}

static int
sfc_repr_proxy_do_start_id(struct sfc_adapter *sa, uint16_t repr_id)
{
	int rc;

	rc = sfc_repr_proxy_port_rule_insert(sa, repr_id);
	if (rc != 0)
		goto fail_filter_insert;

	return 0;

fail_filter_insert:
	return rc;
}

static void
sfc_repr_proxy_do_stop_id(struct sfc_adapter *sa, uint16_t repr_id)
{
	sfc_repr_proxy_port_rule_remove(sa, repr_id);
}

static bool
sfc_repr_proxy_port_enabled(struct sfc_repr_proxy_port *port)
{
	return port->rte_port_id != RTE_MAX_ETHPORTS && port->enabled;
}

static bool
sfc_repr_proxy_ports_disabled(struct sfc_repr_proxy *rp)
{
	unsigned int i;

	for (i = 0; i < rp->num_ports; i++) {
		if (sfc_repr_proxy_port_enabled(&rp->ports[i]))
			break;
	}

	return i == rp->num_ports;
}

int
sfc_repr_proxy_start(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	unsigned int port_i;
	int rc;

	/* Representor proxy is not started when no representors are started */
	if (!sfc_repr_supported(sas) || sfc_repr_proxy_ports_disabled(rp))
		return 0;

	rc = sfc_repr_proxy_rxq_start(sa);
	if (rc != 0)
		goto fail_rxq_start;

	rc = sfc_repr_proxy_txq_start(sa);
	if (rc != 0)
		goto fail_txq_start;

	/* Service core may be in "stopped" state, start it */
	rc = rte_service_lcore_start(rp->service_core_id);
	if (rc != 0 && rc != -EALREADY) {
		rc = -rc;
		sfc_err(sa, "Failed to start service core for %s: %s",
			rte_service_get_name(rp->service_id),
			rte_strerror(rc));
		goto fail_start_core;
	}

	/* Run the service */
	rc = rte_service_component_runstate_set(rp->service_id, 1);
	if (rc < 0) {
		rc = -rc;
		sfc_err(sa, "Failed to run %s component: %s",
			rte_service_get_name(rp->service_id),
			rte_strerror(rc));
		goto fail_component_runstate_set;
	}
	rc = rte_service_runstate_set(rp->service_id, 1);
	if (rc < 0) {
		rc = -rc;
		sfc_err(sa, "Failed to run %s: %s",
			rte_service_get_name(rp->service_id),
			rte_strerror(rc));
		goto fail_runstate_set;
	}

	for (port_i = 0; port_i < rp->num_ports; port_i++) {
		if (!sfc_repr_proxy_port_enabled(&rp->ports[port_i]))
			continue;

		rc = sfc_repr_proxy_do_start_id(sa, port_i);
		if (rc != 0)
			goto fail_start_id;
	}

	rc = sfc_repr_proxy_mport_filter_insert(sa);
	if (rc != 0)
		goto fail_mport_filter_insert;

	return 0;

fail_mport_filter_insert:
fail_start_id:
	while (port_i-- > 0) {
		if (sfc_repr_proxy_port_enabled(&rp->ports[port_i]))
			sfc_repr_proxy_do_stop_id(sa, port_i);
	}

	rte_service_runstate_set(rp->service_id, 0);

fail_runstate_set:
	rte_service_component_runstate_set(rp->service_id, 0);

fail_component_runstate_set:
	/* Service lcore may be shared and we never stop it */

fail_start_core:
	sfc_repr_proxy_txq_stop(sa);

fail_txq_start:
	sfc_repr_proxy_rxq_stop(sa);

fail_rxq_start:
	return rc;
}

void
sfc_repr_proxy_stop(struct sfc_adapter *sa)
{
	struct sfc_adapter_shared * const sas = sfc_sa2shared(sa);
	struct sfc_repr_proxy *rp = &sa->repr_proxy;
	const unsigned int wait_ms_total = 10000;
	unsigned int i;
	int rc;

	if (!sfc_repr_supported(sas) || sfc_repr_proxy_ports_disabled(rp))
		return;

	for (i = 0; i < rp->num_ports; i++) {
		if (!sfc_repr_proxy_port_enabled(&rp->ports[i]))
			continue;

		sfc_repr_proxy_do_stop_id(sa, i);
	}

	sfc_repr_proxy_mport_filter_remove(sa);

	rc = rte_service_runstate_set(rp->service_id, 0);
	if (rc < 0) {
		sfc_err(sa, "Failed to stop %s: %s",
			rte_service_get_name(rp->service_id),
			rte_strerror(-rc));
	}

	rc = rte_service_component_runstate_set(rp->service_id, 0);
	if (rc < 0) {
		sfc_err(sa, "Failed to stop %s component: %s",
			rte_service_get_name(rp->service_id),
			rte_strerror(-rc));
	}

	/* Service lcore may be shared and we never stop it */

	/*
	 * Wait for the representor proxy routine to finish the last iteration.
	 * Give up on timeout.
	 */
	for (i = 0; i < wait_ms_total; i++) {
		if (rte_service_may_be_active(rp->service_id) == 0)
			break;

		rte_delay_ms(1);
	}
	sfc_repr_proxy_rxq_stop(sa);
	sfc_repr_proxy_txq_stop(sa);
}

int
sfc_repr_proxy_add_port(uint16_t pf_port_id, uint16_t repr_id,
			uint16_t rte_port_id, const efx_mport_sel_t *mport_sel)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_adapter *sa;
	int rc;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	port = &sfc_repr_proxy_by_adapter(sa)->ports[repr_id];

	if (port->rte_port_id != RTE_MAX_ETHPORTS) {
		rc = EEXIST;
		goto fail_port_exists;
	}

	rc = efx_mae_mport_id_by_selector(sa->nic, mport_sel,
					  &port->egress_mport);
	if (rc != 0)
		goto fail_mport_id;

	port->rte_port_id = rte_port_id;

	sfc_put_adapter(sa);

	return 0;

fail_mport_id:
fail_port_exists:
	sfc_put_adapter(sa);

	return rc;
}

int
sfc_repr_proxy_del_port(uint16_t pf_port_id, uint16_t repr_id)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_adapter *sa;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	port = &sfc_repr_proxy_by_adapter(sa)->ports[repr_id];

	if (port->rte_port_id == RTE_MAX_ETHPORTS) {
		sfc_put_adapter(sa);
		return ENOENT;
	}

	port->egress_mport.id = EFX_MPORT_NULL;
	port->rte_port_id = RTE_MAX_ETHPORTS;

	sfc_put_adapter(sa);

	return 0;
}

int
sfc_repr_proxy_add_rxq(uint16_t pf_port_id, uint16_t repr_id,
		       uint16_t queue_id, struct rte_ring *rx_ring,
		       struct rte_mempool *mp)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy_rxq *rxq;
	struct sfc_repr_proxy *rp;
	struct sfc_adapter *sa;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	rp = sfc_repr_proxy_by_adapter(sa);
	port = &rp->ports[repr_id];
	rxq = &port->rxq[queue_id];

	if (rp->dp_rxq.mp != NULL && rp->dp_rxq.mp != mp) {
		sfc_put_adapter(sa);
		return ENOTSUP;
	}

	rxq->ring = rx_ring;
	rxq->mb_pool = mp;
	rp->dp_rxq.mp = mp;
	rp->dp_rxq.ref_count++;

	sfc_put_adapter(sa);

	return 0;
}

void
sfc_repr_proxy_del_rxq(uint16_t pf_port_id, uint16_t repr_id,
		       uint16_t queue_id)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy_rxq *rxq;
	struct sfc_repr_proxy *rp;
	struct sfc_adapter *sa;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	rp = sfc_repr_proxy_by_adapter(sa);
	port = &rp->ports[repr_id];
	rxq = &port->rxq[queue_id];

	rxq->ring = NULL;
	rxq->mb_pool = NULL;
	rp->dp_rxq.ref_count--;
	if (rp->dp_rxq.ref_count == 0)
		rp->dp_rxq.mp = NULL;

	sfc_put_adapter(sa);
}

int
sfc_repr_proxy_add_txq(uint16_t pf_port_id, uint16_t repr_id,
		       uint16_t queue_id, struct rte_ring *tx_ring,
		       efx_mport_id_t *egress_mport)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy_txq *txq;
	struct sfc_adapter *sa;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	port = &sfc_repr_proxy_by_adapter(sa)->ports[repr_id];
	txq = &port->txq[queue_id];

	txq->ring = tx_ring;

	*egress_mport = port->egress_mport;

	sfc_put_adapter(sa);

	return 0;
}

void
sfc_repr_proxy_del_txq(uint16_t pf_port_id, uint16_t repr_id,
		       uint16_t queue_id)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy_txq *txq;
	struct sfc_adapter *sa;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	port = &sfc_repr_proxy_by_adapter(sa)->ports[repr_id];
	txq = &port->txq[queue_id];

	txq->ring = NULL;

	sfc_put_adapter(sa);
}

int
sfc_repr_proxy_start_id(uint16_t pf_port_id, uint16_t repr_id)
{
	bool proxy_start_required = false;
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy *rp;
	struct sfc_adapter *sa;
	int rc;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	rp = sfc_repr_proxy_by_adapter(sa);
	port = &rp->ports[repr_id];

	if (port->enabled) {
		rc = EALREADY;
		goto fail_already_started;
	}

	if (sa->state == SFC_ADAPTER_STARTED) {
		if (sfc_repr_proxy_ports_disabled(rp)) {
			proxy_start_required = true;
		} else {
			rc = sfc_repr_proxy_do_start_id(sa, repr_id);
			if (rc != 0)
				goto fail_start_id;
		}
	}

	port->enabled = true;

	if (proxy_start_required) {
		rc = sfc_repr_proxy_start(sa);
		if (rc != 0)
			goto fail_proxy_start;
	}

	sfc_put_adapter(sa);

	return 0;

fail_proxy_start:
	port->enabled = false;

fail_start_id:
fail_already_started:
	sfc_put_adapter(sa);

	return rc;
}

void
sfc_repr_proxy_stop_id(uint16_t pf_port_id, uint16_t repr_id)
{
	struct sfc_repr_proxy_port *port;
	struct sfc_repr_proxy *rp;
	struct sfc_adapter *sa;
	unsigned int i;

	sa = sfc_get_adapter_by_pf_port_id(pf_port_id);
	rp = sfc_repr_proxy_by_adapter(sa);
	port = &rp->ports[repr_id];

	if (!port->enabled) {
		sfc_put_adapter(sa);
		return;
	}

	if (sa->state == SFC_ADAPTER_STARTED) {
		bool last_enabled = true;

		for (i = 0; i < rp->num_ports; i++) {
			if (i == repr_id)
				continue;

			if (sfc_repr_proxy_port_enabled(&rp->ports[i])) {
				last_enabled = false;
				break;
			}
		}

		if (last_enabled)
			sfc_repr_proxy_stop(sa);
		else
			sfc_repr_proxy_do_stop_id(sa, repr_id);
	}

	port->enabled = false;

	sfc_put_adapter(sa);
}
