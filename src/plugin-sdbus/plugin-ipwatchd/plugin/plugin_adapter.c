// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "plugin_adapter.h"
#include "hooks/ipwatchd_hooks.h"
#include "service.h"
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* Forward declarations */
static void *periodic_probe_thread(void *arg);

/* Plugin-specific global data */
IPWD_S_CHECK_CONTEXT check_context = {0};
IPCONFLICT_DEV_INFO *ipconflict_dev_info = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pcap_t *plugin_pcap_handle = NULL;
static volatile int exit_flag = 0;
static int timeout_ms = 100;
static pthread_t probe_thread;
static volatile int probe_thread_running = 0;

/* Global variables required by ipwatchd core - defined here for plugin */
int debug_flag = 0;
int syslog_flag = 0;
int testing_flag = 0;
IPWD_S_DEVS devices = {0};
IPWD_S_CONFIG config = {0};
pcap_t *h_pcap = NULL;

/* Hook implementations */

/**
 * Hook: Called after parsing ARP packet
 * Handles IP conflict check requests from D-Bus and updates probe counters
 */
int ipwd_hook_on_arp_packet(const char *rcv_sip, const char *rcv_smac,
                             const char *rcv_dip, const char *rcv_dmac)
{
    (void)rcv_dip;
    (void)rcv_dmac;
    
    pthread_mutex_lock(&mutex);
    
    /* Check if we're waiting for a D-Bus reply */
    if (check_context.wait_reply)
    {
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "Hook: Checking packet - waiting for IP:%s, got IP:%s MAC:%s (my MAC:%s)",
                    check_context.ip, rcv_sip, rcv_smac, check_context.dev.mac);
        
        /* Ignore packets from our own MAC address */
        if (strcasecmp(rcv_smac, check_context.dev.mac) == 0)
        {
            ipwd_message(IPWD_MSG_TYPE_DEBUG, "Ignoring packet from our own MAC: %s", rcv_smac);
            pthread_mutex_unlock(&mutex);
            return 0;
        }
        
        /* Check if this packet indicates a conflict */
        if (strcasecmp(rcv_sip, check_context.ip) == 0)
        {
            ipwd_message(IPWD_MSG_TYPE_DEBUG, "!!! IP conflict detected: %s with MAC %s",
                        check_context.ip, rcv_smac);
            check_context.conflic_mac = strdup(rcv_smac);
        }
    }
    
    /* Update probe counters for tracked conflicts */
    IPCONFLICT_DEV_INFO *info = ipconflict_dev_info ? ipconflict_dev_info->next : NULL;
    time_t now = time(NULL);
    
    while (info)
    {
        /* Check if this packet is related to a tracked conflict */
        if (strcasecmp(rcv_sip, info->ip) == 0)
        {
            if (strcasecmp(rcv_smac, info->remote_mac) == 0)
            {
                /* Conflict still exists - reset probe counter */
                info->probe_no_response_count = 0;
                info->last_conflict_time = now;
            }
        }
        
        info = info->next;
    }
    
    pthread_mutex_unlock(&mutex);
    return 0; /* Continue normal processing */
}

/**
 * Hook: Called when IP conflict is detected
 * Manages conflict tracking list and emits D-Bus signal
 */
