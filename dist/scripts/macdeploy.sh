#!/usr/bin/env bash
set -e

if [[ -n "$1" ]]; then
    VERSION="$1"
else
    VERSION=$(grep -m1 '^CMAKE_PROJECT_VERSION:' build/CMakeCache.txt | cut -d= -f2)
fi

cd bin

echo "Running macdeployqt"
macdeployqt qView.app

IMF_DIR=qView.app/Contents/PlugIns/imageformats
if [[ (-f "$IMF_DIR/kimg_heif.dylib" || -f "$IMF_DIR/kimg_heif.so") && -f "$IMF_DIR/libqmacheif.dylib" ]]; then
    # Prefer kimageformats HEIF plugin for proper color space handling
    echo "Removing duplicate HEIF plugin"
    rm "$IMF_DIR/libqmacheif.dylib"
fi
if [[ (-f "$IMF_DIR/kimg_tga.dylib" || -f "$IMF_DIR/kimg_tga.so") && -f "$IMF_DIR/libqtga.dylib" ]]; then
    # Prefer kimageformats TGA plugin which supports more formats
    echo "Removing duplicate TGA plugin"
    rm "$IMF_DIR/libqtga.dylib"
fi

echo "Running codesign"
SIGN_ID="${CODESIGN_CERT_NAME:--}"
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    APP_IDENTIFIER=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "qView.app/Contents/Info.plist")
    codesign --sign "$SIGN_ID" --deep --force --options runtime --timestamp "qView.app"
else
    codesign --sign "$SIGN_ID" --deep --force "qView.app"
fi

echo "Creating disk image"
if [[ -n "$1" ]]; then
    BUILD_NAME=qView-nightly-$1
    DMG_FILENAME=$BUILD_NAME.dmg
    mv qView.app "$BUILD_NAME.app"
    hdiutil create -volname "$BUILD_NAME" -srcfolder "$BUILD_NAME.app" -fs HFS+ "$DMG_FILENAME"
else
    DMG_FILENAME=qView-$VERSION.dmg
    DMG_STAGE=$(mktemp -d)
    cp -R qView.app "$DMG_STAGE/"
    ln -s /Applications "$DMG_STAGE/Applications"
    hdiutil create -volname "qView $VERSION" -srcfolder "$DMG_STAGE" -ov -format UDZO "$DMG_FILENAME"
    rm -rf "$DMG_STAGE"
fi
if [[ "$APPLE_NOTARIZE_REQUESTED" == "true" ]]; then
    codesign --sign "$CODESIGN_CERT_NAME" --timestamp --identifier "$APP_IDENTIFIER.dmg" "$DMG_FILENAME"
    xcrun notarytool submit "$DMG_FILENAME" --apple-id "$APPLE_ID_USER" --password "$APPLE_ID_PASS" --team-id "${CODESIGN_CERT_NAME: -11:10}" --wait
    xcrun stapler staple "$DMG_FILENAME"
    xcrun stapler validate "$DMG_FILENAME"
fi

rm -r *.app