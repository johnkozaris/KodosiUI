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
- Kodosi desktop protocol 37

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

## Linux deep links

The registered public route is
`kodosi://session/<sessionId>` with an optional single
`toolUseId=<toolUseId>` query item. A strict native parser rejects unsupported
or unsafe forms before presentation. Session links wait for the authoritative
catalog; approval links additionally wait for the authoritative pending
permission snapshot and pass only the resolved opaque approval identity to
QML.

Linux uses one app owner per isolated Kodosi data root. Secondary desktop
launches wait on the owner's early startup lock, then forward a bounded native
activation frame over a mode-0600, same-UID Unix socket below
`QStandardPaths::RuntimeLocation`, wait for an acknowledgement, and exit before
constructing the Rust runtime. The owner publishes the socket after its QML
activation connections are ready and before the event-loop-scheduled runtime
start attempt. Runtime startup failure leaves the window and activation
endpoint alive so the native lifecycle model can retry the same bridge
instance without losing desktop activation. Deep links received before runtime
readiness remain in the bounded native queue without consuming their
authoritative-data timeout; runtime loss pauses that timeout until the next
successful generation. The endpoint
namespace hashes the normalized effective Rust data root: validated
`KODOSI_DATA_ROOT/core` in isolated runs, otherwise
`QStandardPaths::ConfigLocation/kodosi`. Isolated roots require
`KODOSI_PRODUCTION_DATA_ROOT` and reject production aliases before endpoint
selection.

Desktop activation captures a bounded control-free
`XDG_ACTIVATION_TOKEN` for every activation kind. The token is carried only
inside the same-UID activation frame, remains native-only, and is installed
only while the owner raises and requests activation for the main window.

## Local application logs

The Linux client installs its Qt message handler after application identity is
set and before the Rust runtime or presentation models start. Redacted JSON
lines are written to Qt's `StateLocation` under a process-specific
`logs/kodosi-<pid>-<launch-id>.log` name with directory mode `0700` and file
mode `0600`. Each process rotates independently at 2 MiB with at most five
archives. Startup keeps only a bounded number of completed process families
and never rotates or prunes a live process family. Persisted messages contain
only UTC time, severity, Qt category, thread identity, and redacted text.
Terminal bytes and terminal content are not logging inputs. Original Qt
messages still go to stderr or journald as developer output.

Diagnostics reports logging health, native path, size, retained archive count,
and the last filesystem error. Logging health is status, not the Open Log
Folder gate: that action remains available whenever the owned directory exists
and passes native validation.
Smoke-test and UI-probe processes set isolated `XDG_CONFIG_HOME` and
`XDG_STATE_HOME` roots under `build/`; they do not write normal user logs.

`DesktopStateModel` owns Stage membership, selection, focus mode, account-scoped
remote restoration, and versioned desktop persistence. The pure
`TerminalTilingLayoutModel` owns adaptive 1–6 pane geometry, minimum tile
clamping, scrolling, and divider proportions. QML renders those typed
presentation models and keeps one independent native terminal surface per
staged session; focus mode hides rather than destroys the other tile delegates.

`AppearanceModel` owns the versioned System, Light, or Dark preference and
applies it through Qt's `QStyleHints`. Follow System removes the application
override and tracks live platform changes. On Linux it reads
`org.freedesktop.appearance/reduced-motion` asynchronously from the
Settings portal, listens for changes and service restarts, and defaults to
normal motion when the desktop does not publish the standard key. Theme
changes affect authored Qt chrome; terminal cell colors remain owned by the
terminal session and its existing settings.

Agent Intelligence Project Memory is acquired on demand through a native
model. QML supplies only the stable session ID and presentation selections;
the model privately resolves the current local Claude identity and retains
protocol 37 filesystem-identity-bound authority.

Resume Agent Work uses a native provider-conversation model over protocol 37.
It owns the canonical project folder, provider-native conversation identity,
pagination cursors, account/runtime fences, and bounded transcript preview.
QML receives only opaque presentation IDs and safe display metadata; resumed
session creation re-resolves the native identity immediately before dispatch.

Custom Agents use the same demand boundary. Native code asks Rust for bounded,
path-free summaries and keeps Rust's one-shot detail selections private. QML
receives only opaque presentation identities and bounded detail text.

Project Intelligence, Agent Settings, Auto Mode rules, and external discovery
use protocol 37 source capabilities bound to Rust-held filesystem identities.
The Qt models retain source paths, one-shot tokens, revisions, mutation IDs,
and native `/proc/<pid>/fd` handoffs privately; QML receives typed trees,
bounded display text, action availability, and opaque presentation IDs only.

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
just ui-probe-deep-link-smoke
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