void ipwd_hook_on_conflict(const char *device, const char *ip,
                           const char *mac, const char *remote_mac,
                           int is_active_mode)
{
    (void)is_active_mode;
    
    pthread_mutex_lock(&mutex);
    
    /* Emit D-Bus signal */
    emit_is_conflict(ip, mac, remote_mac, 1);
    
    /* Check if this conflict is already in the list */
    int exist = 0;
    IPCONFLICT_DEV_INFO *tail = ipconflict_dev_info;
    IPCONFLICT_DEV_INFO *info = ipconflict_dev_info ? ipconflict_dev_info->next : NULL;
    time_t now = time(NULL);
    
    while (info)
    {
        if (strcasecmp(info->ip, ip) == 0 &&
            strcasecmp(info->mac, mac) == 0 &&
            strcasecmp(info->remote_mac, remote_mac) == 0)
        {
            /* Reset counters - conflict still exists */
            if (info->signal_count < 3)
            {
                info->signal_count = 3;
            }
            info->probe_no_response_count = 0;
            info->last_conflict_time = now;
            exist = 1;
            break;
        }
        tail = info;
        info = info->next;
    }
    
    /* Add new conflict to list */
    if (!exist && tail)
    {
        IPCONFLICT_DEV_INFO *newinfo = (IPCONFLICT_DEV_INFO *)malloc(sizeof(IPCONFLICT_DEV_INFO));
        if (newinfo)
        {
            strncpy(newinfo->ip, ip, IPWD_MAX_DEVICE_ADDRESS_LEN - 1);
            strncpy(newinfo->mac, mac, IPWD_MAX_DEVICE_ADDRESS_LEN - 1);
            strncpy(newinfo->remote_mac, remote_mac, IPWD_MAX_DEVICE_ADDRESS_LEN - 1);
            strncpy(newinfo->device, device, IPWD_MAX_DEVICE_NAME_LEN - 1);
            newinfo->signal_count = 3;
            newinfo->last_conflict_time = now;
            newinfo->last_probe_time = 0;
            newinfo->probe_no_response_count = 0;
            newinfo->next = NULL;
            tail->next = newinfo;
        }
    }
    
    pthread_mutex_unlock(&mutex);
}

/**
 * Hook: Called when conflict is resolved
 * Decrements counter and emits resolve signal when appropriate
 */
void ipwd_hook_on_conflict_resolved(const char *device, const char *ip,
                                    const char *mac, const char *remote_mac)
{
    (void)device;
    (void)remote_mac;  /* Not used - we check all conflicts for this device */
    
    pthread_mutex_lock(&mutex);
    
    /* Find and update all conflict entries for this MAC address */
    IPCONFLICT_DEV_INFO *pre = ipconflict_dev_info;
    IPCONFLICT_DEV_INFO *info = pre ? pre->next : NULL;
    
    int processed_count = 0;
    
    while (info)
    {
        /* Match only by MAC address - any non-conflict packet means things are improving */
        if (strcasecmp(info->mac, mac) == 0)
        {
            processed_count++;
            
            /* Check if this specific conflict is resolved:
             * 1. IP changed (local or remote modified IP) - conflict no longer exists
             * 2. Remote MAC changed - remote device changed IP
             */
            int conflict_resolved = 0;
            if (strcasecmp(info->ip, ip) != 0)
            {
                ipwd_message(IPWD_MSG_TYPE_DEBUG, 
                            "Conflict resolved: IP changed from %s to %s (MAC:%s, Remote:%s)",
                            info->ip, ip, info->mac, info->remote_mac);
                conflict_resolved = 1;
            }
            
            if (conflict_resolved)
            {
                /* Immediately resolve - IP changed means conflict is definitely gone */
                ipwd_message(IPWD_MSG_TYPE_DEBUG, 
                            "Emitting IPConflictReslove signal immediately");
                emit_is_conflict(info->ip, info->mac, info->remote_mac, 0);
                
                /* Remove from list */
                IPCONFLICT_DEV_INFO *to_free = info;
                pre->next = info->next;
                info = info->next;
                free(to_free);
                continue;  /* Don't advance pre since we removed current node */
            }
            else
            {
                /* Same IP, just a normal packet - decrement counter (debounce) */
                if (info->signal_count > 0)
                {
                    info->signal_count--;
                }
                
                /* Also increment probe no-response counter - indicates network is healthy */
                info->probe_no_response_count++;
                
                if (info->signal_count == 0)
                {
                    /* Debounce complete - no conflict seen for a while */
                    ipwd_message(IPWD_MSG_TYPE_DEBUG, 
                                "Signal count reached 0! Emitting IPConflictReslove signal for IP:%s Remote:%s", 
                                info->ip, info->remote_mac);
                    emit_is_conflict(info->ip, info->mac, info->remote_mac, 0);
                    
                    /* Remove from list */
                    IPCONFLICT_DEV_INFO *to_free = info;
                    pre->next = info->next;
                    info = info->next;
                    free(to_free);
                    continue;  /* Don't advance pre since we removed current node */
                }
            }
        }
        pre = info;
        info = info->next;
    }
    
    pthread_mutex_unlock(&mutex);
}

/**
 * Clear all conflicts for a specific MAC address
 */
