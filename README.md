# KodosiQT

Linux Qt Quick client for Kodosi. Rust owns terminals, identity, encrypted sharing
and Mission metadata; Qt owns presentation and native desktop integration.
See [PRODUCT.md](PRODUCT.md) for scope and [DESIGN.md](DESIGN.md) for visual constraints.

New Session starts an auto-named shell immediately. The sidebar groups terminals
by folder; headers add provider icons and terminal titles. Minimize closes a view,
not its process. Close Session confirms before stopping it. Closing the window
hides Kodosi; Quit stops local processes. Approved devices and selected friends
have full control; Mission membership never grants terminal access.

History reads bounded Claude/Copilot pages across projects and resumes explicitly.
Provider files remain read-only. Details organizes Workspace, People and Mission.
Appearance lives in Settings. Geometry is automatic; no manual fit/pan controls.

## Build and verify

Requires Linux x86-64 and sibling `Kodosi` and `kodosi-ghostty` checkouts.
Exact source and tool pins are in `dependencies.lock.json`.

```sh
just configure
just build
just test
just lint
just check
```

Bootstrap installs pinned Qt/CMake/Ninja under `.tools/`. `build/` contains disposable
outputs. Control regression tests use the packaged `Kodosi` QML module.
Linux containers verify builds and offscreen tests, not desktop portal or Wayland
interaction. Use isolated storage; never reset user databases or provider history.

`-DKODOSI_ALLOW_DIRTY_RUNTIME=ON` is for coordinated local development only. Releases
require clean checkouts matching the immutable source pins, native provenance,
licenses and packaging gates. Never invent a source hash.

Ctrl+Shift+N creates a terminal; Ctrl+Shift+B toggles the sidebar; Ctrl+Shift+F
maximizes/restores the selected tile; Ctrl+Shift+W minimizes it. `kodosi://session/<uuid>` opens a terminal through the normal
activation path. Validate live workflows manually with the available agent tools;
do not maintain a separate smoke-test driver.
