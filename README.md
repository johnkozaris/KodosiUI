# KodosiQT

Cross-platform Qt Quick desktop client for Kodosi.

KodosiQT is a presentation adapter over the existing Rust runtime. Rust remains
the authority for processes, PTYs, terminal ordering, identity, encryption,
sharing, Missions, agent supervision, and mutation outcomes. The Qt client owns
windows, native input, rendering, accessibility, localization, and other
desktop integration.

## Baseline

- Qt 6.11.2
- CMake 4.4.3
- C++23
- Kodosi FFI ABI 5
- Kodosi desktop protocol 36

The exact source and protocol baselines are recorded in `dependencies.lock.json`
and `protocol/desktop-client-parity.json`.

## Architecture

```text
QML presentation
      |
typed Qt models and actions
      |
C++ runtime adapter -------- native terminal renderer
      |                               |
Kodosi C ABI 5                 patched libghostty-vt
      |                               |
      +--------- Rust authority ------+
```

Raw protocol JSON, terminal bytes, checkpoints, account epochs, and session
incarnations do not enter QML. The first client uses ABI 5 directly so the
Linux work remains reversible. A shared Rust projection module may be added
later only after it runs in shadow mode against the shipping Swift client.

`DesktopFileIntegration` owns local folder selection and default-application
launching. It uses an asynchronous, application-modal Qt Widgets
`QFileDialog`. Before `QApplication` exists, Kodosi selects Qt's
`xdgdesktopportal` platform theme when `QT_QPA_PLATFORMTHEME` is unset and
preserves any explicit user override. The desktop portal discovers the active
backend, including KDE's backend when the desktop provides it; Kodosi does not
ship or claim a KDE Qt platform-theme plugin. The official Qt 6.11.2 portal
theme and GTK 3 fallback are packaged in the private runtime. QML receives only
semantic request results and typed failures. Every accepted or launched path
is length-bounded, local, canonical, existing, and permission-checked.
Launches call `QDesktopServices::openUrl(QUrl::fromLocalFile(...))`; Kodosi
does not invoke a shell directly, while Qt or the desktop may delegate to a
portal, registered handler, or `xdg-open`.

Linux packages depend on `xdg-desktop-portal` for native chooser routing and
`xdg-utils` as a desktop-handler fallback for `QDesktopServices`.

`DesktopStateModel` owns Stage membership, selection, focus mode, account-scoped
remote restoration, and versioned desktop persistence. The pure
`TerminalTilingLayoutModel` owns adaptive 1–6 pane geometry, minimum tile
clamping, scrolling, and divider proportions. QML renders those typed
presentation models and keeps one independent native terminal surface per
staged session; focus mode hides rather than destroys the other tile delegates.

Agent Intelligence Project Memory is acquired on demand through a native
model. QML supplies only the stable session ID and presentation selections;
the model privately resolves the current local Claude identity and retains
protocol 36 bound-selection authority.

Custom Agents use the same demand boundary. Native code asks Rust for bounded,
path-free summaries and keeps Rust's one-shot detail selections private. QML
receives only opaque presentation identities and bounded detail text.

## Development

```bash
just parity
just configure
just build
just test
```

## Linux UI probe

`kodosi-ui-probe` is a developer-only external accessibility and raw-input CLI
for operating the real running Qt app. Semantic operations and X11 input use
the accessibility bus returned by `org.a11y.Bus.GetAddress`; screenshots and
Wayland input use official `xdg-desktop-portal` interfaces. Every command emits
deterministic JSON on stdout.

```bash
just ui-probe --help
just ui-probe apps
just ui-probe find --pid <launched-pid> --app Kodosi --id header.settings
just ui-probe shortcut --keys Ctrl+Shift+P
just ui-probe pointer --element <handle> --position center
just ui-probe button --button left --click
just ui-probe drag --from-x 10 --from-y 20 --to-x 300 --to-y 200
just ui-probe-input-status
just ui-probe-smoke
```