static void clear_conflicts_by_mac(const char *mac)
{
    pthread_mutex_lock(&mutex);
    
    IPCONFLICT_DEV_INFO *pre = ipconflict_dev_info;
    IPCONFLICT_DEV_INFO *info = pre ? pre->next : NULL;
    int cleared_count = 0;
    
    while (info)
    {
        if (strcasecmp(info->mac, mac) == 0)
        {
            ipwd_message(IPWD_MSG_TYPE_DEBUG,
                        "Clearing conflict: IP=%s MAC=%s Remote=%s",
                        info->ip, info->mac, info->remote_mac);
            
            /* Emit resolve signal */
            emit_is_conflict(info->ip, info->mac, info->remote_mac, 0);
            
            /* Remove from list */
            IPCONFLICT_DEV_INFO *to_free = info;
            pre->next = info->next;
            info = info->next;
            cleared_count++;
            free(to_free);
            continue;
        }
        
        pre = info;
        info = info->next;
    }
    
    if (cleared_count > 0)
    {
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "Cleared %d conflicts for MAC %s",
                    cleared_count, mac);
    }
    
    pthread_mutex_unlock(&mutex);
}

/**
 * Hook: Called when a device's IP address changes
 * Clears all conflict entries for the old IP
 */
void ipwd_hook_on_ip_changed(const char *device, const char *old_ip,
                             const char *new_ip, const char *mac)
{
    (void)device;
    
    ipwd_message(IPWD_MSG_TYPE_DEBUG, 
                "IP changed on device %s: %s -> %s (MAC: %s), clearing old conflicts",
                device, old_ip, new_ip, mac);
    
    /* Clear all conflicts for this MAC address */
    clear_conflicts_by_mac(mac);
}

/**
 * Hook: Called after configuration is loaded
 */
void ipwd_hook_on_config_loaded(void)
{
    /* Initialize conflict tracking list */
    if (!ipconflict_dev_info)
    {
        ipconflict_dev_info = (IPCONFLICT_DEV_INFO *)malloc(sizeof(IPCONFLICT_DEV_INFO));
        if (ipconflict_dev_info)
        {
            memset(ipconflict_dev_info, 0, sizeof(IPCONFLICT_DEV_INFO));
            ipconflict_dev_info->next = NULL;
        }
    }
}

/**
 * Hook: Called after pcap is ready
 */
void ipwd_hook_on_pcap_ready(void *pcap_handle)
{
    plugin_pcap_handle = (pcap_t *)pcap_handle;
}

/* Plugin adapter functions */

int ipwd_plugin_init(const char *config_file)
{
    /* Enable debug mode from environment variable or default to on */
    const char *debug_env = getenv("IPWATCHD_DEBUG");
    debug_flag = (debug_env == NULL || atoi(debug_env) != 0) ? 1 : 0;
    
    /* Enable syslog */
    syslog_flag = 1;
    
    /* Open syslog */
    openlog("ipwatchd", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);
    
    /* Check capabilities instead of root */
    /* Note: We rely on CAP_NET_RAW and CAP_NET_ADMIN capabilities
     * which should be set via systemd service or setcap */
    if (getuid() != 0)
    {
        ipwd_message(IPWD_MSG_TYPE_INFO, "Running as non-root user (uid=%d), relying on capabilities", getuid());
    }
    
    /* Read configuration */
    if (ipwd_read_config(config_file) == IPWD_RV_ERROR)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to read configuration file: %s",config_file);
        return -1;
    }
    
    ipwd_message(IPWD_MSG_TYPE_INFO, "IPwatchD plugin initialized");
    return 0;
}

