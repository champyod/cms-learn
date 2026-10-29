#!/usr/bin/env bash
# Renders every raster and print asset from the SVG masters in design/.
#
# The masters are the only drawings. This script is the only way a PNG, ICO or PDF is produced, so a
# change to a mark is one edit plus one run, and no raster is ever hand-tuned into a state the SVG
# cannot reproduce.
#
# Usage: tools/render-assets.sh [--check]
#   --check  render to a temporary directory and fail if any committed output would differ
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
design="$repo/design"
public="$repo/web/public"

mode="render"
if [ "${1:-}" = "--check" ]; then mode="check"; fi

command -v rsvg-convert >/dev/null || { printf 'render-assets: rsvg-convert is required (librsvg)\n' >&2; exit 1; }
command -v magick >/dev/null || { printf 'render-assets: magick is required (imagemagick)\n' >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ "$mode" = "check" ]; then
  icons="$work/icons"
else
  icons="$public/icons"
fi
mkdir -p "$icons"

outputs=()

render() {
  local master="$1" size="$2" name="$3"
  rsvg-convert -w "$size" -h "$size" "$design/$master" -o "$icons/$name"
  outputs+=("$name")
}

opaque() {
  # -strip removes the creation timestamp ImageMagick writes, without which two runs never match.
  magick "$icons/$1" -background '#161a12' -alpha remove -alpha off -strip "$icons/$1"
}

# The favicon trio comes from the small-size master: at 16 px the middle unit would close up.
render logo-16.svg 16 favicon-16.png
render logo-16.svg 32 favicon-32.png
render logo.svg    48 favicon-48.png

# The ladder every other surface picks from: launchers, docs, slides, social cards.
for size in 64 96 128 192 256 384 512 768 1024; do
  render logo.svg "$size" "icon-$size.png"
done

# Apple and maskable are separate drawings, not sizes of the master: one is opaque and square, the
# other is inset so a launcher's circular crop cannot reach the mark.
render logo-apple.svg    180 apple-touch-icon-180.png
render logo-maskable.svg 512 icon-maskable-512.png

opaque icon-192.png
opaque icon-512.png
opaque icon-1024.png
opaque icon-maskable-512.png
opaque apple-touch-icon-180.png

# Embeddable art for documents, email and print at any size the page asks for.
for size in 256 512 1024; do
  render logo.svg       "$size" "logo-$size.png"
  render logo-mono.svg  "$size" "logo-mono-$size.png"
  render logo-white.svg "$size" "logo-white-$size.png"
done

# Windows and legacy browsers request the .ico directly, so it carries the three small sizes.
magick "$icons/favicon-16.png" "$icons/favicon-32.png" "$icons/favicon-48.png" -strip "$icons/favicon.ico"
outputs+=(favicon.ico)

# Vector for print and for statement rendering, where a raster would be visible on paper.
mkdir -p "$design/pdf"
for name in logo logo-mono logo-white; do
  if [ "$mode" = "render" ]; then
    rsvg-convert -f pdf -o "$design/pdf/$name.pdf" "$design/$name.svg"
  fi
done

# Files the page serves directly, copied rather than duplicated by hand.
if [ "$mode" = "render" ]; then
  for name in logo logo-mono logo-white; do
    cp "$design/$name.svg" "$public/$name.svg"
  done
  cp "$design/logo-favicon.svg" "$public/favicon.svg"
fi

printf 'render-assets: %d raster(s) from %d master(s)\n' "${#outputs[@]}" "$(find "$design" -maxdepth 1 -name '*.svg' | wc -l)"

if [ "$mode" = "check" ]; then
  drift=0
  for name in "${outputs[@]}"; do
    if [ ! -f "$public/icons/$name" ]; then
      printf 'missing: icons/%s\n' "$name"; drift=1; continue
    fi
    if ! cmp -s "$icons/$name" "$public/icons/$name"; then
      printf 'differs: icons/%s\n' "$name"; drift=1
    fi
  done
  [ "$drift" = 0 ] || { printf 'render-assets: committed rasters do not match the masters\n' >&2; exit 1; }
  printf 'render-assets: committed rasters match the masters\n'
fi
