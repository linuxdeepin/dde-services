// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ipwatchd_hooks.h"

/**
 * Default (weak) implementations of hook functions
 * These will be used when no plugin overrides them
 */

int __attribute__((weak)) ipwd_hook_on_arp_packet(
    const char *rcv_sip, const char *rcv_smac,
    const char *rcv_dip, const char *rcv_dmac)
{
    (void)rcv_sip;
    (void)rcv_smac;
    (void)rcv_dip;
    (void)rcv_dmac;
    return 0; // Continue normal processing
}

void __attribute__((weak)) ipwd_hook_on_conflict(
    const char *device, const char *ip,
    const char *mac, const char *remote_mac,
    int is_active_mode)
{
    (void)device;
    (void)ip;
    (void)mac;
    (void)remote_mac;
    (void)is_active_mode;
    // No-op
}

void __attribute__((weak)) ipwd_hook_on_conflict_resolved(
    const char *device, const char *ip,
    const char *mac, const char *remote_mac)
{
    (void)device;
    (void)ip;
    (void)mac;
    (void)remote_mac;
    // No-op
}

void __attribute__((weak)) ipwd_hook_on_config_loaded(void)
{
    // No-op
}

void __attribute__((weak)) ipwd_hook_on_pcap_ready(void *pcap_handle)
{
    (void)pcap_handle;
    // No-op
}