int ipwd_plugin_start(void)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    struct bpf_program fp;
    
    /* Check for "any" pseudodevice */
    pcap_if_t *pcap_alldevs = NULL;
    int any_exists = 0;
    
    if (pcap_findalldevs(&pcap_alldevs, errbuf))
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to get network device list - %s", errbuf);
        return -1;
    }
    
    for (pcap_if_t *pcap_dev = pcap_alldevs; pcap_dev; pcap_dev = pcap_dev->next)
    {
        if (strcasecmp(pcap_dev->name, "any") == 0)
        {
            any_exists = 1;
            break;
        }
    }
    pcap_freealldevs(pcap_alldevs);
    
    if (!any_exists)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Pseudodevice \"any\" not available");
        return -1;
    }
    
    /* Initialize pcap */
    h_pcap = pcap_create("any", errbuf);
    if (!h_pcap)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to create pcap - %s", errbuf);
        return -1;
    }
    
    pcap_set_snaplen(h_pcap, BUFSIZ);
    pcap_set_promisc(h_pcap, 0);
    pcap_set_timeout(h_pcap, timeout_ms);
    
    if (pcap_activate(h_pcap) != 0)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to activate pcap - %s", pcap_geterr(h_pcap));
        pcap_close(h_pcap);
        return -1;
    }
    
    /* Compile and set filter */
    if (pcap_compile(h_pcap, &fp, "arp", 0, 0) == -1)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to compile filter - %s", pcap_geterr(h_pcap));
        return -1;
    }
    
    if (pcap_setfilter(h_pcap, &fp) == -1)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Unable to set filter - %s", pcap_geterr(h_pcap));
        return -1;
    }
    
    pcap_freecode(&fp);
    
    /* Call hook */
    ipwd_hook_on_pcap_ready(h_pcap);
    
    /* Start periodic probe thread */
    probe_thread_running = 1;
    if (pthread_create(&probe_thread, NULL, periodic_probe_thread, NULL) != 0)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Failed to create probe thread");
        probe_thread_running = 0;
    }
    else
    {
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "Periodic probe thread created");
    }
    
    ipwd_message(IPWD_MSG_TYPE_INFO, "IPwatchD plugin started");
    
    /* Main loop */
    while (!exit_flag)
    {
        /* Main loop continuously processes ARP packets */
        /* The ipwd_hook_on_arp_packet callback will update check_context when needed */
        pcap_dispatch(h_pcap, -1, ipwd_analyse, NULL);
        usleep(1000); /* 1ms sleep to free CPU */
    }
    
    /* Stop probe thread */
    if (probe_thread_running)
    {
        probe_thread_running = 0;
        pthread_join(probe_thread, NULL);
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "Probe thread joined");
    }
    
    /* Cleanup */
    pcap_close(h_pcap);
    closelog();
    
    if (config.script)
    {
        free(config.script);
        config.script = NULL;
    }
    
    if (devices.dev)
    {
        free(devices.dev);
        devices.dev = NULL;
    }
    
    /* Free conflict list */
    IPCONFLICT_DEV_INFO *info = ipconflict_dev_info;
    while (info)
    {
        IPCONFLICT_DEV_INFO *tmp = info;
        info = info->next;
        free(tmp);
    }
    
    pthread_mutex_destroy(&mutex);
    
    ipwd_message(IPWD_MSG_TYPE_INFO, "IPwatchD plugin stopped");
    return 0;
}

void ipwd_plugin_stop(void)
{
    exit_flag = 1;
}

int ipwd_check_context_verify(void)
{
    if (!check_context.ip || !check_context.misc)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Invalid check context");
        return -1;
    }
    
    struct in_addr sip, dip;
    if (inet_aton(check_context.ip, &sip) == 0 || sip.s_addr == 0)
    {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Invalid IP address: %s", check_context.ip);
        return -1;
    }
    
    memset(&check_context.dev, 0, sizeof(IPWD_S_DEV));
    check_context.dev.state = IPWD_DEVICE_STATE_UNUSABLE;
    
    /* Auto-select device or use specified one */
    if (check_context.misc[0] == '\0')
    {
        /* Auto-select: prefer same subnet */
        for (int i = 0; i < devices.devnum; i++)
        {
            IPWD_S_DEV dev;
            memset(&dev, 0, sizeof(dev));
            strcpy(dev.device, devices.dev[i].device);
            
            if (ipwd_devinfo(dev.device, dev.ip, dev.mac) == IPWD_RV_ERROR)
                continue;
            
            if (inet_aton(dev.ip, &dip) != 0)
            {
                uint8_t *p = (uint8_t*)&sip.s_addr;
                uint8_t *q = (uint8_t*)&dip.s_addr;
                
                /* Prefer same subnet (first 3 bytes match) */
                if (p[0] == q[0] && p[1] == q[1] && p[2] == q[2])
                {
                    memcpy(&check_context.dev, &dev, sizeof(IPWD_S_DEV));
                    check_context.dev.state = IPWD_DEVICE_STATE_USABLE;
                    break;
                }
                
                /* Fallback to first usable device */
                if (check_context.dev.state == IPWD_DEVICE_STATE_UNUSABLE)
                {
                    memcpy(&check_context.dev, &dev, sizeof(IPWD_S_DEV));
                    check_context.dev.state = IPWD_DEVICE_STATE_USABLE;
                }
            }
        }
    }
    else
    {
        /* Use specified device */
        strncpy(check_context.dev.device, check_context.misc, IPWD_MAX_DEVICE_NAME_LEN - 1);
        for (int i = 0; i < devices.devnum; i++)
        {
            if (strcasecmp(check_context.dev.device, devices.dev[i].device) == 0)
            {
                if (ipwd_devinfo(check_context.dev.device, check_context.dev.ip, 
                                check_context.dev.mac) != IPWD_RV_ERROR)
                {
                    check_context.dev.state = IPWD_DEVICE_STATE_USABLE;
                }
                break;
            }
        }
    }
    
    if (check_context.dev.state == IPWD_DEVICE_STATE_UNUSABLE)
    {
        ipwd_message(IPWD_MSG_TYPE_ALERT, "Cannot find usable device for IP %s", check_context.ip);
        return -1;
    }
    
    return 0;
}

