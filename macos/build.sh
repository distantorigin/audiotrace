#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "$(uname -s)" != Darwin ]]; then
    echo "Build the macOS app on a Mac with Xcode Command Line Tools." >&2
    exit 1
fi
version="$(printf 'AT_VERSION\n' | xcrun clang -E -P -x c -include macos/version.h - | tr -d '[:space:]\"')"
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "macos/version.h must define a major.minor.patch version." >&2
    exit 1
fi
app="build/AudioTrace.app"
mkdir -p "$app/Contents/MacOS"
cp macos/Info.plist "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $version" "$app/Contents/Info.plist"
# Build a universal app for Apple Silicon and Intel Macs.
for arch in arm64 x86_64; do
    xcrun clang -arch "$arch" -mmacosx-version-min=14.2 -std=c11 -fobjc-arc -fblocks \
        -Oz -flto -Wl,-dead_strip -Wall -Wextra -Werror macos/main.m macos/ProcessTap.m common/audio_events.c \
        -framework AppKit -framework CoreAudio -o "build/AudioTrace-$arch"
    xcrun strip -x "build/AudioTrace-$arch"
done
xcrun lipo -create build/AudioTrace-arm64 build/AudioTrace-x86_64 -output "$app/Contents/MacOS/AudioTrace"
/usr/bin/plutil -lint "$app/Contents/Info.plist"
/usr/bin/codesign --force --sign "${AUDIO_TRACE_SIGN_ID:--}" --identifier local.audiotrace.app "$app"
/usr/bin/codesign --verify --strict "$app"
echo "Built $app. Run: open build/AudioTrace.app"
