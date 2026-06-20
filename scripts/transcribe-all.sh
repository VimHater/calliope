#!/usr/bin/env bash

set -uxe

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"

src="${1:-$root/musicxml}"
out="${2:-$root/transcribed}"
calliope="$root/build/calliope"

if [[ ! -x "$calliope" ]]; then
    echo "error: $calliope not found — build first: cmake --build build" >&2
    exit 1
fi
if [[ ! -d "$src" ]]; then
    echo "error: source dir '$src' not found" >&2
    exit 1
fi

mkdir -p "$out"

shopt -s nullglob nocaseglob
files=("$src"/*.mxl "$src"/*.xml "$src"/*.musicxml)
shopt -u nullglob nocaseglob

if [[ ${#files[@]} -eq 0 ]]; then
    echo "no MusicXML files in '$src'" >&2
    exit 1
fi

ok=0
fail=0
for f in "${files[@]}"; do
    name="$(basename "$f")"
    name="${name%.*}"
    cal="$out/$name.cal"
    warn="$out/$name.warn"
    if "$calliope" "$f" -o "$cal" 2>"$warn"; then
        # keep the .warn only when it has content (unsupported tags)
        [[ -s "$warn" ]] || rm -f "$warn"
        printf '  ok    %s\n' "$name.cal"
        ok=$((ok + 1))
    else
        printf '  FAIL  %s\n' "$name"  >&2
        cat "$warn" >&2
        fail=$((fail + 1))
    fi
done

echo "transcribed $ok file(s) to $out${fail:+ ($fail failed)}"
[[ $fail -eq 0 ]]
