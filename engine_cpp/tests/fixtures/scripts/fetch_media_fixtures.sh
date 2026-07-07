#!/usr/bin/env bash
# Downloads real media test fixtures for ebbackup tests.
# Usage: ./fetch_media_fixtures.sh [--mirror]

set -euo pipefail
USE_MIRROR=0
if [[ "${1:-}" == "--mirror" ]]; then USE_MIRROR=1; fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MEDIA_ROOT="$(cd "$SCRIPT_DIR/../real_world/media" && pwd)"

download() {
  local url="$1" dest="$2"
  mkdir -p "$(dirname "$dest")"
  echo "  GET $url"
  curl -fsSL "$url" -o "$dest"
}

try_download() {
  local primary="$1" mirror="$2" dest="$3"
  local first second
  if [[ "$USE_MIRROR" -eq 1 ]]; then first="$mirror"; second="$primary"; else first="$primary"; second="$mirror"; fi
  for url in "$first" "$second"; do
    [[ -z "$url" ]] && continue
    if download "$url" "$dest" 2>/dev/null; then return 0; fi
    echo "  warn: failed $url" >&2
  done
  return 1
}

rm -rf "$MEDIA_ROOT"
mkdir -p "$MEDIA_ROOT"

JPG_PRIMARY="https://raw.githubusercontent.com/python-pillow/Pillow/main/Tests/images/hopper.jpg"
JPG_MIRROR="https://cdn.jsdelivr.net/gh/python-pillow/Pillow@main/Tests/images/hopper.jpg"
WEBP_PRIMARY="https://www.gstatic.com/webp/gallery/1.webp"
WEBP_MIRROR="https://cdn.jsdelivr.net/gh/webmproject/webp@main/examples/1.webp"
WEBP2_PRIMARY="https://www.gstatic.com/webp/gallery/2.webp"
WEBP2_MIRROR="https://cdn.jsdelivr.net/gh/webmproject/webp@main/examples/2.webp"
MP4_PRIMARY="https://raw.githubusercontent.com/mdn/learning-area/master/html/multimedia-and-embedding/video-and-audio-content/rabbit320.mp4"
MP4_MIRROR="https://cdn.jsdelivr.net/gh/mdn/learning-area@master/html/multimedia-and-embedding/video-and-audio-content/rabbit320.mp4"
GIF_PRIMARY="https://raw.githubusercontent.com/python-pillow/Pillow/main/Tests/images/hopper.gif"
GIF_MIRROR="https://cdn.jsdelivr.net/gh/python-pillow/Pillow@main/Tests/images/hopper.gif"
NINJA_PRIMARY="https://live.sysinternals.com/Sigcheck.exe"
NINJA_MIRROR="https://live.sysinternals.com/Sigcheck.exe"

echo "Fetching images/earth.jpg ..."
try_download "$JPG_PRIMARY" "$JPG_MIRROR" "$MEDIA_ROOT/images/earth.jpg"
echo "Fetching images/demo.webp ..."
try_download "$WEBP_PRIMARY" "$WEBP_MIRROR" "$MEDIA_ROOT/images/demo.webp"
echo "Fetching images/demo_alt.webp ..."
try_download "$WEBP2_PRIMARY" "$WEBP2_MIRROR" "$MEDIA_ROOT/images/demo_alt.webp"
echo "Fetching video/sample.mp4 ..."
try_download "$MP4_PRIMARY" "$MP4_MIRROR" "$MEDIA_ROOT/video/sample.mp4"
echo "Fetching nested/.../badge.gif ..."
try_download "$GIF_PRIMARY" "$GIF_MIRROR" "$MEDIA_ROOT/nested/l0/l1/l2/badge.gif"

mkdir -p "$MEDIA_ROOT/nested/l0/l1/l2"
cp "$MEDIA_ROOT/video/sample.mp4" "$MEDIA_ROOT/nested/l0/l1/l2/clip.mp4"

echo "Building archives/sample.zip ..."
ZIP_STAGE="$(mktemp -d)"
printf 'ebbackup zip fixture\n' > "$ZIP_STAGE/readme.txt"
printf '{"in_archive":true}\n' > "$ZIP_STAGE/inner.json"
mkdir -p "$MEDIA_ROOT/archives"
(
  cd "$ZIP_STAGE"
  zip -q -r "$MEDIA_ROOT/archives/sample.zip" .
)
rm -rf "$ZIP_STAGE"

echo "Building nested/l0/l1/payload.tar.gz ..."
TAR_STAGE="$(mktemp -d)"
printf 'tar payload\n' > "$TAR_STAGE/payload.txt"
mkdir -p "$MEDIA_ROOT/nested/l0/l1"
tar -czf "$MEDIA_ROOT/nested/l0/l1/payload.tar.gz" -C "$TAR_STAGE" .
rm -rf "$TAR_STAGE"

echo "Fetching binaries/sample.exe (Sigcheck) ..."
try_download "$NINJA_PRIMARY" "$NINJA_MIRROR" "$MEDIA_ROOT/binaries/sample.exe"

cat > "$MEDIA_ROOT/meta.json" <<'EOF'
{"fixture":"media","version":1,"formats":["jpg","webp","mp4","gif","zip","tar.gz","exe"]}
EOF

cat > "$MEDIA_ROOT/ATTRIBUTION.md" <<EOF
# Media fixture attribution

Downloaded for ebbackup integration tests. Refresh with:
\`engine_cpp/tests/fixtures/scripts/fetch_media_fixtures.sh\` (add \`--mirror\` for jsDelivr-first)

Sources:
- \`images/earth.jpg\`: Pillow test image (HPND)
- \`images/demo.webp\`: Google WebP gallery / webmproject examples
- \`video/sample.mp4\`: MDN learning-area sample (CC0)
- \`nested/l0/l1/l2/badge.gif\`: Pillow test image (HPND)
- \`nested/l0/l1/l2/clip.mp4\`: copy of video/sample.mp4
- \`archives/sample.zip\`: generated locally
- \`nested/l0/l1/payload.tar.gz\`: generated locally
- \`binaries/sample.exe\`: Microsoft Sysinternals Sigcheck (test binary)

Generated: $(date +%Y-%m-%d)
EOF

python3 - <<'PY' "$MEDIA_ROOT"
import hashlib, json, os, sys
root = sys.argv[1]
entries = []
for dirpath, _, files in os.walk(root):
    for name in sorted(files):
        if name == "media_manifest.json":
            continue
        path = os.path.join(dirpath, name)
        rel = os.path.relpath(path, root).replace("\\", "/")
        with open(path, "rb") as f:
            h = hashlib.sha256(f.read()).hexdigest()
        entries.append({"path": rel, "sha256": h})
entries.sort(key=lambda e: e["path"])
with open(os.path.join(root, "media_manifest.json"), "w", encoding="utf-8") as out:
    json.dump(entries, out, separators=(",", ":"))
total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(root) for f in fs)
print(f"Done: {len(entries)} files, {total / (1024*1024):.2f} MB")
PY
