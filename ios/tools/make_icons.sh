#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/EtherPlayer/Brand/EtherPlayerE.png"
OUT="$ROOT/EtherPlayer/Assets.xcassets/AppIcon.appiconset"
mkdir -p "$OUT"
make_icon() {
  local pixels="$1"
  local name="$2"
  /usr/bin/sips -s format png -z "$pixels" "$pixels" "$SRC" --out "$OUT/$name" >/dev/null
}
make_icon 40  'Icon-20@2x.png'
make_icon 60  'Icon-20@3x.png'
make_icon 58  'Icon-29@2x.png'
make_icon 87  'Icon-29@3x.png'
make_icon 80  'Icon-40@2x.png'
make_icon 120 'Icon-40@3x.png'
make_icon 120 'Icon-60@2x.png'
make_icon 180 'Icon-60@3x.png'
make_icon 76  'Icon-76@1x.png'
make_icon 152 'Icon-76@2x.png'
make_icon 167 'Icon-83.5@2x.png'
make_icon 1024 'Icon-1024.png'
echo "EtherPlayer iOS icons generated."