## Linux release integrity

Release packages include `/usr/bin/kodosi-qt` and the pinned Rust CLI at
`/usr/bin/kodosi`, backed by private binaries under `/usr/lib/kodosi/bin`.
The Ghostty licenses and Linux VT inventory, the exact dependency lock, the
build-time identities of KodosiQT, the Rust runtime, and the Ghostty package,
and a deterministic locked Rust dependency license inventory with actual
LICENSE, COPYING, and NOTICE evidence are installed under
`/usr/share/doc/kodosi`. Packages also include the applicable Qt open-source
LGPL/GPL and third-party license texts, Qt's SPDX and CycloneDX SBOMs for the
exact 6.11.2 `linux_gcc_64` binary distribution, and ICU 73.2 license and
copyright evidence. Their official source URLs, immutable refs, source archive
SHA-256 values, and individual evidence digests are recorded in
`packaging/licenses/native-license-evidence.json`; Kodosi does not claim a Qt
commercial license.

`just release-manifest` builds and verifies the DEB, TGZ, and Manjaro package,
then writes canonical `build/release/release-manifest.json`. The manifest has
no clock field and is available only from a clean committed worktree after all
three artifacts are rebuilt. It records the product version; clean, exact
build-time identities for KodosiQT, `../Kodosi`, and `../kodosi-ghostty`;
Qt/runtime/Ghostty pins;
ABI/protocol versions, and each artifact's filename, size, SHA-256, and embedded
source identities. The Swift parity checkout remains pinned by the parity gate
but is not package provenance because it is neither compiled nor installed.
The independent verifier rejects path traversal, symlinks, duplicate names,
unexpected or oversized artifacts, hash changes, pin drift, dirty siblings,
artifacts from stale sibling commits, a changed `LinuxGhostty.ref`, and
substitution of the pinned Linux Ghostty archive. The installed provenance
payload includes that platform ref beside the Linux VT notice inventory.

`SOURCE_DATE_EPOCH` is the clean KodosiQT source commit time. It is passed to
the Rust release builds and CPack, embedded in source identity, and used for
the DEB, TGZ, and deterministic Arch package metadata. The package gate builds
DEB/TGZ twice from the same compiled/install tree, writes the Arch package
twice without `makepkg` host metadata, and requires byte-identical SHA-256
results. Archive paths, ownership, modes, and mtimes are normalized, and
`.BUILDINFO`, wall-clock, workstation, and rolling host-package metadata are
rejected. This reproducibility guarantee is scoped to the same pinned source,
toolchain, dependencies, and compiled/install build tree; it does not claim
cross-toolchain or cross-host reproducibility.

`just package` is a development packaging command: KodosiQT itself may be
dirty and that fact is embedded as `client.dirty=true`. The runtime and
Ghostty package must still be pinned and completely clean immediately before
their build, copy, and install steps. Release-manifest generation and signing
refuse a dirty identity for any of the three sources.

Three crates whose registry archives omit their upstream license files use
committed redistribution evidence under `packaging/licenses/rust-overrides`.
`packaging/licenses/rust-license-overrides.json` binds the exact crate
name/version/source identity to the official upstream URL, ref, commit, and
SHA-256. Cargo metadata declarations alone are never accepted as license
evidence.

Unsigned local builds rely on this deterministic SHA-256 manifest only.
Optional detached signing is explicit and noninteractive:

```bash
just sign-release-manifest FINGERPRINT
just verify-release-signature FINGERPRINT
```

The scripts require the exact trusted 40-hex fingerprint and an already
available GPG keyring. Signatures are ASCII-armored, verification disables
automatic key retrieval, and expired, revoked, or unexpected signers are
rejected. A signing subkey is accepted by its exact fingerprint or by the exact
primary fingerprint reported in GPG's `VALIDSIG` status. Signature verification
is offline and validates the signed manifest's canonical structure without a
producer checkout; source/pin and artifact hash verification remain the
separate full manifest verifier. Production scripts never generate, import,
export, or prompt for keys; tests use an isolated throwaway `GNUPGHOME`.
Private/internal GitHub artifact attestations require GitHub Enterprise Cloud,
so this repository does not publish or claim attestations. Automatic update
remains unimplemented; release consumers must verify the manifest (and, when
provided, its detached signature) before installing an artifact.
