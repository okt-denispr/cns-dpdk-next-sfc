/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2012-2018 Solarflare Communications Inc.
 * All rights reserved.
 */

#ifndef _SYS_EFX_CHECK_H
#define	_SYS_EFX_CHECK_H

#include "efsys.h"

/* Any EF10-based NIC family */
#define	EFSYS_OPT_EF10	\
	(EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2)

/*
 * Check that the efsys.h header in client code has a valid combination of
 * EFSYS_OPT_xxx options.
 *
 * NOTE: Keep checks for obsolete options here to ensure that they are removed
 * from client code (and do not reappear in merges from other branches).
 */

#ifdef EFSYS_OPT_FALCON
# error "FALCON is obsolete and is not supported."
#endif

#if EFSYS_OPT_BOOTCFG
/* Support NVRAM based boot config */
# if !EFSYS_OPT_NVRAM
#  error "BOOTCFG requires NVRAM"
# endif
#endif /* EFSYS_OPT_BOOTCFG */

#if EFSYS_OPT_CHECK_REG
/* Verify chip implements accessed registers */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "CHECK_REG requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_CHECK_REG */

#if EFSYS_OPT_DECODE_INTR_FATAL
/* Decode fatal errors */
# if !EFSYS_OPT_SIENA
#  error "INTR_FATAL requires SIENA"
# endif
#endif /* EFSYS_OPT_DECODE_INTR_FATAL */

#if EFSYS_OPT_DIAG
/* Support diagnostic hardware tests */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "DIAG requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_DIAG */

#if EFSYS_OPT_EV_PREFETCH
/* Support optimized EVQ data access */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "EV_PREFETCH requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_EV_PREFETCH */

#ifdef EFSYS_OPT_FALCON_NIC_CFG_OVERRIDE
# error "FALCON_NIC_CFG_OVERRIDE is obsolete and is not supported."
#endif

#if EFSYS_OPT_FILTER
/* Support hardware packet filters */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "FILTER requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_FILTER */

#if EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD
# if !EFSYS_OPT_FILTER
#  error "HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD requires FILTER"
# endif
#endif /* EFSYS_OPT_EF10 */

#if EFSYS_OPT_LOOPBACK
/* Support hardware loopback modes */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "LOOPBACK requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_LOOPBACK */

#ifdef EFSYS_OPT_MAC_FALCON_GMAC
# error "MAC_FALCON_GMAC is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_MAC_FALCON_XMAC
# error "MAC_FALCON_XMAC is obsolete and is not supported."
#endif

#if EFSYS_OPT_MAC_STATS
/* Support MAC statistics */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "MAC_STATS requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_MAC_STATS */

#if EFSYS_OPT_MCDI
/* Support management controller messages */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "MCDI requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_MCDI */

#if (EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
# if !EFSYS_OPT_MCDI
#  error "SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD requires MCDI"
# endif
#endif

#if EFSYS_OPT_MCDI_LOGGING
/* Support MCDI logging */
# if !EFSYS_OPT_MCDI
#  error "MCDI_LOGGING requires MCDI"
# endif
#endif /* EFSYS_OPT_MCDI_LOGGING */

#if EFSYS_OPT_MCDI_PROXY_AUTH
/* Support MCDI proxy authorization */
# if !EFSYS_OPT_MCDI
#  error "MCDI_PROXY_AUTH requires MCDI"
# endif
#endif /* EFSYS_OPT_MCDI_PROXY_AUTH */

#ifdef EFSYS_OPT_MON_LM87
# error "MON_LM87 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_MON_MAX6647
# error "MON_MAX6647 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_MON_NULL
# error "MON_NULL is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_MON_SIENA
#  error "MON_SIENA is obsolete (replaced by MON_MCDI)."
#endif

#ifdef EFSYS_OPT_MON_HUNTINGTON
#  error "MON_HUNTINGTON is obsolete (replaced by MON_MCDI)."
#endif

#if EFSYS_OPT_MON_STATS
/* Support monitor statistics (voltage/temperature) */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "MON_STATS requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_MON_STATS */

#if EFSYS_OPT_MON_MCDI
/* Support Monitor via mcdi */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "MON_MCDI requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_MON_MCDI*/

#if EFSYS_OPT_NAMES
/* Support printable names for statistics */
# if !(EFSYS_OPT_LOOPBACK || EFSYS_OPT_MAC_STATS || EFSYS_OPT_MCDI || \
	EFSYS_MON_STATS || EFSYS_OPT_PHY_STATS || EFSYS_OPT_QSTATS)
#  error "NAMES requires LOOPBACK or xxxSTATS or MCDI"
# endif
#endif /* EFSYS_OPT_NAMES */

#if EFSYS_OPT_NVRAM
/* Support non volatile configuration */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "NVRAM requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_NVRAM */

#if EFSYS_OPT_IMAGE_LAYOUT
/* Support signed image layout handling */
# if !(EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2)
#  error "IMAGE_LAYOUT requires MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_IMAGE_LAYOUT */

