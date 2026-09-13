# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Stack

Qt Quick 6.11.2 and C++23 over the Kodosi Rust runtime, desktop protocol 39 and
C ABI 6. Swift is the macOS client; Qt is the Linux client.

## Users

Developers running real local terminals and coding agents who want to reach their
work from another approved device or share selected terminals with trusted people.

## Product Purpose

A small terminal workbench: local terminals, approved personal devices, explicit
full-control friend sharing, and Missions as named project groups with people and
attached terminals. Native provider history supplies conversation preview/resume.
Configuration files open in the user's existing editor; Kodosi does not manage or
erase provider memory, history, or configuration.

## Operating Context

- Local processes continue when a view or window closes; explicit Stop or Quit ends them.
- A shared shell runs with the host OS user's capabilities, not a project sandbox.
- Terminal control does not confer identity, device, friend-set, or Mission administration.
- Mission membership is independent of terminal sharing.
- Agent-to-agent messaging and task-board design are future work, not dormant UI.
- Linux GNOME/KDE use Wayland or X11; macOS uses the native Swift client.

## Capabilities and Constraints

Keep **Sessions**, **Missions**, and **Stage**. Preserve native terminal input,
checkpoint ordering, screen-reader text, IME, window behavior, and adaptive tiling.
Keep execution and cryptographic authority in Rust. QML is presentation only.

Do not reintroduce access tiers, semantic steering, approval interception,
Agent Intelligence/Attention dashboards, provider catalogs, Mission chat/tasks,
or compatibility with the removed product. No old feature should remain as a
hidden button, reserved schema, or no-op command.

## Brand Commitments

Warm near-black surfaces, restrained copper accents, compact authored controls,
subtle seams, and a workbench rather than administration-dashboard character.
Follow `UI-DONTS.md`; do not use the reduction as an unrelated redesign.

## Accessibility & Inclusion

Keyboard navigation, screen-reader semantics, stable accessible IDs, visible
focus, reduced motion, and sufficient contrast remain requirements. Terminal
content uses its native accessible text interface, never a QML copy.

## Evidence

The product is unlaunched. Do not invent usage, release, parity, or live-desktop
validation claims. Source/model checks, native compiled tests, and real desktop
interaction evidence are separate.
