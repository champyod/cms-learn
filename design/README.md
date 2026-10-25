# Design directions

`theme-gallery.html` renders the **same markup** in twelve directions, switchable in place. Each
theme is a token block (`--bg`, `--surface`, `--border`, `--radius`, `--shadow`, `--font`), never a
separate stylesheet — the layout file mentions no colour, radius or font, so a theme that needs a
markup edit is not a theme.

Open it and click through; `mobile 390` constrains the frame to a phone width so the responsive
collapse is visible.

## The twelve, and what each is actually copying

| Theme | Real reference | Optimises for | Where it fails a learning site |
|---|---|---|---|
| Vercel Geist | Vercel dashboard / Geist+Ink | monochrome discipline, one accent, hairline borders | reads "empty" for content-dense pages without art |
| Linear | Linear app | dense dark UI, subtle borders, motion | dark code reading is polarising for long statements |
| Stripe | Stripe dashboard & docs | dense data tables, one confident accent | the accent is the brand's, not ours |
| GitHub Primer | Primer / GitHub | utility, status semantics, familiar affordances | green accent reads as "success" everywhere |
| Liquid Glass | Apple HIG *Materials* / Liquid Glass | layered chrome, translucency on the chrome layer only | `backdrop-filter` on content = contrast + GPU cost; Apple keeps content opaque |
| IBM Carbon | Carbon Design System | data-dense, 0 radius, strict grid | zero radius + grey surfaces reads dated to students |
| Material 3 | Material You | tonal surfaces, rounded, dynamic colour | large radii waste vertical space on mobile |
| shadcn neutral | shadcn/ui default | border-led neutral, zero shadow | generic without a brand accent; the panel already uses this family |
| Notion | Notion | warm paper, serif headings, calm reading | serif + code blocks fight each other |
| Neo-brutalist | Gumroad-era web, indie products | thick borders, offset shadows, high contrast | no large-scale production system; fatigues over a 40-minute reading session |
| Judge mono | terminal / AtCoder-style tooling | monospace everywhere, status-green accent | unreadable for TH prose at length |
| Warm editorial | Anthropic-style warm neutrals | long-form reading, low glare | needs a type system to not look like a blog |

## Recommendation

**shadcn neutral as the token base, Vercel Geist's discipline applied to it, glass reserved for
overlays.** Reasons, in the order they matter here:

1. The admin panel already ships Radix + CVA + Tailwind 4 with a neutral token set. A second token
   vocabulary beside it is a duplicated design system — the exact defect to avoid — so the base
   should be the one already in the hub.
2. The site's content is long TH/EN prose plus code plus tables. Borders and one accent carry that
   shape without art; glass and brutalism both spend legibility on identity.
3. Glass stays available where Apple actually uses it — sheets, sticky headers, the command palette
   — as `--blur` on a chrome layer, not as the page background.
4. A single accent colour is the only brand surface needed: one hue for "next action", everything
   else neutral. Two accents is how a learning tool starts looking like a dashboard.

Not chosen, with the reason recorded rather than implied: the panel's theme unchanged (it is an
admin identity, not a student one), full glass (contrast + mobile GPU), neo-brutalist (no
production-scale precedent, reading fatigue), serif/editorial (fights code blocks), monospace
(TH prose).
