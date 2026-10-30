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

Tokens live in `web/tokens.css`, which is the single source. The static previews that were used to
choose the direction were removed on 2026-10-29; the comparison behind the choice is in
`.agents/memos/2026-10-29-decisions.md`, and the direction itself is expressed by the token file. Contrast, advance widths and the rejected
subsetting measurement are in `.agents/memos/`.

## 2. Accounts: open signup, organisation mark, admin approval

Anyone may create a local account. At signup the account records the **organisation** the person
belongs to; an address outside a known organisation is not refused, it is **marked external**. No
account becomes active until an admin approves it.

Why a mark rather than a gate: a school-domain allowlist answers "is this our student" but cannot
answer "is this the guest lecturer", and the moment one legitimate outsider needs access the allowlist
becomes a settings edit that someone forgets to revert. A label keeps the information — this account
is not from our school — without making the system refuse people the operator has decided to admit.
Approval is what protects minors, and it is the only barrier, so it must default to on.

Consequences:

- Stored PII stays minimal: email, display name, organisation, locale. No birthdate, photo or phone.
- `unverified → verified → pending_approval → active`; every transition is audited with its actor.
- The known-organisation list is a settings row, not a code constant, and it decides the mark only.
- The `external` mark is visible to an admin and to the account itself, so a guest is never mistaken
  for a classmate and a teacher never approves one believing it is a student.
- With no domain gate, an open signup plus a default-off approval switch would admit anyone: approval
  is on unless an operator turns it off for a deliberate public demo, and that switch is recorded in
  the audit trail.
- Rate limits and lockout belong to the auth service, never to the UI.

## 3. CSS## 3. CSS

`web/tokens.css` is the only token definition. **Tailwind 4.3.3 + daisyUI 5.7.50**, both MIT, build
time only: Tailwind emits only the utilities the templates contain, and daisyUI supplies component
classes as plain CSS with no component JavaScript, so htmx stays the only script on the page.

## Open

- Whether approval is on for the pilot cohort — an operator setting, decided at deploy time.
