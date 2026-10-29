# 0002 — Theme and account model

Status: accepted 2026-10-25

## 1. Visual direction: "judge mono"

One monospace family, near-black olive ground, a single status-green accent, 2px radius, no shadows.

**Font.** `Arundina Sans Mono` (GPL-2+, Thai and Latin in one design), self-hosted at
`web/public/fonts/` with its licence beside it. Not subset by `unicode-range`: doing so hands Latin
back to the system mono and mixed Thai/Latin columns drift.

**Two Thai rules live in the token layer**, because a monospace theme breaks on Thai in exactly these
places:

1. `text-transform: uppercase` does nothing to Thai, so hierarchy comes from size, weight and spacing.
   `--tracking-label` is for Latin labels only.
2. Vowels and tone marks stack above and below the base glyph, so `--lh: 1.7` body and `--lh-tight:
   1.5` headings, never below 1.45; `:lang(th)` pins its own.

`font-synthesis: none` — italic is not a Thai tradition; emphasis uses weight or the accent.

Tokens live in `web/tokens.css`, which is the single source. Contrast, advance widths and the rejected
subsetting measurement are in `.agents/memos/`.

## 2. Accounts: self-signup, school domain, teacher approval

A student creates a local account, email-verifies, is restricted to an allowlisted school domain, and
waits for teacher approval that can be switched on or off. The switch is what lets one build serve a
pilot class and a public demo.

Consequences:

- Stored PII is the minimum: email, display name, class, locale. No birthdate, photo or phone.
- `unverified → verified → pending_approval → active`; approval is skipped when the toggle is off and
  every transition is audited with its actor.
- The domain allowlist is a settings row, not a code constant, and the gate fails closed when it is
  empty.
- Rate limits and lockout belong to the auth service, never to the UI.

## 3. CSS

`web/tokens.css` is the only token definition. **Tailwind 4.3.3 + daisyUI 5.7.50**, both MIT, build
time only: Tailwind emits only the utilities the templates contain, and daisyUI supplies component
classes as plain CSS with no component JavaScript, so htmx stays the only script on the page.

## Open

- Whether approval is on for the pilot cohort — an operator setting, decided at deploy time.
