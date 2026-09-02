# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Stack

Qt Quick 6.11.2 and C++23 over the existing Kodosi Rust C ABI. The shipping
Swift client remains the product and interaction baseline.

## Users

Kodosi is for developers who run several coding agents locally and need to
supervise their work, terminals, approvals, and collaboration from one place.
The primary desktop use case is a long-running engineering session on Linux or
macOS with frequent switching between active agents and Missions.

## Product Purpose

Kodosi is mission control for coding agents. It keeps execution local while
letting a user observe and steer concurrent sessions, approve sensitive work,
and collaborate with trusted people through end-to-end encrypted Missions.
Success means the user can understand what every agent needs and act without
leaving the workbench.

## Positioning

The Rust runtime remains the authority for execution, identity, encryption,
permissions, ordering, and reconciliation. Desktop clients are native
presentation adapters over that same local-first authority rather than separate
implementations of security or session rules.

## Operating Context

- Long-lived local and shared terminal sessions.
- Several agents visible at once, with one selected interaction target.
- Time-sensitive permission requests and attention states.
- Account, device, trust, and Mission state that can change while the app runs.
- GNOME and KDE desktops under Wayland or X11; macOS remains represented by the
  shipping Swift client.

## Capabilities and Constraints

- Preserve the established product terms **My Agents**, **Missions**, and
  **Stage**.
- Raw wire JSON, terminal bytes, account epochs, and session incarnations never
  enter QML.
- Every action resolves a stable native identity immediately before dispatch.
- Linux must retain feature and visual parity with the Swift client.
- The terminal is rendered natively with the pinned Ghostty VT implementation.
- Third-party runtime dependencies must be established, maintained projects;
  low-confidence packages are not acceptable.

## Brand Commitments

The visual identity is the shipping Swift client: warm near-black surfaces,
restrained copper accents, compact native controls, subtle seams, and a
workbench rather than dashboard character. Kodosi should feel focused and
crafted, not like a generic administration console.

## Evidence on Hand

- Swift visual authority: `../kodosiSwift/Sources/DesignSystem/AppTheme.swift`
- Shipping shell: `../kodosiSwift/Sources/Features/Shell/AppShell.swift`
- Session rail: `../kodosiSwift/Sources/Features/Sidebar/SessionSidebarView.swift`
- Terminal tile: `../kodosiSwift/Sources/Features/Sessions/SessionTileView.swift`
- Brand assets: `../kodosiSwift/Resources/Assets.xcassets`

No testimonials, usage metrics, or commercial claims are available and none
should be fabricated.

## Product Principles

- Keep execution local and security authority in Rust.
- Make urgent work visible without turning the whole interface into an alert.
- Preserve context while moving between agents, people, and Missions.
- Prefer exact, recoverable state over optimistic presentation.
- Match platform conventions without losing Kodosi's visual identity.

## Accessibility & Inclusion

Keyboard navigation, screen-reader semantics, visible focus, reduced-motion
compatibility, and sufficient text contrast are release requirements. Terminal
content uses a native accessible text interface rather than a visual-only QML
representation.
