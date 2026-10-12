#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."

bash macos/build.sh universal

binary="build/AudioTrace.app/Contents/MacOS/AudioTrace"
test "$(xcrun lipo -archs "$binary")" = 'x86_64 arm64'
codesign --verify --strict build/AudioTrace.app
echo 'Universal macOS build and signature checks passed.'
