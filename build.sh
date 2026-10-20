#!/bin/bash
# Builds iXpat into a runnable .app bundle (menubar-only).
#   ./build.sh          release build, stable local signing identity if present
#   ./build.sh debug    debug build
#   ./build.sh dist     universal build, Developer ID signed, notarized and stapled app + DMG in dist/
set -euo pipefail
cd "$(dirname "$0")"
MODE="${1:-release}"
APP="iXpat.app"; BIN="iXpat"; EXE="iXpat"; IDENTIFIER="com.alcybr.ixpat"
SHORT_VERSION="${IXPAT_VERSION:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo 0.1.0)}"
[ -z "$SHORT_VERSION" ] && SHORT_VERSION="0.1.0"
BUILD_VERSION="$(git rev-list --count HEAD 2>/dev/null || echo 1)"

# dist refuses to fall back to a local or ad-hoc signature: TCC keys grants to the
# signature, so one public build signed any other way would make every later
# Developer ID update look like a different app and drop the user's grants.
if [ "$MODE" = "dist" ]; then
    DIST_ID="${IXPAT_DIST_ID:-$(security find-identity -v -p codesigning | sed -n 's/.*"\(Developer ID Application: .*\)"$/\1/p' | head -1)}"
    NOTARY_PROFILE="${IXPAT_NOTARY_PROFILE:-}"
    [ -z "$NOTARY_PROFILE" ] && [ -f .notary-profile ] && NOTARY_PROFILE="$(tr -d '\n' < .notary-profile)"
    if [ -z "$DIST_ID" ]; then
        echo "✗ dist needs a Developer ID Application identity (or IXPAT_DIST_ID)"; exit 1
    fi
    if [ -z "$NOTARY_PROFILE" ] || ! xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1; then
        echo "✗ dist needs a notarytool keychain profile in IXPAT_NOTARY_PROFILE or an untracked .notary-profile file"
        echo "  create one: xcrun notarytool store-credentials <name> --apple-id <id> --team-id <team> --password <app-specific>"
        exit 1
    fi
    BUILD_ARGS=(-c release --arch arm64 --arch x86_64)
else
    BUILD_ARGS=(-c "$MODE")
fi

echo "▸ swift build ${BUILD_ARGS[*]}"
swift build "${BUILD_ARGS[@]}"
BIN_PATH="$(swift build "${BUILD_ARGS[@]}" --show-bin-path)/$BIN"

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
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
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
    <key>NSHighResolutionCapable</key> <true/>
    <key>NSPrincipalClass</key>        <string>NSApplication</string>
    <key>NSHumanReadableCopyright</key><string>© 2026 AL Najafi. MIT License.</string>
</dict>
</plist>
PLIST

if [ "$MODE" = "dist" ]; then
    DIST_DIR="dist"; DMG="$DIST_DIR/iXpat-$SHORT_VERSION.dmg"
    mkdir -p "$DIST_DIR"
    echo "▸ codesign with $DIST_ID"
    codesign --force --options runtime --timestamp --sign "$DIST_ID" --identifier "$IDENTIFIER" "$APP"
    codesign --verify --strict --verbose=2 "$APP"

    # Staple the app itself, not only the DMG, so the copy dragged into
    # /Applications opens offline. ditto keeps the bundle intact; zip -r does not.
    echo "▸ notarizing $APP (profile $NOTARY_PROFILE)"
    APP_ZIP="$DIST_DIR/iXpat-app.zip"
    rm -f "$APP_ZIP"
    ditto -c -k --keepParent "$APP" "$APP_ZIP"
    xcrun notarytool submit "$APP_ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
    rm -f "$APP_ZIP"
    xcrun stapler staple "$APP"
    xcrun stapler validate "$APP"

    echo "▸ building $DMG"
    STAGE="$(mktemp -d)"
    ditto "$APP" "$STAGE/$APP"
    ln -s /Applications "$STAGE/Applications"
    rm -f "$DMG"
    hdiutil create -volname "iXpat" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
    rm -rf "$STAGE"
    codesign --force --timestamp --sign "$DIST_ID" "$DMG"
    echo "▸ notarizing $DMG"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
    xcrun stapler validate "$DMG"
    spctl -a -vvv -t exec "$APP"
    echo "✓ $DMG  ($SHORT_VERSION build $BUILD_VERSION, $(lipo -archs "$APP/Contents/MacOS/$EXE"), $(du -h "$DMG" | cut -f1 | tr -d ' '))"
    exit 0
fi

# A stable local signing identity keeps TCC grants across rebuilds. Name it in
# IXPAT_SIGN_ID or in an untracked .sign-id file; otherwise the build is ad-hoc signed.
SIGN_ID="${IXPAT_SIGN_ID:-}"
[ -z "$SIGN_ID" ] && [ -f .sign-id ] && SIGN_ID="$(tr -d '\n' < .sign-id)"
if [ -n "$SIGN_ID" ] && security find-identity -p codesigning 2>/dev/null | grep -Fq -- "$SIGN_ID"; then
    echo "▸ codesign with local identity: $SIGN_ID"
    codesign --force --options runtime --sign "$SIGN_ID" --identifier "$IDENTIFIER" "$APP"
else
    echo "▸ ad-hoc codesign (TCC grants will not persist across rebuilds)"
    codesign --force --options runtime --sign - --identifier "$IDENTIFIER" "$APP"
fi
echo "✓ built $APP  ($SHORT_VERSION)   run:  open ./$APP"
