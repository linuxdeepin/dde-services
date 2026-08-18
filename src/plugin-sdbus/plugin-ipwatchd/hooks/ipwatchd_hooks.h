// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef IPWATCHD_HOOKS_H
#define IPWATCHD_HOOKS_H

/**
 * Hook system for ipwatchd plugin integration
 * 
 * These hooks allow external code to intercept and extend ipwatchd behavior
 * without modifying the core logic. All hooks use weak symbols, so they are
 * optional and have no-op default implementations.
 */

/**
 * Hook called after parsing an ARP packet
 * 
 * @param rcv_sip Source IP address
 * @param rcv_smac Source MAC address
 * @param rcv_dip Destination IP address
 * @param rcv_dmac Destination MAC address
 * @return 0 to continue normal processing, non-zero to skip further processing
 */
int __attribute__((weak)) ipwd_hook_on_arp_packet(
    const char *rcv_sip, const char *rcv_smac,
    const char *rcv_dip, const char *rcv_dmac);

/**
 * Hook called when an IP conflict is detected
 * 
 * @param device Network device name
 * @param ip IP address in conflict
 * @param mac Local MAC address
 * @param remote_mac Remote MAC address causing conflict
 * @param is_active_mode 1 if active mode, 0 if passive mode
 */
void __attribute__((weak)) ipwd_hook_on_conflict(
    const char *device, const char *ip,
    const char *mac, const char *remote_mac,
    int is_active_mode);

/**
 * Hook called when a conflict is resolved
 * 
 * @param device Network device name
 * @param ip IP address
 * @param mac Local MAC address
 * @param remote_mac Remote MAC address
 */
void __attribute__((weak)) ipwd_hook_on_conflict_resolved(
    const char *device, const char *ip,
    const char *mac, const char *remote_mac);

/**
 * Hook called after configuration is loaded
 */
void __attribute__((weak)) ipwd_hook_on_config_loaded(void);

/**
 * Hook called after pcap is initialized and ready
 * 
 * @param pcap_handle Pointer to pcap_t handle
 */
void __attribute__((weak)) ipwd_hook_on_pcap_ready(void *pcap_handle);

#endif // IPWATCHD_HOOKS_H
