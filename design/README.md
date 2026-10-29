# Design assets

The visual system is not stored here. Colour, type, spacing and the Thai typography rules live in
`web/tokens.css`, which is the only token source; the decision and its consequences are in
`docs/decisions/0002-theme-and-auth.md`.

This directory holds the mark and the specification behind it.

| File | What it is |
|---|---|
| `logo.svg` | the mark, 64-unit grid, the master artwork |
| `logo-16.svg` | the favicon build: one unit dropped, silhouette unchanged |
| `logo-mono.svg` | one colour, for print, embossing and any single-tone surface |
| `logo-tile.svg` | the app tile: the mark on a plate, for launchers and social previews |
| `logo-brief.md` | the specification: what the product is, what the mark may carry, what it must survive, and why each element is where it is |

The twelve theme directions and the reasoning behind choosing `judge mono` were rendered as static
previews until 2026-10-29, when the previews were removed; the comparison is recorded in
`.agents/memos/2026-10-29-decisions.md`, and the direction itself is fully expressed by the token file.