#ifdef EFSYS_OPT_NVRAM_FALCON_BOOTROM
# error "NVRAM_FALCON_BOOTROM is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_NVRAM_SFT9001
# error "NVRAM_SFT9001 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_NVRAM_SFX7101
# error "NVRAM_SFX7101 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PCIE_TUNE
# error "PCIE_TUNE is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_BIST
# error "PHY_BIST is obsolete (replaced by BIST)."
#endif

#if EFSYS_OPT_PHY_FLAGS
/* Support PHY flags */
# if !EFSYS_OPT_SIENA
#  error "PHY_FLAGS requires SIENA"
# endif
#endif /* EFSYS_OPT_PHY_FLAGS */

#if EFSYS_OPT_PHY_LED_CONTROL
/* Support for PHY LED control */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "PHY_LED_CONTROL requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_PHY_LED_CONTROL */

#ifdef EFSYS_OPT_PHY_NULL
# error "PHY_NULL is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_PM8358
# error "PHY_PM8358 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_PROPS
# error "PHY_PROPS is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_QT2022C2
# error "PHY_QT2022C2 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_QT2025C
# error "PHY_QT2025C is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_SFT9001
# error "PHY_SFT9001 is obsolete and is not supported."
#endif

#ifdef EFSYS_OPT_PHY_SFX7101
# error "PHY_SFX7101 is obsolete and is not supported."
#endif

#if EFSYS_OPT_PHY_STATS
/* Support PHY statistics */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD)
#  error "PHY_STATS requires SIENA or HUNTINGTON or MEDFORD"
# endif
#endif /* EFSYS_OPT_PHY_STATS */

#ifdef EFSYS_OPT_PHY_TXC43128
# error "PHY_TXC43128 is obsolete and is not supported."
#endif

#if EFSYS_OPT_QSTATS
/* Support EVQ/RXQ/TXQ statistics */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "QSTATS requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_QSTATS */

#ifdef EFSYS_OPT_RX_HDR_SPLIT
# error "RX_HDR_SPLIT is obsolete and is not supported"
#endif

#if EFSYS_OPT_RX_SCALE
/* Support receive scaling (RSS) */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "RX_SCALE requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_RX_SCALE */

#if EFSYS_OPT_RX_SCATTER
/* Support receive scatter DMA */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10 || EFSYS_OPT_RIVERHEAD)
#  error "RX_SCATTER requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_RX_SCATTER */

#ifdef EFSYS_OPT_STAT_NAME
# error "STAT_NAME is obsolete (replaced by NAMES)."
#endif

#if EFSYS_OPT_VPD
/* Support PCI Vital Product Data (VPD) */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "VPD requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_VPD */

#ifdef EFSYS_OPT_WOL
# error "WOL is obsolete and is not supported"
#endif /* EFSYS_OPT_WOL */

#ifdef EFSYS_OPT_MCAST_FILTER_LIST
#  error "MCAST_FILTER_LIST is obsolete and is not supported"
#endif

#if EFSYS_OPT_BIST
/* Support BIST */
# if !(EFSYS_OPT_SIENA || EFSYS_OPT_EF10)
#  error "BIST requires SIENA or HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif /* EFSYS_OPT_BIST */

#if EFSYS_OPT_LICENSING
/* Support MCDI licensing API */
# if !EFSYS_OPT_MCDI
#  error "LICENSING requires MCDI"
# endif
# if !EFSYS_HAS_UINT64
#  error "LICENSING requires UINT64"
# endif
#endif /* EFSYS_OPT_LICENSING */

#if EFSYS_OPT_ALLOW_UNCONFIGURED_NIC
/* Support adapters with missing static config (for factory use only) */
# if !(EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 || EFSYS_OPT_RIVERHEAD)
#  error "ALLOW_UNCONFIGURED_NIC requires MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_ALLOW_UNCONFIGURED_NIC */

#if EFSYS_OPT_RX_PACKED_STREAM
/* Support packed stream mode */
# if !EFSYS_OPT_EF10
#  error "PACKED_STREAM requires HUNTINGTON or MEDFORD or MEDFORD2"
# endif
#endif

#if EFSYS_OPT_RX_ES_SUPER_BUFFER
/* Support equal stride super-buffer mode */
# if !(EFSYS_OPT_MEDFORD2)
#  error "ES_SUPER_BUFFER requires MEDFORD2"
# endif
#endif

/* Support hardware assistance for tunnels */
#if EFSYS_OPT_TUNNEL
# if !(EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 || EFSYS_OPT_RIVERHEAD)
#  error "TUNNEL requires MEDFORD or MEDFORD2 or RIVERHEAD"
# endif
#endif /* EFSYS_OPT_TUNNEL */

#if EFSYS_OPT_FW_SUBVARIANT_AWARE
/* Advertise that the driver is firmware subvariant aware */
# if !(EFSYS_OPT_MEDFORD2)
#  error "FW_SUBVARIANT_AWARE requires MEDFORD2"
# endif
#endif

#endif /* _SYS_EFX_CHECK_H */
