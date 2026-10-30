# Design assets

The visual system is not stored here. Colour, type, spacing and the Thai typography rules live in
`web/tokens.css`, which is the only token source; the decision and its consequences are in
`docs/decisions/0002-theme-and-auth.md`.

This directory holds the mark, the variants it is drawn in, and the specification behind them.

## Masters

| File | Ground it is drawn for | Use |
|---|---|---|
| `logo.svg` | dark | the master artwork, 64-unit grid, three tones |
| `logo-16.svg` | dark | the favicon build: middle unit dropped, silhouette unchanged |
| `logo-mono.svg` | light | one colour (`#0f110c`) for print, embossing, invoices, any single-tone surface |
| `logo-white.svg` | dark and photographic | one colour (`#ffffff`) where the mark must sit on an image or a dark field |
| `logo-apple.svg` | — | 180 Apple touch icon: square opaque plate, no radius, because the platform masks it |
| `logo-maskable.svg` | — | maskable build: the mark at 57% so a circular crop cannot reach it |
| `logo-favicon.svg` | both | serves as `/favicon.svg`, with a `prefers-color-scheme` rule for light and dark tab bars |
| `logo-brief.md` | — | the specification: what the product is, what the mark carries, why each element is where it is |

## Served files

`web/public/icons/` holds what the browser actually requests. Every file is rendered from a master
above; nothing there is drawn by hand.

| File | Size | Declared as |
|---|---|---|
| `favicon.ico` | 16, 32, 48 | `/favicon.ico`, requested automatically |
| `*.webp` | every PNG above | the same raster, lossless, for browsers that take it |
| `favicon-16.png` | 16×16 | `<link rel="icon" sizes="16x16">` |
| `favicon-32.png` | 32×32 | `<link rel="icon" sizes="32x32">` |
| `favicon-48.png` | 48×48 | `<link rel="icon" sizes="48x48">` |
| `apple-touch-icon-180.png` | 180×180 | `<link rel="apple-touch-icon">` — opaque, no alpha |
| `icon-192.png` | 192×192 | `manifest.json`, `purpose: "any maskable"` |
| `icon-512.png` | 512×512 | `manifest.json`, `purpose: "any"` |
| `icon-maskable-512.png` | 512×512 | `manifest.json`, `purpose: "maskable"` — the mark sits inside the 409 px safe circle |
| `logo-512.png` | 512×512 | documentation, README, social cards |
| `logo-mono-512.png` | 512×512 | print and light-ground embeds |
| `logo-white-512.png` | 512×512 | dark and photographic embeds |

```html
<link rel="icon" href="/icons/favicon.ico" sizes="32x32">
<link rel="apple-touch-icon" href="/icons/apple-touch-icon-180.png">
<link rel="manifest" href="/manifest.webmanifest">
```

The served set is **PNG, ICO and WEBP only**. No SVG and no PDF leave this repository's build: a
vector asset served to a browser is one more format to keep in step, and every browser that matters
takes one of the three.

```json
{
  "icons": [
    { "src": "/icons/icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any" },
    { "src": "/icons/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any" },
    { "src": "/icons/icon-maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable" }
  ]
}
```

A maskable icon is a separate file, not a flag on the same one: the launcher crops it to its own shape,
so the artwork must be smaller inside the canvas than the version used at 100%.

## Where the mark appears

Every surface, and the file it takes. A surface that is not listed uses the master for its ground;
nothing invents a new drawing.

| Surface | File | Format |
|---|---|---|
| Browser tab, bookmarks, history | `../favicon.svg` first, `favicon.ico`, `icons/favicon-16/32/48.png` | SVG, ICO, PNG |
| iOS home screen | `icons/apple-touch-icon-180.png` | PNG, opaque |
| Android launcher, desktop install, PWA | `icons/icon-192.png`, `icons/icon-512.png`, `icons/icon-maskable-512.png` | PNG, opaque |
| Site header, navigation | `../logo.svg` | SVG, inline |
| Footer, sign-in, sign-up, 404, empty state | `../logo.svg`, or `../logo-white.svg` on a dark or photographic ground | SVG |
| **Statement page for a generated task** | `../logo.svg` | SVG, inline at 20–24 px |
| Verification and approval email | `icons/logo-512.png` | PNG — most clients strip SVG |
| README, docs, changelog | `../logo.svg` | SVG |
| Slide deck | `icons/logo-1024.png` | PNG |
| Social preview / OG card | `icons/logo-1024.png`, composited into the card | PNG |
| Embossing, stamp, single-colour print | `../logo-mono.svg` | SVG |
| Photograph or dark hero | `../logo-white.svg` | SVG |

