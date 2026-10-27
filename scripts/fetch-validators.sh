#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Downloads pinned release binaries of the plugin validators for this OS into <dest>:
#
#   scripts/fetch-validators.sh tools-bin
#
# leaves tools-bin/clap-validator and tools-bin/pluginval (with .exe on Windows; on macOS,
# pluginval is a symlink into pluginval.app). tools-bin/ is git-ignored. Each archive is
# checked against its pinned SHA-256 before anything is unpacked.
#
# Pinned on 2026-10-26 to the latest stable releases:
#   clap-validator 0.4.1   https://github.com/free-audio/clap-validator/releases/tag/0.4.1
#   pluginval      v1.0.4  https://github.com/Tracktion/pluginval/releases/tag/v1.0.4
#                          (v2.0.0-beta3 exists but is a pre-release)

set -euo pipefail

CLAP_VALIDATOR_VERSION=0.4.1
CLAP_VALIDATOR_BUILD=0.4.1-127-g152b982
PLUGINVAL_VERSION=v1.0.4

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <dest>" >&2
    exit 2
fi
dest="$1"

case "$(uname -s)" in
    Darwin)
        cv_asset="clap-validator-${CLAP_VALIDATOR_BUILD}-macos-universal.zip"
        cv_sha256=bbec8cd7d18274e549d5d8c12ece3cec54be966129388dd2e742b9957f2ba9f1
        pv_asset=pluginval_macOS.zip
        pv_sha256=3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f
        exe=""
        ;;
    Linux)
        cv_asset="clap-validator-${CLAP_VALIDATOR_BUILD}-ubuntu-22.04.zip"
        cv_sha256=49edadcfb407ea0dd946ce418300e853fbd2660fa4b0d00e4f19ff8eef24ad90
        pv_asset=pluginval_Linux.zip
        pv_sha256=c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352
        exe=""
        ;;
    MINGW* | MSYS* | CYGWIN*)
        cv_asset="clap-validator-${CLAP_VALIDATOR_BUILD}-windows.zip"
        cv_sha256=d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10
        pv_asset=pluginval_Windows.zip
        pv_sha256=c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab
        exe=".exe"
        ;;
    *)
        echo "error: unsupported OS '$(uname -s)'" >&2
        exit 1
        ;;
esac

cv_url="https://github.com/free-audio/clap-validator/releases/download/${CLAP_VALIDATOR_VERSION}/${cv_asset}"
pv_url="https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/${pv_asset}"

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# fetch <url> <sha256> <file>
fetch() {
    echo "fetching $1"
    curl -fsSL --retry 3 -o "$3" "$1"
    local actual
    actual="$(sha256_of "$3")"
    if [[ "$actual" != "$2" ]]; then
        echo "error: $3 has SHA-256 $actual, expected $2" >&2
        exit 1
    fi
}

mkdir -p "$dest"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# clap-validator: the zip holds the binary, or (macOS, Linux) a tarball holding it.
fetch "$cv_url" "$cv_sha256" "$work/clap-validator.zip"
mkdir "$work/cv"
unzip -q "$work/clap-validator.zip" -d "$work/cv"
for tarball in "$work"/cv/*.tar.gz; do
    [[ -e "$tarball" ]] || continue
    tar -xzf "$tarball" -C "$work/cv"
done
cv_bin="$(find "$work/cv" -type f -name "clap-validator${exe}" | head -n 1)"
if [[ -z "$cv_bin" ]]; then
    echo "error: no clap-validator${exe} in $cv_asset" >&2
    exit 1
fi
cp "$cv_bin" "$dest/clap-validator${exe}"
chmod +x "$dest/clap-validator${exe}"

# pluginval: a bare binary, or (macOS) an app bundle, linked as $dest/pluginval.
fetch "$pv_url" "$pv_sha256" "$work/pluginval.zip"
mkdir "$work/pv"
unzip -q "$work/pluginval.zip" -d "$work/pv"
if [[ -d "$work/pv/pluginval.app" ]]; then
    rm -rf "$dest/pluginval.app" "$dest/pluginval"
    cp -R "$work/pv/pluginval.app" "$dest/"
    ln -s pluginval.app/Contents/MacOS/pluginval "$dest/pluginval"
else
    cp "$work/pv/pluginval${exe}" "$dest/pluginval${exe}"
    chmod +x "$dest/pluginval${exe}"
fi

if [[ "$(uname -s)" == Darwin ]]; then
    # Downloaded with curl, so there is no quarantine flag; clear it anyway in case.
    xattr -dr com.apple.quarantine "$dest/clap-validator" "$dest/pluginval.app" 2>/dev/null || true
fi

echo "clap-validator ${CLAP_VALIDATOR_VERSION} and pluginval ${PLUGINVAL_VERSION} are in $dest"
