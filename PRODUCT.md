# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Stack

Qt Quick and C++ over the shared Kodosi Rust runtime. Qt is the Linux client; Swift
is the macOS client. Current versions and contracts live in the build/protocol files.
Cross-platform product direction lives in `../Kodosi/PRODUCT.md`.

## Users

Developers running real local terminals and coding agents who want to reach their
work from another approved device or share selected terminals with trusted people.

## Product Purpose

A small terminal workbench: local terminals, approved personal devices, explicit
full-control friend sharing, and Missions as named groups of people and terminals.
Native provider history supplies read-only conversation preview and explicit resume.
Original provider configuration files open in the user's editor.

The September 2026 pivot changed the product substantially. This brief is a current
direction, not a complete specification or proof that all workflows are solved.
Creative thinking is welcome: question inherited assumptions, identify missing pieces,
and find better, simpler implementations. Use `/code-cleanup:code-cleanup`,
`/code-review`, and `/simplify` as appropriate, not a separate audit framework in these
docs. Existing code and candidate lists are starting evidence, not predetermined answers.

## Operating Context

- Minimize hides a terminal view and leaves its process running. Close ends the session
  for everyone connected and removes it from the live catalog; it creates no session archive.
- Closing the window hides Kodosi. Quit ends processes hosted on this computer, not
  sessions hosted elsewhere. Files and provider-saved conversations are kept.
- A shared shell runs with the host OS user's capabilities, not a project sandbox.
- Terminal control does not confer identity or sharing administration.
- Mission membership is independent of terminal sharing.
- Linux GNOME/KDE use Wayland or X11; macOS uses the native Swift client.
- Agent messaging and task boards are outside today's scope. Explore them separately
  if the product direction changes rather than reviving dormant old implementations.

## Capabilities and Constraints

Sessions, Missions, and the terminal stage are the current navigation model. Their
presentation and implementation can improve as the product develops. Native input,
checkpoint ordering, screen-reader text, IME, window behavior, and useful tiling matter.
Rust owns execution and cryptographic authority; Qt/QML presents and adapts the native
experience rather than becoming a competing process or authorization owner.

Access tiers, semantic steering, approval interception, agent-intelligence dashboards,
and provider/plugin management are outside the current focus. Do not preserve their
old machinery merely because it still compiles or has tests. Equally, do not remove a
useful current native capability just because its implementation predates the pivot.

Protect consent, identity pins, provider files, credentials, histories, and user working
data. Architectural flexibility is not permission to weaken those boundaries.

## Brand Commitments

Warm near-black surfaces, restrained copper accents, compact authored controls,
subtle seams, and a workbench rather than administration-dashboard character.
`DESIGN.md` describes the current visual language. Improve usability thoughtfully
without turning maintenance into an unrelated visual redesign.

## Accessibility & Inclusion

Keyboard navigation, screen-reader semantics, visible focus, reduced motion, and
sufficient contrast are part of the native experience. Use the terminal's accessible
text boundary rather than an independent QML copy of terminal state. Reassess gaps
and incomplete interactions rather than assuming existing tests settle the design.

## Evidence

The product is unlaunched. Do not invent usage, release, parity, or live-desktop
validation claims. Source/model checks, native compiled tests, and real desktop
interaction evidence establish different things.
