#!/bin/bash
set -euo pipefail
image="$1"
architecture="$2"
version="$3"
mount="$(mktemp -d)"
mounted=false
cleanup() {
    if "$mounted"; then
        hdiutil detach "$mount" -quiet || return 1
    fi
    rmdir "$mount"
}
trap cleanup EXIT
hdiutil attach "$image" -mountpoint "$mount" -readonly -nobrowse -noautoopen -quiet
mounted=true
test -d "$mount/AudioTrace.app"
test -x "$mount/AudioTrace.app/Contents/MacOS/AudioTrace"
test -L "$mount/Applications"
test "$(readlink "$mount/Applications")" = /Applications
test ! -e "$mount/README.md"
test "$(xcrun lipo -archs "$mount/AudioTrace.app/Contents/MacOS/AudioTrace")" = "$architecture"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$mount/AudioTrace.app/Contents/Info.plist")" = "$version"
codesign --verify --strict "$mount/AudioTrace.app"
echo 'DMG app, Applications shortcut, architecture, version, and signature checks passed.'
