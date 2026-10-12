#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then
    echo "Package the macOS app on a Mac." >&2
    exit 1
fi
architecture="${1:-universal}"
case "$architecture" in
    arm64|x86_64|universal) ;;
    *) echo "Usage: bash macos/package.sh [arm64|x86_64|universal]" >&2; exit 1 ;;
esac
app="build/AudioTrace.app"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Invalid app version: $version" >&2
    exit 1
fi
expected="$architecture"
if [[ "$architecture" == universal ]]; then expected='x86_64 arm64'; fi
actual="$(xcrun lipo -archs "$app/Contents/MacOS/AudioTrace")"
if [[ "$(echo "$actual" | tr ' ' '\n' | sort)" != "$(echo "$expected" | tr ' ' '\n' | sort)" ]]; then
    echo "App architectures ($actual) do not match $architecture." >&2
    exit 1
fi
/usr/bin/codesign --verify --strict "$app"
staging="$(mktemp -d "$PWD/build/dmg.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
ditto "$app" "$staging/AudioTrace.app"
ln -s /Applications "$staging/Applications"
output="build/AudioTrace-$version-macos-$architecture.dmg"
hdiutil create -volname AudioTrace -srcfolder "$staging" -fs HFS+ -format UDZO -ov -o "$output"
hdiutil verify "$output"
echo "Packaged $output. Open it and drag AudioTrace to Applications."
