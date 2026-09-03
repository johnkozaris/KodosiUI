# KodosiQT agent guide

Read `README.md`, `dependencies.lock.json`, and
`protocol/desktop-client-parity.json` before changing architecture,
dependencies, runtime integration, or feature coverage.

## Ownership

- `../Kodosi` owns runtime authority, PTYs, process lifecycle, identity, E2E
  encryption, sharing, Missions, agent supervision, terminal ordering, and the
  generated C ABI.
- This repository owns Qt presentation, typed C++ adapters, QML, the native
  terminal render item, accessibility, localization, and desktop integration.
- `../kodosiSwift` is the shipping parity baseline until every required feature
  in `protocol/desktop-client-parity.json` has a passing Qt test.

## Architecture rules

- QML is presentation only. Do not decode wire JSON, retain FFI pointers,
  decide permissions, retry commands, reconcile mutations, or own security
  state in QML.
- C++ copies borrowed callback memory before returning and marshals typed values
  to the GUI thread.
- Keep raw terminal bytes and semantic checkpoints out of QML and ordinary Qt
  models.
- Semantic checkpoint installation is synchronous and must finish before raw
  continuation is admitted.
- Preserve FFI ABI 5 and desktop protocol 37.
- Add a shared Rust projection interface only in `../Kodosi`, additively, after
  shadow parity against Swift. Do not invent a Qt-only projection authority.
- Use stable IDs plus account/session incarnation identity. Never use a QML row
  index as command authority.

## Dependencies

- Build with the exact versions in `dependencies.lock.json`.
- The shipped Linux VT source authority is
  `../kodosi-ghostty/LinuxGhostty.ref`; never infer it from the macOS pin or a
  compatibility alias.
- Before changing a framework or library, review its latest release and the
  preceding 6-7 months of primary release notes.
- Do not add direct dependencies from obscure or low-confidence projects or
  repositories with fewer than 100 stars.
- Low-adoption Qt Ghostty projects may inform design but are not dependencies.
- Prefer Qt modules and the C++ standard library over third-party packages.

## Layout

- `src/app`: application composition and lifecycle.
- `src/bridge`: C ABI ownership and typed Qt models.
- `src/terminal`: native libghostty render/input adapter.
- `src/qml`: presentation, design system, and feature surfaces.
- `protocol`: parity and generated-contract authorities.
- `scripts`: deterministic bootstrap and validation.
- `tests`: interface, model, QML, accessibility, and parity tests.

## Gates

Run `just check`. New interactive QML objects require a stable dotted
`objectName` and an accessible name. User-visible strings use `qsTr`.
