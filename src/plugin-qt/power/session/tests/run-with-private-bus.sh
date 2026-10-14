#!/bin/sh
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Run a command against a freshly started private session bus.
#
# Do not use dbus-run-session here. It terminates the bus by signalling
# dbus-daemon, and UOS kernels with the usec LSM label /usr/bin/dbus-daemon as
# deepin_unkillable_t, so that signal is denied:
#
#   uavc: denied { signal } for comm="dbus-run-sessio"
#         scontext=...:deepin_usec_t tcontext=...:deepin_unkillable_t
#
# dbus-run-session then waits forever for its daemon. That daemon also inherits
# the stdout pipe of the test runner, so ctest never sees EOF and reports a
# timeout even though the test itself finished and passed. Starting the daemon
# with --fork avoids depending on a signal at all: cleanup below is best effort,
# and an unreapable daemon only costs a little idle memory.
#
# Under fakeroot (dpkg-buildpackage) libfakeroot is injected through LD_PRELOAD
# and fakes getuid() as root. The client then authenticates with "AUTH EXTERNAL
# 30" while the daemon checks the real uid 10759 via SO_PEERCRED and refuses the
# connection, so every registerObject/registerService call fails. The intercept
# library is of no use to this test, so drop it and restore the real uid.
LD_PRELOAD=
export LD_PRELOAD

set -u

if [ $# -lt 2 ]; then
    echo "usage: $0 <bus-config> <command> [args...]" >&2
    exit 2
fi

bus_config=$1
shift
dbus_daemon=${DBUS_DAEMON_BIN:-dbus-daemon}

command -v "$dbus_daemon" >/dev/null 2>&1 || {
    echo "$0: dbus-daemon not found (looked for '$dbus_daemon')" >&2
    exit 1
}

address_file=$(mktemp) || exit 1
pid_file=$(mktemp) || exit 1
trap 'rm -f "$address_file" "$pid_file"' EXIT HUP INT TERM

# stdin and both output streams to /dev/null: a daemon that survives cleanup
# must never hold a pipe of the test runner open.
"$dbus_daemon" --config-file="$bus_config" --fork \
    --print-address=3 --print-pid=4 \
    3>"$address_file" 4>"$pid_file" </dev/null >/dev/null 2>&1 || {
    echo "$0: cannot start $dbus_daemon with $bus_config" >&2
    exit 1
}

bus_address=$(cat "$address_file")
daemon_pid=$(cat "$pid_file")
if [ -z "$bus_address" ]; then
    echo "$0: dbus-daemon did not print a bus address" >&2
    exit 1
fi

DBUS_SESSION_BUS_ADDRESS=$bus_address
export DBUS_SESSION_BUS_ADDRESS

"$@"
status=$?

# Best effort only: where the LSM allows it the daemon is reaped, where it does
# not the failure to signal must not turn a passing test into a failing one.
if [ -n "$daemon_pid" ]; then
    kill "$daemon_pid" 2>/dev/null || :
fi

exit $status