/**
 * Send ARP probe for a specific conflict entry
 * Must be called with mutex locked
 */
static void send_conflict_probe(IPCONFLICT_DEV_INFO *info)
{
    /* Find device info for sending ARP */
    char local_ip[IPWD_MAX_DEVICE_ADDRESS_LEN] = {0};
    char local_mac[IPWD_MAX_DEVICE_ADDRESS_LEN] = {0};
    
    if (ipwd_devinfo(info->device, local_ip, local_mac) == IPWD_RV_ERROR)
    {
        return;
    }
    
    /* Send ARP request to check if conflict still exists */
    ipwd_genarp(info->device, "0.0.0.0", local_mac,
                info->ip, "ff:ff:ff:ff:ff:ff", ARPOP_REQUEST);
    
    info->last_probe_time = time(NULL);
}

/**
 * Periodic probe thread - actively checks if conflicts are resolved
 * Probes every 5 seconds
 */
static void *periodic_probe_thread(void *arg)
{
    (void)arg;
    
    ipwd_message(IPWD_MSG_TYPE_DEBUG, "Periodic probe thread started (interval: 5 seconds)");
    
    while (probe_thread_running)
    {
        /* Sleep for 5 seconds between probe cycles */
        for (int i = 0; i < 5 && probe_thread_running; i++)
        {
            sleep(1);
        }
        
        if (!probe_thread_running)
            break;
        
        pthread_mutex_lock(&mutex);
        
        IPCONFLICT_DEV_INFO *pre = ipconflict_dev_info;
        IPCONFLICT_DEV_INFO *info = pre ? pre->next : NULL;
        time_t now = time(NULL);
        int probed_count = 0;
        
        while (info)
        {
            /* Check if we should probe this conflict */
            int should_probe = 1;  /* Always probe in each cycle */
            int should_timeout = 0;
            
            /* Timeout if no conflict seen for 5 minutes */
            if ((now - info->last_conflict_time) >= 300)
            {
                should_timeout = 1;
            }
            
            if (should_timeout)
            {
                ipwd_message(IPWD_MSG_TYPE_DEBUG,
                            "Conflict timeout (5min): IP=%s MAC=%s Remote=%s - auto-resolving",
                            info->ip, info->mac, info->remote_mac);
                
                emit_is_conflict(info->ip, info->mac, info->remote_mac, 0);
                
                /* Remove from list */
                IPCONFLICT_DEV_INFO *to_free = info;
                pre->next = info->next;
                info = info->next;
                free(to_free);
                continue;
            }
            else if (should_probe)
            {
                send_conflict_probe(info);
                probed_count++;
            }
            
            pre = info;
            info = info->next;
        }
        
        pthread_mutex_unlock(&mutex);
        
        /* Wait 2 seconds for ARP responses to arrive */
        if (probed_count > 0)
        {
            sleep(2);
            
            /* Now check which probes got no response */
            pthread_mutex_lock(&mutex);
            
            pre = ipconflict_dev_info;
            info = pre ? pre->next : NULL;
            time_t check_time = time(NULL);
            
            while (info)
            {
                /* Check if this conflict was just probed (last_probe_time close to 'now') */
                if (info->last_probe_time > 0 && (check_time - info->last_probe_time) <= 5)
                {
                    /* Check if we got a conflict response after the probe
                     * If last_conflict_time is older than last_probe_time, no response */
                    if (info->last_conflict_time < info->last_probe_time)
                    {
                        /* No new conflict seen since probe - increment no-response counter */
                        info->probe_no_response_count++;
                        
                        ipwd_message(IPWD_MSG_TYPE_DEBUG,
                                    "Probe no-response for IP=%s Remote=%s, count now=%d (last_conflict=%ld, last_probe=%ld)",
                                    info->ip, info->remote_mac, info->probe_no_response_count,
                                    (long)info->last_conflict_time, (long)info->last_probe_time);
                        
                        /* If we've had 3 consecutive probes with no conflict response,
                         * consider it resolved */
                        if (info->probe_no_response_count >= 3)
                        {
                            ipwd_message(IPWD_MSG_TYPE_DEBUG,
                                        "Conflict resolved by probe (3 no-response): IP=%s Remote=%s",
                                        info->ip, info->remote_mac);
                            
                            emit_is_conflict(info->ip, info->mac, info->remote_mac, 0);
                            
                            /* Remove from list */
                            IPCONFLICT_DEV_INFO *to_free = info;
                            pre->next = info->next;
                            info = info->next;
                            free(to_free);
                            continue;
                        }
                    }
                    else
                    {
                        /* Got conflict response - reset counter */
                        info->probe_no_response_count = 0;
                    }
                }
                
                pre = info;
                info = info->next;
            }
            
            pthread_mutex_unlock(&mutex);
        }
    }
    
    ipwd_message(IPWD_MSG_TYPE_DEBUG, "Periodic probe thread stopped");
    return NULL;
}