A statement **page** takes the SVG inline, so it stays sharp on a high-density screen and costs no
request. A **PDF** is not a stored asset: whatever renders the document takes the same SVG as its
source, so paper output is vector without a second file existing in the repository.

## Rendering

`tools/render-assets.sh` produces every raster from the masters, and `--check` fails if a
committed file no longer matches its master. A mark is changed by editing the SVG and running the
script; no raster is edited directly.

Print and statement output is produced from the SVG by whatever renders the document, so the
repository carries no PDF at all. That removes a second artefact that could fall out of step with the
master, and the byte-churn a PDF's embedded timestamp would cause on every render.

## Which format for which consumer

The drawing is one; the file is whatever the consumer actually accepts. Nothing is ever redrawn, and
no raster is ever upscaled into a place that needs vector.

| Where it lands | Format | Why that one |
|---|---|---|
| Website, app shell | `logo.svg` inline or as `<img>` | one request, any zoom, and the same file the master is |
| Browser tab | `favicon.ico` + the three PNG sizes | the `.ico` is requested automatically; the PNGs let a modern browser pick the sharpest |
| Home screen (iOS) | `apple-touch-icon-180.png` | required, and required to be opaque |
| Installed app (PWA) | `icon-192/512.png` | required, opaque, maskable-safe |
| Statement PDF, generated by the app | the SVG itself, embedded by the renderer | a PDF is vector; embedding a 512 px PNG makes the mark soft on a printed statement |
| Posters, print, anything going to a printer | the SVG, converted by the print workflow | print shops want vector, and the SVG is the vector |
| Slides, Word, LibreOffice | SVG where the version accepts it, otherwise PNG at twice the display size | office software handles SVG unevenly |
| Email | `logo-512.png` | most clients strip SVG |
| Social preview / OG image | rendered into the card at the platform's size | the card is a composed image, not the logo alone |

A statement PDF is the case worth naming: the app renders the statement, so the mark should be placed
as vector: the SVG placed into the rendering path, not a raster. A statement printed from a 512 px PNG
is the one place this set would look wrong.

## Formats deliberately not shipped

| Format | Why not |
|---|---|
| `mask-icon.svg` | Safari's pinned-tab mask, obsolete since Safari 12 (2018) — apple.com itself does not use it, and a normal favicon serves the pinned tab |
| `browserconfig.xml` + `mstile-150x150.png` | Windows Start tiles for IE11 and old Edge; current Windows no longer reads them |
| `apple-touch-icon-precomposed.png` | a pre-iOS-7 workaround; the plain `apple-touch-icon` is what every current device reads |
| WebP, AVIF | content-image formats. No browser accepts them as a favicon, and iOS ignores them for a home-screen icon — using them here would be a silent downgrade |
| `.icns` | a macOS application bundle asset, not a web one |

## Rules

- **A master changes, every raster is re-rendered.** No PNG is edited. The render is one command per
  size, from the SVG that owns the shape.
- **The Apple icon is opaque.** iOS composites it on a home screen; a transparent PNG becomes black on
  some devices.
- **The tile keeps the mark inside 80% of the canvas** so a maskable crop never clips it. At 62.5% it
  is inside that bound at every corner.
- **Two tones, never three.** A variant either carries the accent or does not; a half-accent variant is
  a new ground, not a new file.

The twelve theme directions and the reasoning behind choosing `judge mono` were rendered as static
previews until 2026-10-29, when the previews were removed; the comparison is recorded in
`.agents/memos/2026-10-29-decisions.md`, and the direction itself is fully expressed by the token file.
