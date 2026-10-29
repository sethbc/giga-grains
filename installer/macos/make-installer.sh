#!/usr/bin/env bash
# Giga Grains — granular instrument and effect
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026, the Giga Grains authors (see git history)
#
# Derived from sst-plugininfra's scripts/installer_mac/make_installer.sh, under its MIT license:
#
#   Copyright 2019-2020, Paul Walker
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
#   and associated documentation files (the "Software"), to deal in the Software without
#   restriction, including without limitation the rights to use, copy, modify, merge, publish,
#   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
#   Software is furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in all copies or
#   substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
#   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
#   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Makes the macOS installer (spec §6.4): a product PKG with one component per product, and a DMG
# holding that PKG and a Licenses folder. The gg-installer target runs it (cmake/products.cmake).
#
#   make-installer.sh --products <dir> --out <dir> --version <X.Y.Z> --full-version <v>
#                     --resources <installer/macos> [--licenses <dir>]
#
#   --products      the five products, as gg-products gathers them: Giga Grains.clap,
#                   Giga Grains.vst3, Giga Grains.component, Giga Grains.app, Giga Grains FX.app.
#   --out           where to write giga-grains-<v>-macos.pkg and giga-grains-<v>-macos.dmg.
#   --version       the numeric version (the packages' version).
#   --full-version  the full version string, <v> (the title, the choices and the file names).
#   --resources     the folder holding distribution.xml.in and Readme.txt.
#   --licenses      the license set, which must hold LICENSE (the AGPL, the license pane); the DMG's
#                   Licenses folder is a copy of it. Default: <products>/licenses.
#
# Signed mode needs all five of MAC_SIGNING_IDENTITY (Developer ID Application, for the DMG),
# MAC_INSTALLER_IDENTITY (Developer ID Installer, for the packages), NOTARY_KEY_FILE,
# NOTARY_KEY_ID and NOTARY_ISSUER_ID (an App Store Connect API key, for notarytool): it signs the
# packages, notarizes and staples the PKG, signs, notarizes and staples the DMG, and asks
# Gatekeeper to accept both. With none of them set, it does the same without signing,
# notarization or the Gatekeeper checks. The products themselves are signed when they're built.
#
# The last line it prints is `signed: yes` or `signed: no`. Exit status: 0 when done; 1 when a
# step fails (a failed notarization prints its submission ID, as does one still in progress after
# 25 minutes, the most it waits for each submission); 2 for bad usage, a signing environment with
# only some of its five variables, or a missing product, before anything is written.

set -euo pipefail

me="make-installer.sh"

# die <message> [exit status, default 1]
die() {
    echo "$me: $1" >&2
    exit "${2:-1}"
}

usage() {
    if [ -n "${1:-}" ]; then
        echo "$me: $1" >&2
    fi
    echo "usage: $me --products <dir> --out <dir> --version <X.Y.Z> --full-version <v>" \
        "--resources <dir> [--licenses <dir>]" >&2
    exit 2
}

products=""
out=""
version=""
full_version=""
resources=""
licenses=""

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --products | --out | --version | --full-version | --resources | --licenses)
                [ $# -ge 2 ] || usage "$1 needs a value"
                case "$1" in
                    --products) products="$2" ;;
                    --out) out="$2" ;;
                    --version) version="$2" ;;
                    --full-version) full_version="$2" ;;
                    --resources) resources="$2" ;;
                    --licenses) licenses="$2" ;;
                esac
                shift 2
                ;;
            -h | --help) usage ;;
            *) usage "unknown argument '$1'" ;;
        esac
    done
    [ -n "$products" ] || usage "--products is required"
    [ -n "$out" ] || usage "--out is required"
    [ -n "$version" ] || usage "--version is required"
    [ -n "$full_version" ] || usage "--full-version is required"
    [ -n "$resources" ] || usage "--resources is required"
    [ -n "$licenses" ] || licenses="$products/licenses"

    local numeric_re='^[0-9]+\.[0-9]+\.[0-9]+$'
    local full_re='^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$'
    if ! [[ "$version" =~ $numeric_re ]]; then
        usage "--version '$version' isn't X.Y.Z"
    fi
    # The full version goes into file names, XML and a sed replacement: only these characters.
    if ! [[ "$full_version" =~ $full_re ]]; then
        usage "--full-version '$full_version' isn't X.Y.Z with an optional -suffix"
    fi
    case "$full_version" in
        "$version" | "$version"-* | "$version"+*) ;;
        *) usage "--full-version '$full_version' doesn't start with --version '$version'" ;;
    esac
}

# Signed when all five variables are set and non-empty, unsigned when none is.
signed="no"
check_signing_env() {
    local var set_names="" unset_names="" count=0
    for var in MAC_SIGNING_IDENTITY MAC_INSTALLER_IDENTITY NOTARY_KEY_FILE NOTARY_KEY_ID \
        NOTARY_ISSUER_ID; do
        if [ -n "${!var:-}" ]; then
            set_names="$set_names $var"
            count=$((count + 1))
        else
            unset_names="$unset_names $var"
        fi
    done
    if [ "$count" -eq 5 ]; then
        signed="yes"
        [ -f "$NOTARY_KEY_FILE" ] || die "NOTARY_KEY_FILE '$NOTARY_KEY_FILE' isn't a file" 2
    elif [ "$count" -ne 0 ]; then
        die "signing needs all five variables; set:$set_names; not set:$unset_names" 2
    fi
}

