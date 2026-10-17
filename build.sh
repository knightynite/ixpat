#!/bin/bash
# Builds iXpat (module Thrum) into a runnable .app bundle (menubar-only).
#   ./build.sh          release build, stable local signing identity if present
#   ./build.sh debug    debug build
set -euo pipefail
cd "$(dirname "$0")"
MODE="${1:-release}"
APP="iXpat.app"; BIN="Thrum"; EXE="iXpat"; IDENTIFIER="com.alcybr.thrum"   # bundle id kept so TCC grants survive the rename
SHORT_VERSION="${THRUM_VERSION:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo 0.1.0)}"
[ -z "$SHORT_VERSION" ] && SHORT_VERSION="0.1.0"
BUILD_VERSION="$(git rev-list --count HEAD 2>/dev/null || echo 1)"

echo "▸ swift build -c $MODE"
swift build -c "$MODE"
BIN_PATH="$(swift build -c "$MODE" --show-bin-path)/$BIN"

echo "▸ assembling $APP ($SHORT_VERSION build $BUILD_VERSION)"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN_PATH" "$APP/Contents/MacOS/$EXE"

ICON_SRC="Resources/AppIcon-1024.png"
if [ ! -f "$ICON_SRC" ]; then
    echo "▸ rendering app icon"; swift tools/make_icon.swift "$ICON_SRC"
fi
ICONSET_DIR="$(mktemp -d)"; ICONSET="$ICONSET_DIR/AppIcon.iconset"; mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$ICON_SRC" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    sips -z $((size*2)) $((size*2)) "$ICON_SRC" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"; rm -rf "$ICONSET_DIR"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>            <string>iXpat</string>
    <key>CFBundleDisplayName</key>     <string>iXpat</string>
    <key>CFBundleIdentifier</key>      <string>${IDENTIFIER}</string>
    <key>CFBundleVersion</key>         <string>${BUILD_VERSION}</string>
    <key>CFBundleShortVersionString</key><string>${SHORT_VERSION}</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>CFBundleExecutable</key>      <string>iXpat</string>
    <key>CFBundleIconFile</key>        <string>AppIcon</string>
    <key>CFBundleIconName</key>        <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>  <string>14.0</string>
    <key>LSUIElement</key>             <true/>
    <key>NSPrincipalClass</key>        <string>NSApplication</string>
    <key>NSHumanReadableCopyright</key><string>© AlCybr</string>
</dict>
</plist>
PLIST

# A stable local signing identity keeps TCC grants across rebuilds. Name it in
# THRUM_SIGN_ID or in an untracked .sign-id file; otherwise the build is ad-hoc signed.
SIGN_ID="${THRUM_SIGN_ID:-}"
[ -z "$SIGN_ID" ] && [ -f .sign-id ] && SIGN_ID="$(tr -d '\n' < .sign-id)"
if [ -n "$SIGN_ID" ] && security find-identity -p codesigning 2>/dev/null | grep -Fq -- "$SIGN_ID"; then
    echo "▸ codesign with local identity: $SIGN_ID"
    codesign --force --options runtime --sign "$SIGN_ID" --identifier "$IDENTIFIER" "$APP"
else
    echo "▸ ad-hoc codesign (TCC grants will not persist across rebuilds)"
    codesign --force --options runtime --sign - --identifier "$IDENTIFIER" "$APP"
fi
echo "✓ built $APP  ($SHORT_VERSION)   run:  open ./$APP"
