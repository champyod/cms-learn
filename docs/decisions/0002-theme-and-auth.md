# 0002 — Theme and account model

Status: accepted · 2026-10-25

## 1. Visual direction: "judge mono"

Chosen by the operator from the twelve-row gallery: the terminal-adjacent row — one monospace family,
near-black olive ground, a single status-green accent, 2px radius, no shadows.

Recorded against my recommendation (neutral shadcn base + Vercel discipline, glass on overlays
only). Two things make it workable rather than a cost, and they are engineering facts, not taste:

**Thai in a monospace face is rare, and the choice lives or dies on the font.** Exactly five open
Thai monospace families exist (Tlwg Mono, Tlwg Typewriter, Tlwg Typist, Tlwg Typo, Arundina Sans
Mono — all Linux-world). This build ships **Arundina Sans Mono** (GPL-2+, Thai + Latin in one
monospace design, Bitstream Vera metrics): 89 KB, self-hosted at `web/public/fonts/`, licence text
beside it. Verified by measurement rather than by font name — at 14px Thai and Latin both advance
8.44 px with zero variance inside either script, so mixed Thai/Latin columns align.

Subsetting the face to the Thai block with `unicode-range` looks cheaper (~70 KB saved) and is
wrong here: it hands Latin back to the system mono (5.91 px/char measured), and every mixed column
then drifts. That defect was found by probing advances, not by looking at the page.

**Two Thai-specific rules are token-layer rules**, because a monospace theme breaks on Thai in
exactly these two places:

1. `text-transform: uppercase` does nothing to Thai — there is no case — so a heading that relies
   on capitals silently loses its hierarchy. Hierarchy comes from size, weight and spacing;
   `--tracking-label` is for Latin labels only.
2. Thai vowels and tone marks stack above and below the base glyph, so Latin-tuned line-height
   clips them. `--lh: 1.7` body, `--lh-tight: 1.5` headings, never below 1.45; `:lang(th)` pins
   its own.

`font-synthesis: none` stops the browser slanting glyphs the family does not have (italic is not a
Thai tradition); emphasis uses weight or the accent.

Contrast measured on the token pair: body 15.41:1, accent 12.38:1, accent-on-ink 12.38:1 — well
past WCAG AA, which is the compensation for choosing a dark surface by taste.

Tokens live in `web/tokens.css` — a framework-free file. Tailwind 4 maps to it rather than
redefining it; a second token source is the duplicated-design-system defect.

## 2. Accounts: self-signup, school domain, teacher approval

Chosen: local accounts created by the student, email-verified, **restricted to an allowlisted school
email domain**, with a **teacher-approval gate that can be on or off** before the account becomes
active.

Why: it keeps the operator's two requirements — no manual account creation at class scale, and no
strangers on a site holding minors' data — without the school-IT dependency of SSO. The approval
toggle is what makes one build serve both a pilot class (approval on) and a public demo (off).

Rejected: teacher-created accounts (manual work that does not scale to the second school); school
Google SSO (best identity story, but blocked on an approval this project cannot schedule);
unrestricted self-signup (minors' PII with no gate).

Consequences for E3:

- Stored PII is the minimum: email, display name, class, locale. No birthdate, no photo, no phone.
- State machine: `unverified → verified → pending_approval → active`; approval is skipped when the
  toggle is off, and the transition is recorded in the audit trail with the approving actor.
- The domain allowlist is one config value, not a code constant: a second school must be a config
  change, and the gate fails closed when the allowlist is empty.
- Email verification needs a working mail path; a magic-link-only design was rejected for that
  reason, and password reset must not become the bypass for approval.
- Rate limits and lockout live with the auth service, not the UI.

## Open

- Judge host provider (E7).
- Whether approval is on for the pilot cohort — an operator setting, decided at deploy time.