# The products: file name, component identifier, install location, component file (spec §6.4).
product_table() {
    cat <<'EOF'
Giga Grains.clap|org.sethbc.giga-grains.clap.pkg|/Library/Audio/Plug-Ins/CLAP|giga-grains-clap.pkg
Giga Grains.vst3|org.sethbc.giga-grains.vst3.pkg|/Library/Audio/Plug-Ins/VST3|giga-grains-vst3.pkg
Giga Grains.component|org.sethbc.giga-grains.component.pkg|/Library/Audio/Plug-Ins/Components|giga-grains-component.pkg
Giga Grains.app|org.sethbc.giga-grains.app.pkg|/Applications|giga-grains-app.pkg
Giga Grains FX.app|org.sethbc.giga-grains-fx.app.pkg|/Applications|giga-grains-fx-app.pkg
EOF
}

check_inputs() {
    local name id location file missing=""
    while IFS='|' read -r name id location file <&3; do
        [ -d "$products/$name" ] || missing="$missing '$name'"
    done 3< <(product_table)
    [ -z "$missing" ] || die "missing from '$products':$missing" 2
    [ -f "$resources/distribution.xml.in" ] ||
        die "'$resources' has no distribution.xml.in" 2
    [ -f "$resources/Readme.txt" ] || die "'$resources' has no Readme.txt" 2
    [ -f "$licenses/LICENSE" ] || die "the license set '$licenses' has no LICENSE" 2
}

# The work folder goes on exit; on a failure, so do the PKG and the DMG, so no half-made
# (unsigned, unnotarized) installer is left in --out to be mistaken for a finished one.
work=""
pkg=""
dmg=""
cleanup() {
    local status=$?
    if [ -n "$work" ]; then
        rm -rf "$work"
    fi
    if [ "$status" -ne 0 ] && [ -n "$pkg" ]; then
        rm -f "$pkg" "$dmg"
    fi
}

# Signed mode: each product must already carry a Developer ID signature (the build signs it,
# with -DGG_MACOS_CODESIGN_IDENTITY), or notarization would refuse it only after the upload.
# check_product_signature <staged bundle> <product name>
check_product_signature() {
    local bundle="$1" name="$2" details
    codesign --verify --strict --deep "$bundle" || die "'$name' fails codesign --verify --strict"
    if [ "$signed" = "yes" ]; then
        # Read whole, not piped into grep -q: under pipefail, grep's early exit fails codesign.
        details="$(codesign -dvv "$bundle" 2>&1)" || die "codesign can't read '$name''s signature"
        case "$details" in
            *"Authority=Developer ID Application"*) ;;
            *)
                details="'$name' has no Developer ID Application signature; build the products"
                die "$details with -DGG_MACOS_CODESIGN_IDENTITY set to that identity"
                ;;
        esac
    fi
}

# One component package per product, each built from a staging folder that holds that product
# alone, with the bundle not relocatable: a relocatable bundle installs over any copy with its
# bundle ID elsewhere on the disk (a build tree's, say) instead of into its install location.
build_components() {
    local name id location file stage plist i
    mkdir -p "$work/packages"
    while IFS='|' read -r name id location file <&3; do
        echo "--- $file: '$name' -> $location"
        stage="$work/stage/$file"
        mkdir -p "$stage"
        ditto "$products/$name" "$stage/$name"
        check_product_signature "$stage/$name" "$name"

        plist="$work/stage/$file.plist"
        pkgbuild --quiet --analyze --root "$stage" "$plist"
        i=0
        while plutil -extract "$i" xml1 -o /dev/null "$plist" 2>/dev/null; do
            plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
            i=$((i + 1))
        done

        # Two whole invocations, not one with an optional (--sign <identity>) array: bash 3.2
        # treats "${array[@]}" of an empty array as unset under set -u.
        if [ "$signed" = "yes" ]; then
            pkgbuild --quiet --root "$stage" --component-plist "$plist" --identifier "$id" \
                --version "$version" --install-location "$location" \
                --sign "$MAC_INSTALLER_IDENTITY" "$work/packages/$file"
        else
            pkgbuild --quiet --root "$stage" --component-plist "$plist" --identifier "$id" \
                --version "$version" --install-location "$location" "$work/packages/$file"
        fi
    done 3< <(product_table)
}

# The distribution, from distribution.xml.in, and the panes it names.
make_distribution() {
    sed -e "s/@GG_VERSION_NUMERIC@/$version/g" -e "s/@GG_VERSION_STRING@/$full_version/g" \
        "$resources/distribution.xml.in" >"$work/distribution.xml"
    if grep -q '@GG_' "$work/distribution.xml"; then
        die "distribution.xml.in has a placeholder sed doesn't fill"
    fi
    mkdir -p "$work/resources"
    cp "$resources/Readme.txt" "$work/resources/Readme.txt"
    cp "$licenses/LICENSE" "$work/resources/LICENSE.txt"
}

