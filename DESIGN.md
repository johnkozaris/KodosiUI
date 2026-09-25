# Kodosi Qt design

Kodosi is a quiet frame around the terminal. It should feel native, compact, and
purposeful rather than like a dashboard.

## Direction

- Warm charcoal or cream surfaces with restrained copper actions.
- Platform UI type and fixed-pitch terminal type.
- Opaque surfaces, subtle seams, compact controls, and clear focus.
- The terminal receives the space and attention; navigation recedes.
- Information appears when it is useful, not all at once.

Use the semantic roles in `src/qml/Theme/KodosiTheme.qml`. Light and dark modes
share the same hierarchy.

## Interaction

- Keep primary actions obvious and secondary actions close to their subject.
- Prefer short labels over instructions and helper copy.
- Preserve native keyboard, pointer, window, and accessibility behavior.
- Keep hidden terminal surfaces inactive.
- Use confirmation only for actions that stop work or remove access.

## Avoid

Avoid decorative cards, gradients, glass effects, status furniture, repeated
metadata, backend terminology, novelty controls, and animation without purpose.
Do not expose implementation detail or explain obvious actions in the interface.
