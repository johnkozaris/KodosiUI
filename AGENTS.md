# KodosiQT agent guide

Read `README.md`, `PRODUCT.md`, `dependencies.lock.json`, and
`../Kodosi/protocol/desktop-runtime-authority.json` before changing runtime integration.
Read `DESIGN.md` before visual changes.

## Ownership

Rust in `../Kodosi` owns PTYs, process lifecycle, terminal ordering, identity,
encryption, sharing, and Mission metadata. Swift on macOS and Qt on Linux are
native presentation clients of the same reduced protocol.

Qt owns windows, typed presentation models, QML, native terminal rendering/input,
accessibility, localization, file picking, and desktop notifications.

## Rules

- Desktop protocol 39 and C ABI 6 only. No old event/command lanes or compatibility.
- QML never decodes raw wire JSON or receives terminal bytes, checkpoints, FFI
  pointers, credentials, or cryptographic authority. Commands re-resolve the
  current session incarnation in native code.
- Copy borrowed callback memory before returning. GUI model changes happen on
  the Qt thread. Semantic checkpoint installation is synchronous before raw
  continuation; retain subscription/incarnation fencing.
- One full-control terminal sharing mode. Controllers cannot administer Kodosi
  identity or sharing. Only the host edits a session's shared friend set.
- Missions contain people and terminal attachments, not tasks/chat/agent dispatch.
- Provider conversation/configuration reads are on demand. Never mutate or erase
  provider files, memory, or history.
- Use the pinned Linux VT archive from `../kodosi-ghostty/LinuxGhostty.ref`.
  Do not infer its source from the macOS pin.
- Keep explanatory comments out of source; preserve functional tool directives and license notices.
- Dependencies stay project-local. Prefer Qt and the standard library.
- The dirty-runtime development override is not release provenance. Immutable
  source hashes must be real commits before packaging can pass.

## Gates

Run `just check` on Linux x86-64. Report unavailable environments and failing
release/source-pin gates honestly. Retained native terminal and platform tests
must not be replaced with no-op production paths.

Interactive QML objects need stable dotted `objectName`, matching `Accessible.id`
where supported, and an accessible name. User-visible text uses `qsTr`.

Validate live workflows manually with available agent skills and tools (Peekaboo,
curl, seam probes, or native platform tools). Do not add automated smoke drivers.
Keep focused regression tests for contracts, security, lifecycle, and native behavior.
