#!/bin/bash
set -euo pipefail

platform="${INPUT_PLATFORM:-all}"
if [[ "$GITHUB_EVENT_NAME" == push ]]; then
    case "$GITHUB_REF_NAME" in
        v*) platform=all ;;
        *) echo 'Use a vMAJOR.MINOR.PATCH tag.' >&2; exit 1 ;;
    esac
fi
case "$platform" in
    all|windows|macos) ;;
    *) echo "Unknown platform: $platform" >&2; exit 1 ;;
esac

echo "platform=$platform" >> "$GITHUB_OUTPUT"
version=''
for target in windows macos; do
    target_version="$(printf 'AT_VERSION\n' | "${CC:-cc}" -E -P -x c -include "$target/version.h" - | tr -d '[:space:]\"')"
    if [[ ! "$target_version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
        echo "$target/version.h must define a major.minor.patch version." >&2
        exit 1
    fi
    if [[ "$target" == windows ]]; then
        IFS=. read -r major minor patch <<< "$target_version"
        for component in "$major" "$minor" "$patch"; do
            if [[ ${#component} -gt 5 ]] || (( component > 65535 )); then
                echo 'Windows version components must fit in 16 bits.' >&2
                exit 1
            fi
        done
    fi
    if [[ -n "$version" && "$target_version" != "$version" ]]; then
        echo "windows/version.h and macos/version.h must match." >&2
        exit 1
    fi
    version="$target_version"
done
if [[ "$GITHUB_EVENT_NAME" == push && "$GITHUB_REF_NAME" != "v$version" ]]; then
    echo "Tag must be v$version to match both version headers." >&2
    exit 1
fi
echo "version=$version" >> "$GITHUB_OUTPUT"
