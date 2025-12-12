// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "service.h"
#include "plugin_adapter.h"
#include <systemd/sd-bus-vtable.h>
#include <string.h>
#include <stdio.h>

static sd_bus *dbus = NULL;
static sd_bus_slot *slot = NULL;
static const char *dbus_path = "/org/deepin/dde/IPWatchD1";
static const char *dbus_interface = "org.deepin.dde.IPWatchD1";

static const sd_bus_vtable ipwatchd_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("RequestIPConflictCheck", "ss", "s", ipwd_conflict_check, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL("IPConflict", "sss", 0),
    SD_BUS_SIGNAL("IPConflictReslove", "sss", 0),
    SD_BUS_VTABLE_END
};

void service_set_dbus(sd_bus *bus)
{
    dbus = bus;
}

sd_bus* service_get_dbus()
{
    return dbus;
}

int dbus_add_object()
{

    int ret = sd_bus_add_object_vtable(dbus, &slot, dbus_path, dbus_interface, ipwatchd_vtable, NULL);
    if (ret < 0) {
        ipwd_message (IPWD_MSG_TYPE_ERROR, "Failed to issue method call: %s\n", strerror(-ret));
        return -1;
    }
    return 0;
}

void emit_is_conflict(const char *ip, const char *smac, const char *dmac, int is_conflict)
{
    if (!dbus) {
        return;
    }
    
    int ret = sd_bus_emit_signal(dbus,
                                  dbus_path,
                                  dbus_interface,
                                  is_conflict ? "IPConflict" : "IPConflictReslove",
                                  "sss",
                                  ip,
                                  smac,
                                  dmac);
    if (ret < 0) {
        ipwd_message(IPWD_MSG_TYPE_ERROR, "Failed to emit signal: %s", strerror(-ret));
    }
}