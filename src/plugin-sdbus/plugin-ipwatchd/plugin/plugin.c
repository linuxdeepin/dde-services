// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "service.h"
#include "plugin_adapter.h"
#include <pthread.h>

/**
 * Plugin entry point - called by deepin-service-manager
 */
int DSMRegister(const char *name, void *data)
{
    (void)name;
    
    if (!data) {
        return -1;
    }
    
    /* Set D-Bus connection */
    service_set_dbus(data);
    
    /* Register D-Bus object */
    if (dbus_add_object() != 0) {
        return -1;
    }
    
    /* Initialize plugin */
    if (ipwd_plugin_init("/var/lib/ipwatchd/ipwatchd.conf") != 0) {
        return -1;
    }
    
    /* Start ipwatchd in separate thread */
    pthread_t thread;
    if (pthread_create(&thread, NULL, (void *)ipwd_plugin_start, NULL) != 0) {
        return -1;
    }
    
    pthread_detach(thread);
    return 0;
}

/**
 * Plugin cleanup - called when plugin is unloaded
 */
int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;
    
    ipwd_plugin_stop();
    return 0;
}