int ipwd_conflict_check(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    (void)userdata;
    (void)ret_error;
    
    pthread_mutex_lock(&mutex);

    /* Get parameters from D-Bus */
    sd_bus_message_read(m, "ss", &check_context.ip, &check_context.misc);
    check_context.wait_reply = true;
    check_context.msg = m;
    check_context.conflic_mac = NULL;
    
    /* Verify and select device */
    if (ipwd_check_context_verify() < 0)
    {
        sd_bus_reply_method_return(m, "s", "");
        check_context.wait_reply = false;
        pthread_mutex_unlock(&mutex);
        return 0;
    }
    
    pthread_mutex_unlock(&mutex);
    
    /* Send ARP probes and let main loop handle responses */
    #define PCAP_MAX_TIMES 5
    ipwd_message(IPWD_MSG_TYPE_DEBUG, "Starting ARP probe for IP:%s on device:%s MAC:%s", 
                check_context.ip, check_context.dev.device, check_context.dev.mac);
    
    for (int i = 0; i < PCAP_MAX_TIMES; i++)
    {
        pthread_mutex_lock(&mutex);
        int rv = ipwd_genarp(check_context.dev.device, "0.0.0.0", check_context.dev.mac,
                   check_context.ip, "ff:ff:ff:ff:ff:ff", ARPOP_REQUEST);
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "ARP probe %d/%d sent, result:%d", i+1, PCAP_MAX_TIMES, rv);
        
        /* Check if conflict already detected by main loop */
        int found = (check_context.conflic_mac != NULL);
        pthread_mutex_unlock(&mutex);
        
        if (found)
        {
            ipwd_message(IPWD_MSG_TYPE_DEBUG, "Conflict found early at probe %d", i+1);
            break;
        }
            
        /* Wait for main loop to process responses */
        usleep(20000); /* 20ms - give main loop time to process */
    }
    
    ipwd_message(IPWD_MSG_TYPE_DEBUG, "ARP probing complete, checking result...");
    
    pthread_mutex_lock(&mutex);
    
    /* Reply with result */
    if (check_context.conflic_mac)
    {
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "Conflict detected! Returning MAC: %s", check_context.conflic_mac);
        sd_bus_reply_method_return(m, "s", check_context.conflic_mac);
        free(check_context.conflic_mac);
        check_context.conflic_mac = NULL;
    }
    else
    {
        ipwd_message(IPWD_MSG_TYPE_DEBUG, "No conflict detected, returning empty string");
        sd_bus_reply_method_return(m, "s", "");
    }
    
    check_context.wait_reply = false;
    pthread_mutex_unlock(&mutex);
    return 0;
}
