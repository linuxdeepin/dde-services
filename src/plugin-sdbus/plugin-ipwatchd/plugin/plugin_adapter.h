// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef PLUGIN_ADAPTER_H
#define PLUGIN_ADAPTER_H

#include "upstream/ipwatchd.h"
#include <systemd/sd-bus.h>
#include <stdbool.h>

/* Plugin-specific data structures */

//! Structure for IP conflict check context (D-Bus method)
typedef struct
{
    const char* ip;              /**< IP address to check */
    const char* misc;            /**< Device name or empty for auto-select */
    sd_bus_message *msg;         /**< D-Bus message for reply */
    IPWD_S_DEV dev;             /**< Selected device for checking */
    bool wait_reply;            /**< Flag indicating D-Bus call is waiting */
    char *conflic_mac;          /**< MAC address of conflicting device (if found) */
} IPWD_S_CHECK_CONTEXT;

//! Structure for tracking IP conflict information
typedef struct IPCONFLICT_DEV_INFO
{
    char ip[IPWD_MAX_DEVICE_ADDRESS_LEN];          /**< IP address */
    char mac[IPWD_MAX_DEVICE_ADDRESS_LEN];         /**< Local MAC address */
    char remote_mac[IPWD_MAX_DEVICE_ADDRESS_LEN];  /**< Remote MAC address */
    char device[IPWD_MAX_DEVICE_NAME_LEN];         /**< Network device name */
    struct IPCONFLICT_DEV_INFO *next;              /**< Next node in list */
    int signal_count;                               /**< Counter for signal emission */
    time_t last_conflict_time;                      /**< Last time conflict was detected */
    time_t last_probe_time;                         /**< Last time we probed this conflict */
    int probe_no_response_count;                    /**< Consecutive probes with no conflict response */
} IPCONFLICT_DEV_INFO;

/* Plugin adapter functions */

/**
 * Initialize plugin adapter
 * @param config_file Path to ipwatchd configuration file
 * @return 0 on success, negative on error
 */
int ipwd_plugin_init(const char *config_file);

/**
 * Start ipwatchd in plugin mode
 * @return 0 on success, negative on error
 */
int ipwd_plugin_start(void);

/**
 * Stop ipwatchd plugin
 */
void ipwd_plugin_stop(void);

/**
 * D-Bus method: Check for IP conflict
 * @param m D-Bus message
 * @param userdata User data (unused)
 * @param ret_error Error return
 * @return 0 on success
 */
int ipwd_conflict_check(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/**
 * Verify and select device for IP conflict check
 * @return 0 on success, negative on error
 */
int ipwd_check_context_verify(void);

/* Access to plugin-specific data */
extern IPWD_S_CHECK_CONTEXT check_context;
extern IPCONFLICT_DEV_INFO *ipconflict_dev_info;

#endif // PLUGIN_ADAPTER_H
