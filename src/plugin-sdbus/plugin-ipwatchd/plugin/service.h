// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef DEEPIN_SERVICE_SERVICE_H
#define DEEPIN_SERVICE_SERVICE_H

#include <systemd/sd-bus.h>

#define DLL_LOCAL __attribute__((visibility("hidden")))

/**
 * Set D-Bus connection for the service
 * @param bus D-Bus connection
 */
DLL_LOCAL void service_set_dbus(sd_bus *bus);

/**
 * Get current D-Bus connection
 * @return D-Bus connection pointer
 */
sd_bus* service_get_dbus(void);

/**
 * Register D-Bus object and methods
 * @return 0 on success, negative on error
 */
DLL_LOCAL int dbus_add_object(void);

/**
 * Emit IP conflict signal via D-Bus
 * @param ip IP address
 * @param smac Source MAC address
 * @param dmac Destination MAC address
 * @param is_conflict 1 for conflict, 0 for resolved
 */
void emit_is_conflict(const char *ip, const char *smac, const char *dmac, int is_conflict);

#endif // DEEPIN_SERVICE_SERVICE_H
