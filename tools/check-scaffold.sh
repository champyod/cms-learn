#!/usr/bin/env bash
# Scaffold gate.
#
# At this stage the workspace is headers, one crate and two dependency manifests, so the defects that
# matter are a header that compiles only because something else was included first, a manifest the
# build silently disagrees with, and an overlay port nothing depends on. The gate compiles every
# header on its own, validates the manifests against the overlay ports they name, configures the
# workspace, and finishes by planting the defect the first check exists to catch, so a green run
# proves the check discriminates rather than merely passing.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

CXX="${CXX:-g++}"
python="${PYTHON:-python3}"
include_dirs=("$repo/core/include" "$repo/db/include")

# A third-party header is self-contained only with the dependency tree on the include path.
vcpkg_include=""
for candidate in "$repo"/build/vcpkg_installed/*/include "$repo"/vcpkg_installed/*/include; do
  if [ -d "$candidate" ]; then
    vcpkg_include="$candidate"
    break
  fi
done
extra_includes=()
if [ -n "$vcpkg_include" ]; then
  extra_includes=(-I"$vcpkg_include")
fi

fail() { printf 'scaffold gate FAILED: %s\n' "$*" >&2; exit 1; }
note() { printf '  %s\n' "$*"; }

compile_header() {
  printf '#include "%s"\n' "$1" > "$work/header.cpp"
  local includes=()
  for dir in "${include_dirs[@]}"; do
    includes+=(-I"$dir")
  done
  "$CXX" -std=c++26 -fsyntax-only "${includes[@]}" "${extra_includes[@]}" "$work/header.cpp"
}

note "every header compiles without a prior include"
for include_dir in "${include_dirs[@]}"; do
  [ -d "$include_dir" ] || fail "no $include_dir directory"
done
note "dependencies: ${vcpkg_include:-not installed}"
header_count=0
while IFS= read -r header; do
  if ! compile_header "$header"; then
    if [ -z "$vcpkg_include" ]; then
      fail "$header needs a third-party header and no vcpkg tree exists; run vcpkg install --triplet x64-linux"
    fi
    fail "$header does not compile on its own"
  fi
  header_count=$((header_count + 1))
done < <(find "${include_dirs[@]}" -name '*.hpp' | sort)
[ "$header_count" -gt 0 ] || fail "no headers were compiled"
note "$header_count headers"

note "manifests and overlay ports agree"
"$python" - "$repo" <<'PY'
import json
import pathlib
import re
import sys

repo = pathlib.Path(sys.argv[1])
for name in ("api/vcpkg.json", "api/vcpkg-configuration.json", "package.json"):
    try:
        json.loads((repo / name).read_text())
    except json.JSONDecodeError as error:
        sys.exit(f"{name} is not valid JSON: {error}")

manifest = json.loads((repo / "api" / "vcpkg.json").read_text())
declared = {entry if isinstance(entry, str) else entry["name"] for entry in manifest["dependencies"]}

ports_dir = repo / "vcpkg-ports"
ports = sorted(path for path in ports_dir.iterdir() if path.is_dir())
if not ports:
    sys.exit("vcpkg-configuration.json names an overlay directory with no ports")

for port in ports:
    spec_path = port / "vcpkg.json"
    portfile_path = port / "portfile.cmake"
    for required in (spec_path, portfile_path):
        if not required.is_file():
            sys.exit(f"overlay port {port.name} is missing {required.name}")
    spec = json.loads(spec_path.read_text())
    version = spec.get("version-semver") or spec.get("version") or spec.get("version-string")
    if not version:
        sys.exit(f"overlay port {port.name} declares no version")
    digests = re.findall(r"\b[0-9a-f]{128}\b", portfile_path.read_text())
    if not digests:
        sys.exit(f"overlay port {port.name} has no source digest in its portfile")
    if port.name not in declared:
        sys.exit(f"overlay port {port.name} is not a dependency of the workspace")

configuration = json.loads((repo / "api" / "vcpkg-configuration.json").read_text())
baseline = configuration.get("default-registry", {}).get("baseline", "")
if not re.fullmatch(r"[0-9a-f]{40}", baseline):
    sys.exit("the default registry baseline is not a commit id")

print(f"  {len(ports)} overlay port(s), baseline {baseline[:12]}")
PY

note "the workspace configures"
generator=()
if command -v ninja >/dev/null; then
  generator=(-G Ninja)
fi
"${CMAKE:-cmake}" -S "$repo" -B "$work/build" "${generator[@]}" \
  -DCMAKE_BUILD_TYPE=Release -DCMS_BUILD_TESTS=OFF -DCMS_WITH_API=OFF -DCMS_WITH_JUDGE=OFF \
  >/dev/null || fail "cmake could not configure the workspace"

if command -v cargo >/dev/null; then
  note "the CLI crate is well formed"
  "${CARGO:-cargo}" metadata --no-deps --format-version 1 --manifest-path "$repo/Cargo.toml" \
    >/dev/null || fail "Cargo.toml is not a valid workspace"
  if command -v rustfmt >/dev/null; then
    "${CARGO:-cargo}" fmt --check --manifest-path "$repo/Cargo.toml" \
      || fail "the crate is not formatted"
  fi
fi

note "falsification: a header that needs an include must fail"
cat > "$work/broken.hpp" <<'CPP'
#pragma once

inline std::string missing_include() { return {}; }
CPP
if compile_header "$work/broken.hpp" 2>/dev/null; then
  fail "a header using std::string without <string> compiled; the check proves nothing"
fi

printf 'scaffold gate: OK\n'
