# GlobalShortcuts Portal Verification

The service plugin and `dde-global-shortcuts-provider` must be deployed together
with the matching `xdg-desktop-portal-dde` backend. The public broker must support
GlobalShortcuts v2 (upstream 1.22 or a complete downstream backport).
The first proposed service release containing this private API is 1.0.51;
the matching backend requires `dde-services (>= 1.0.51~)`. Build with Qt 6.8+
and treeland-protocols 0.6.0+. On Wayland, registration and capture use
`treeland_shortcut_manager_v3` interface version 1.

## Private Contract

`interfaces/org.deepin.dde.Keybinding1.Portal.xml` is the wire contract.
`PortalSessionShortcutsChanged(appId, sessionToken, shortcuts)` has signature
`ssa(ssssasasbb)`. It publishes each live Session's actual binding snapshot after
Configure updates its references. An application-level `active` flag alone does
not grant another Session a reference. Both XML and runtime introspection are
checked by `tst-portalshortcutcontracts`.

Before release, inspect the installed service, not just its package version:

```bash
busctl --user introspect org.deepin.dde.Keybinding1 \
  /org/deepin/dde/Keybinding1 org.deepin.dde.Keybinding1
```

Require the Session signal and all XML methods. An older service missing this
signal is not a supported pairing for the updated backend. Set the packaging
dependency floor to the first release that actually contains this contract.

## Automated Tests

From the repository root, with a configured Qt6/DTK6 build and local D-Bus sockets
permitted:

```bash
cmake --build build --target plugin-dde-shortcut dde-global-shortcuts-provider \
  tst-portalshortcutcontracts tst-portalshortcutmanager \
  tst-globalshortcutsprovidercontract tst-globalshortcutsprovider \
  tst-x11shortcutcapture tst-waylandshortcuts tst-qkeysequenceconverter -j4
ctest --test-dir build --output-on-failure \
  -R '^shortcut-(portal|globalshortcuts|x11shortcutcapture|waylandshortcuts|qkeysequenceconverter)'
```

Manager tests cover per-Session references, Configure enabling previously unbound
actions, press/release deduplication, disable/rebind/rollback/close/reconnect while
held, and Release during nested writes. The runtime and storage failure cases use
test doubles, not the user's DConfig data. Nested writes must still return Busy;
the backend is responsible for serial recovery and bounded Busy retries.
Regression cases include persistence and runtime compensation failures, bounded
reconciliation failure, closing during reconciliation, late v3 registration
failure, supplementary Unicode, symbolic digits, keypad conversion, stale
configuration replies after reopening, and X11 long holds and split repeat pairs.
The Wayland mock verifies that `Ctrl+Num+1` reaches the v3 adapter unchanged and
remains distinct from `Ctrl+1`; physical compositor matching needs manual coverage.

XDG `NUM` means XKB NumLock/Mod2. It is rejected because the current PortableText
contract cannot encode that lock-state requirement. Keypad identity comes from
`KP_*`, for example `CTRL+KP_1` becomes `Ctrl+Num+1` and `<Control>KP_1`.

For shared input-path changes, rebuild and run the other `shortcut-*` targets.
X11 injection tests must use their CTest Xvfb wrappers, never an active desktop.

## Manual Acceptance

On both X11 and Treeland, verify a short press/release, disabling and rebinding
while held, two same-app Sessions with different successful subsets, Configure
disable/re-enable, last-Session cleanup, and service-only restart with two live
Sessions. Verify old system/custom shortcuts still work and respect conflicts.
Also check actual persistence, provider capture, cancellation, and Flatpak access.

For host clients, install a test application's `.desktop` file and call
`org.freedesktop.host.portal.Registry.Register` on the same connection before
the first portal method. A systemd `app-*.service` unit alone does not guarantee
an app ID. Flatpak clients use their sandbox identity and must not call the host
Registry. Keep the client's stdin/connection alive until the case finishes.

Before a recovery test, require successful Bind responses and inspect
`busctl --user status org.deepin.dde.Keybinding1`. Restart the unit that owns that
name, then wait for a new owner and restored lists before testing input. The X11
QA machine uses `deepin-service-group@dde.service`.

On a compositor without `treeland_shortcut_manager_v3`, capture must fail to
start without disconnecting the provider. With the v3 interface at version 1,
verify capture from the provider's own surface, Busy while a capture is active,
and error completion. This requires a real or controlled compositor; the
provider D-Bus contract test does not cover physical capture.

Record package/build identity, compositor/protocol version, app ID, Session,
trigger, signals, timestamps, and logs. Never clear a real user's DConfig to
prepare a test. A service crash publishes an empty backend binding snapshot; it
does not guarantee a synthetic Release across the lost service clock.

The cooperating workspace's `portal-manual-test-plan.md` contains the detailed
Chinese QA procedure and `portal/gs_manual.py` client. These workspace artifacts
are separate from this repository. On 2026-09-08 the local shortcut CTest run
passed 15/15 suites. On 2026-09-11 real X11/Flatpak regressions passed for session
isolation, held disable/rebind, close and multi-session service recovery, including
a same-action Session whose successful Bind returned an empty subset. Input was
XTest, not a physical keyboard; the sessions shared one Flatpak app ID.

For the historical v2 implementation, on 2026-09-14 the Treeland version guard built and the provider contract
test passed. This guard is not in the installed QA packages. A host Registry +
Create/empty Bind/List workflow also passed with a temporary test desktop entry.
Full Wayland, physical input, cross-app, persistence-failure and release-matrix
acceptance remains pending. Workspace evidence is indexed by
`portal/results/README.md`; targeted results do not constitute release sign-off.
Those historical packages predate the current v3 integration and review fixes.