# build_product <product pkg>. Its local isn't named pkg, so it never shadows the global that
# the EXIT trap reads.
build_product() {
    local product="$1"
    echo "--- $(basename "$product")"
    # Two whole invocations, as in build_components (bash 3.2's empty arrays under set -u).
    if [ "$signed" = "yes" ]; then
        productbuild --quiet --distribution "$work/distribution.xml" \
            --package-path "$work/packages" --resources "$work/resources" \
            --sign "$MAC_INSTALLER_IDENTITY" "$product"
    else
        productbuild --quiet --distribution "$work/distribution.xml" \
            --package-path "$work/packages" --resources "$work/resources" "$product"
    fi
}

# json_field <key> <file>: the value of the top-level <key> in notarytool's JSON output, read by
# plutil (which parses JSON); empty when the file isn't JSON or has no such key.
json_field() {
    plutil -extract "$1" raw -o - "$2" 2>/dev/null || true
}

# Submits one file to the notary service and waits. Fails, printing the submission ID, unless
# the service accepts it.
notarize() {
    local file="$1" log id status
    log="$work/notary-$(basename "$file").json"
    echo "--- notarizing $(basename "$file")"
    # --timeout: notarytool stops waiting after 25 minutes (the service carries on), so a stuck
    # submission fails, printing its ID, inside CI's 70-minute installer step, PKG and DMG both,
    # instead of running into the step's timeout.
    if ! xcrun notarytool submit "$file" --key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" \
        --issuer "$NOTARY_ISSUER_ID" --wait --timeout 25m --output-format json >"$log"; then
        cat "$log"
    fi
    id="$(json_field id "$log")"
    status="$(json_field status "$log")"
    echo "notarytool: $(basename "$file"): submission ${id:-(none)}, status ${status:-(none)}"
    if [ "$status" != "Accepted" ]; then
        if [ -n "$id" ]; then
            local message="notarization of '$file' failed (status '${status:-none}',"
            message="$message submission $id); its log: xcrun notarytool log $id"
            die "$message --key <key file> --key-id <key ID> --issuer <issuer ID>"
        fi
        die "notarization of '$file' failed before a submission ID was assigned"
    fi
}

staple_and_assess() {
    local file="$1"
    shift
    xcrun stapler staple "$file"
    spctl --assess -vv "$@" "$file"
}

# Signs the DMG. Apple's timestamp server fails now and then ("timestamps differ by 300
# seconds", or "could not be reached"), so a failure whose message is about the timestamp is
# retried, up to three attempts, 10 s apart; any other failure (a wrong identity, say) fails at
# once. The locals aren't named pkg or dmg, so they never shadow the globals the EXIT trap reads.
sign_dmg() {
    local image="$1" attempt=1 errors="$work/codesign-dmg.err"
    while ! codesign --force --sign "$MAC_SIGNING_IDENTITY" --timestamp "$image" 2>"$errors"; do
        cat "$errors" >&2
        if ! grep -qiE 'timestamp|could not be reached' "$errors"; then
            die "codesign failed on '$image'"
        fi
        if [ "$attempt" -ge 3 ]; then
            die "codesign --timestamp of '$image' failed $attempt times"
        fi
        echo "codesign --timestamp failed (attempt $attempt of 3); retrying in 10 s"
        sleep 10
        attempt=$((attempt + 1))
    done
    cat "$errors" >&2
    codesign --verify --strict -vv "$image"
}

build_dmg() {
    local product="$1" image="$2" folder="$work/dmg"
    echo "--- $(basename "$image")"
    mkdir -p "$folder/Licenses"
    cp "$product" "$folder/"
    cp -R "$licenses/." "$folder/Licenses/"
    hdiutil create -quiet -ov -fs HFS+ -srcfolder "$folder" -volname "Giga Grains $full_version" \
        "$work/giga-grains.dmg"
    hdiutil convert -quiet -ov -format UDZO -o "$image" "$work/giga-grains.dmg"
}

main() {
    parse_args "$@"
    check_signing_env
    check_inputs

    local base="giga-grains-$full_version-macos"
    pkg="$out/$base.pkg"
    dmg="$out/$base.dmg"
    echo "Giga Grains $full_version (packages $version), signed: $signed"

    work="$(mktemp -d "${TMPDIR:-/tmp}/gg-installer.XXXXXX")"
    trap cleanup EXIT
    mkdir -p "$out"
    rm -f "$pkg" "$dmg"

    build_components
    make_distribution
    build_product "$pkg"
    if [ "$signed" = "yes" ]; then
        notarize "$pkg"
        staple_and_assess "$pkg" --type install
    fi

    build_dmg "$pkg" "$dmg"
    if [ "$signed" = "yes" ]; then
        sign_dmg "$dmg"
        notarize "$dmg"
        staple_and_assess "$dmg" --type open --context context:primary-signature
    fi

    echo "--- wrote:"
    ls -l "$pkg" "$dmg"
    echo "signed: $signed"
}

main "$@"