Set `KODOSI_UI_PROBE_SKIP_INTERACTIVE_PORTALS=1` when running the real-app
probe unattended. The flow still verifies the Browse and Open Folder
accessibility contracts but does not click Browse or request screenshot
consent.

The real-app probe also launches an isolated
`--ui-probe-tiling-synthetic` process to exercise multi-pane selection, focus,
divider adjustment, accessibility values, and exact PID isolation without
reading or writing normal user configuration.

`apps` reports each application's exact Unix process ID together with its
AT-SPI unique bus name, root object path, and opaque application handle.
Automation should resolve the launched PID together with the expected
application name or accessible ID once, then pass that application handle to
every `tree`, `find`, and `wait`; element handles returned by that identity are
used for `click`, `focus`, and `set-text`. Any ambiguous PID/name association
is rejected rather than guessed.

On X11, `key`, `shortcut`, `pointer`, `button`, and `drag` call the official
AT-SPI `org.a11y.atspi.DeviceEventController` methods after non-mutating
Registry-owner and interface introspection checks. On Wayland, run
`just ui-probe-input-start` once. The `input-serve` sidecar obtains keyboard,
pointer, and screen-source consent on one portal session through
`RemoteDesktop.CreateSession`, `ScreenCast.SelectSources`,
`RemoteDesktop.SelectDevices`, and `RemoteDesktop.Start`, then serializes
events received on a mode-0600 Unix socket below `XDG_RUNTIME_DIR`. The socket
has a random name and nonce, accepts only the same Unix UID, has bounded
length-prefixed JSON frames, and never listens on TCP.
During consent and startup, the sidecar remains parent-bound by a Linux
parent-death signal and an inherited lifecycle channel. It becomes detached
only after `input-start` verifies and commits the exact PID, process start
time, socket, and nonce through a READY/COMMIT handshake. Pending startup is
reported by `input-status`, can be cancelled exactly by `input-stop`, and
cannot later publish a socket if the launcher exits before commit.
`just ui-probe-input-stop` stops that exact authenticated sidecar.

Portal consent remains authoritative. The sidecar closes when
`org.freedesktop.portal.Session::Closed` is received; a disconnected CLI client
does not close or recreate consent. Portal version 2 persistence is requested
with `persist_mode=2` and a private single-use restore token, but the desktop
may still prompt, deny, revoke, or omit persistence. Absolute Wayland pointer
motion additionally requires a stream returned by the portal; otherwise use
`pointer --dx/--dy`. `input-status` and `doctor` report this limitation without
opening a consent prompt.

Raw input modes default to `--press`/`--click`. Explicit `--down` and `--up`
are available for stateful portal keys and for AT-SPI modifiers; the legacy
AT-SPI interface cannot hold a non-modifier keysym and returns exit `6` for
that request. Shortcuts release the terminal key first and then modifiers in
reverse order, and
shortcut/drag/click cleanup is attempted after a remote failure.
Keys, buttons, coordinates, event counts, frame sizes, and drag duration are
bounded; command arguments are parsed directly and never evaluated by a shell.
Coordinates are event parameters, never element identity. `pointer --element
<handle> --position center` resolves fresh AT-SPI screen bounds immediately
before dispatch.

Exit codes are: `0` success, `2` usage, `3` environment unavailable, `4`
not found, `5` ambiguous, `6` unsupported, `7` D-Bus/remote failure, `8`
timeout, `9` invalid handle, `10` portal denied/cancelled, and `11` a safety
limit.

The target is intentionally not installed by the DEB or Arch package.
Applications under test should be launched with
`QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`. The packaged X11/Wayland smoke also
checks the adjacent release-build probe's Wayland adapter selection and portal
authority reporting without opening an unattended consent request.

The sibling `../Kodosi`, `../kodosiSwift`, and `../kodosi-ghostty` checkouts
are required. CI checks out their immutable baseline commits and requires a
read-only `KODOSI_REPOSITORY_TOKEN` secret for the private sibling repositories.
