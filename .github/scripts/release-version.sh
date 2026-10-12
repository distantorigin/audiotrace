#!/bin/bash
set -euo pipefail

platform="${INPUT_PLATFORM:-all}"
if [[ "$GITHUB_EVENT_NAME" == push ]]; then
    case "$GITHUB_REF_NAME" in
        windows-v*) platform=windows ;;
        macos-v*) platform=macos ;;
        *) echo 'Use a windows-vMAJOR.MINOR.PATCH or macos-vMAJOR.MINOR.PATCH tag.' >&2; exit 1 ;;
    esac
fi
case "$platform" in
    all|windows|macos) ;;
    *) echo "Unknown platform: $platform" >&2; exit 1 ;;
esac

echo "platform=$platform" >> "$GITHUB_OUTPUT"
for target in windows macos; do
    if [[ "$platform" != all && "$platform" != "$target" ]]; then continue; fi
    version="$(printf 'AT_VERSION\n' | "${CC:-cc}" -E -P -x c -include "$target/version.h" - | tr -d '[:space:]\"')"
    if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
        echo "$target/version.h must define a major.minor.patch version." >&2
        exit 1
    fi
    if [[ "$target" == windows ]]; then
        IFS=. read -r major minor patch <<< "$version"
        for component in "$major" "$minor" "$patch"; do
            if [[ ${#component} -gt 5 ]] || (( component > 65535 )); then
                echo 'Windows version components must fit in 16 bits.' >&2
                exit 1
            fi
        done
    fi
    if [[ "$GITHUB_EVENT_NAME" == push && "$GITHUB_REF_NAME" != "$target-v$version" ]]; then
        echo "Tag must be $target-v$version to match $target/version.h." >&2
        exit 1
    fi
    echo "$target-version=$version" >> "$GITHUB_OUTPUT"
done
