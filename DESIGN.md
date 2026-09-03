---
name: Kodosi
description: A warm, compact command deck for supervising coding agents.
colors:
  canvas: "#100d0b"
  panel: "#1d1714"
  card: "#2b211c"
  selected: "#3a281f"
  terminal: "#0a0807"
  foreground: "#f1e9e3"
  muted-foreground: "#ad9a8e"
  primary: "#db8a62"
  tertiary: "#62997c"
  destructive: "#df6c68"
  border: "#40332c"
  status-waiting: "#c9a355"
  status-reconnecting: "#629bb8"
typography:
  title:
    fontFamily: "system-ui, sans-serif"
    fontSize: "17px"
    fontWeight: 600
    lineHeight: 1.2
  body:
    fontFamily: "system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.4
  label:
    fontFamily: "system-ui, sans-serif"
    fontSize: "11px"
    fontWeight: 600
    lineHeight: 1.2
    letterSpacing: "0.08em"
rounded:
  sm: "8px"
  md: "10px"
  lg: "12px"
  modal: "16px"
spacing:
  sm: "6px"
  md: "8px"
  lg: "10px"
  shell: "12px"
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.canvas}"
    rounded: "{rounded.sm}"
    padding: "8px 12px"
  card:
    backgroundColor: "{colors.card}"
    textColor: "{colors.foreground}"
    rounded: "{rounded.sm}"
    padding: "12px"
---

# Design System: Kodosi

## Overview

**Creative North Star: "The Warm Command Deck"**

Kodosi is an operating surface, not a dashboard. It combines the density and
directness of a terminal multiplexer with the calm material character of a
well-used studio console. Information is compact, seams are structural, and
color is reserved for state and intent.

**Key Characteristics:**
- Warm, low-glare dark surfaces with copper and muted green accents.
- Dense native controls with clear hierarchy and minimal ornament.
- Stable rails and stages that preserve spatial context during live work.
- Terminal content remains the visual center of gravity.

## Colors

The palette uses warm neutrals rather than blue-black defaults. Copper marks
selection and primary actions; green communicates trusted or healthy state.

Dark appearance keeps the established near-black cocoa command deck. Light
appearance is authored independently as parchment, espresso, clay, and copper:
it is not an inversion and does not admit platform white, gray, or blue into
the control family. Both palettes preserve the same semantic role names so
controls remain appearance-agnostic. Terminal cell colors continue to come
from the terminal session; light appearance only changes the surrounding
workbench chrome and authored selection roles.

**The Signal Rarity Rule.** Accent colors indicate selection, action, or live
state. Large decorative accent fields are not part of the system.

## Typography

Use the platform UI family for controls and prose, and the platform fixed-pitch
family only for terminal content, identifiers, codes, and measurements.
Hierarchy is compact: 17px section titles, 13px body, and 11px labels.

## Layout

The desktop shell has a 56px layered command header, a 272px session rail, and a
flexible Stage. Use a 6/8/10/12px rhythm inside controls and 16/24px separation
between functional regions. Collapse the sidebar before reducing terminal
legibility.

## Elevation & Depth

Depth is primarily tonal. Warm charcoal layers separate canvas, rail, cards,
selected rows, and inset inputs. One-pixel seams define permanent regions;
restrained offset shadows are reserved for active terminal tiles and transient
overlays.

## Motion

Fast, normal, spinner, and attention-stagger timings are centralized in the
theme. The desktop reduced-motion preference zeros transitions and stops
continuous rotation or pulsing while preserving a static busy or attention
indicator.

## Shapes

The current shipping screenshots supersede the former 3–4px corner assumption.
Interactive controls use 8px corners, grouped controls 10px, cards and popovers
12px, and modal outer surfaces 16px. Apply this family to nearly every eligible
button, field, selected row, card, menu, popover, and dialog. Capsules remain
limited to counts and status labels; structural dividers and terminal pixels
remain square.

## Components

### Buttons
- Primary actions use copper with dark text.
- Secondary actions use raised warm charcoal; quiet actions begin transparent.
- Directional actions pair explicit copy with a trailing authored chevron.
- Keyboard focus, selection, caret, and text selection use copper, never
  platform blue.

### Cards / Containers
- Cards use the card tone, a one-pixel border, and 12px internal padding.
- Do not nest decorative cards; use seams and spacing for structure.

### Navigation
- The top navigation is a centered, inset segmented control with uppercase
  eyebrow tracking, authored line icons, and a warm selected surface.
- Session rows use state dots, one primary line, and one restrained metadata
  line. Selection is a tonal fill, not a wide accent stripe.
- The session rail begins with a full-width copper New Session action and a
  dark Resume Agent Work action.

### Inputs and overlays
- Fields are inset into the canvas tone with an 8px continuous corner and warm
  seam; menus, steppers, switches, checks, sliders, and scrollbars are fully
  branded Qt Quick controls.
- Settings is a large two-pane modal: categorical navigation at left, content
  at right, with a 16px outer surface and compact behavior at 820×560.
- Popovers use a raised brown surface, 12px corners, warm seams, and soft
  downward shadow. Linux platform controls must never leak through.

### Terminal Stage
- Terminal pixels sit directly on the deepest surface.
- Chrome stays compact and never overlays terminal content.
- Connection and failure states use short, actionable copy.
- Grid mode uses adaptive native tiling with six-pixel structural dividers,
  280×170 minimum panes, and vertical overflow instead of shrinking terminals
  below legibility.
- Focus mode preserves the other staged terminal subscriptions and marks the
  selected tile with a restrained copper seam.

## Do's and Don'ts

### Do:
- **Do** make approvals visually urgent but spatially contained.
- **Do** keep terminal and session state readable at a glance.
- **Do** use exact product language from the Swift client.

### Don't:
- **Don't** expose raw protocol data or security identifiers as editable UI.
- **Don't** use dashboard metric cards as primary structure.
- **Don't** add glass, gradients, pill-shaped ordinary controls, or generic
  blue accents.
