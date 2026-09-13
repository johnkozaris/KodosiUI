# Kodosi Qt design

Swift's current workflow is the reference. These visual constraints are binding;
Qt remains a native Linux presentation layer, not an agent dashboard.

- Warm charcoal/cream surfaces and copper actions. Use the semantic colors in
  `src/qml/Theme/KodosiTheme.qml`; light and dark appearances share role names.
- Platform UI fonts, 14px navigation, compact labels, fixed-pitch terminal text.
  Sentence case, not tracked uppercase. Terminal font size controls cell geometry.
- A compact header with Sessions, Missions, People, Sign in and Settings. Sidebar
  width is 230px, with folder groups, auto-named New Session and History.
- Stable Kodosi names in sidebar rows; tile headers append provider/title metadata.
  Share, Details, Minimize, Maximize and Close remain directly accessible.
- Details separates Workspace, People and Mission. Save only unapplied drafts;
  close overlays with X. Appearance lives in Settings.
- History replaces bounded pages, collapses long/tool content and resumes explicitly.
- Terminal pixels remain primary. Keep native surfaces while navigating/maximizing;
  hidden surfaces must not accept input. At most six staged terminals, with native
  adaptive tiling and structural dividers. No manual fit/pan or Interrupt menu.
- Use opaque authored controls, visible keyboard focus, accessible contrast and
  stable dotted accessibility identifiers. Respect reduced motion.

The current Qt theme uses 8/10/12/16px control/container radii; do not invent another
radius family locally. Product-wide visual changes must update the shared design
intent and implementation together. Avoid status furniture, decorative cards,
backend controls, or repeated host metadata on every session row.

## Avoid

- Purple/blue gradients, gradient text, glass cards, colored-border cards, transparent
  surfaces, thin outlined controls, grain-on-gradient overlays, and low contrast.
- Emoji headings, decorative italic serifs, default Inter everywhere, the Space Grotesk
  and Instrument Serif pairing, universal Lucide iconography, and stock web-kit styling.
- Repetitive icon-box grids, headline badges, navigation counts, connection-status
  furniture, a separate Attention sidebar, or repeated project/mode metadata per row.
- Scroll-reveal animations, cursor-following effects, controls fading on hover,
  inconsistent spacing, excessive em dashes, buzzwords, and helper copy for obvious actions.
- Multiple Mission panes, controls exposing backend internals, or new interaction
  patterns absent from the Swift client.
