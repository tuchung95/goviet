#!/bin/bash
# Builds GoViet.app with only the Xcode Command Line Tools (no Xcode, no XcodeGen).
#
#   scripts/build.sh            # build GoViet.app into $BUILD_DIR
#   scripts/build.sh install    # build, install to /Applications and launch
#
# Environment:
#   CONFIG=release|debug        (default release)
#   ARCHS="arm64 x86_64"        (default: this Mac's architecture)
#   SIGN_IDENTITY=...           (default "GoViet Local Signing" if scripts/setup-signing.sh
#                                was run, else "-" = ad-hoc. A certificate keeps the
#                                same identity, so Accessibility survives reinstalls)
#   BUILD_DIR=...               (default ~/Library/Caches/GoViet-build — must be APFS:
#                                exFAT drives add ._ files that break codesign)
set -euo pipefail

APP_NAME=GoViet
BUNDLE_ID=vn.goviet.app
MIN_MACOS=14.0

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/Sources"
CONFIG="${CONFIG:-release}"
ARCHS="${ARCHS:-$(uname -m)}"
LOCAL_IDENTITY="GoViet Local Signing"
if [ -z "${SIGN_IDENTITY:-}" ]; then
    if security find-certificate -c "$LOCAL_IDENTITY" >/dev/null 2>&1; then
        SIGN_IDENTITY="$LOCAL_IDENTITY"
    else
        SIGN_IDENTITY="-"
    fi
fi
BUILD_DIR="${BUILD_DIR:-$HOME/Library/Caches/GoViet-build}"
APP="$BUILD_DIR/$APP_NAME.app"

# Sparkle (in-app updates) is fetched once into BUILD_DIR: the framework relies
# on symlinks, which exFAT volumes can't store.
SPARKLE_VERSION=2.9.6
SPARKLE_SHA256=52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192
SPARKLE_DIR="$BUILD_DIR/Sparkle-$SPARKLE_VERSION"
if [ ! -d "$SPARKLE_DIR/Sparkle.framework" ]; then
    echo "==> Downloading Sparkle $SPARKLE_VERSION"
    TMP_SPARKLE="$(mktemp -d)"
    curl -sSLf -o "$TMP_SPARKLE/sparkle.tar.xz" \
        "https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz"
    echo "$SPARKLE_SHA256  $TMP_SPARKLE/sparkle.tar.xz" | shasum -a 256 -c - >/dev/null
    mkdir -p "$TMP_SPARKLE/x" && tar -xf "$TMP_SPARKLE/sparkle.tar.xz" -C "$TMP_SPARKLE/x"
    rm -rf "$SPARKLE_DIR" && mkdir -p "$SPARKLE_DIR"
    mv "$TMP_SPARKLE/x/Sparkle.framework" "$TMP_SPARKLE/x/bin" "$TMP_SPARKLE/x/LICENSE" "$SPARKLE_DIR/"
    rm -rf "$TMP_SPARKLE"
fi

# macOS 27+ SDKs declare SwiftUI's @State as a macro whose compiler plugin
# (libSwiftUIMacros) ships only with Xcode. On Command Line Tools, fall back to
# the newest installed SDK that doesn't need it.
needs_swiftui_plugin() {
    grep -qs 'type: "StateMacro"' "$1"/System/Library/Frameworks/SwiftUICore.framework/Modules/SwiftUICore.swiftmodule/*-apple-macos.swiftinterface
}
if [ -z "${SDK:-}" ]; then
    SDK="$(xcrun --sdk macosx --show-sdk-path)"
    PLUGIN_DIR="$(dirname "$(xcrun --find swiftc)")/../lib/swift/host/plugins"
    if [ ! -e "$PLUGIN_DIR/libSwiftUIMacros.dylib" ] && needs_swiftui_plugin "$SDK"; then
        FALLBACK=""
        while IFS= read -r candidate; do
            if ! needs_swiftui_plugin "$candidate"; then FALLBACK="$candidate"; break; fi
        done < <(ls -d "$(dirname "$SDK")"/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV)
        if [ -z "$FALLBACK" ]; then
            echo "error: $SDK needs Xcode's SwiftUI macro plugin and no older SDK is installed." >&2
            echo "       Install Xcode, or pass SDK=/path/to/MacOSX26.x.sdk" >&2
            exit 1
        fi
        SDK="$FALLBACK"
    fi
fi

# Source lists, skipping the AppleDouble (._*) files exFAT volumes create.
collect() { find "$@" -type f ! -name '._*' | sort; }
CPP=(); while IFS= read -r f; do CPP+=("$f"); done < <(collect "$SRC/Engine" -name '*.cpp')
MM=(); while IFS= read -r f; do MM+=("$f"); done < <(collect "$SRC/Platform" -name '*.mm')
SWIFT=(); while IFS= read -r f; do SWIFT+=("$f"); done < <(collect "$SRC/App" -name '*.swift')

if [ "$CONFIG" = debug ]; then
    C_OPT=(-O0 -g -DDEBUG=1)
    SWIFT_OPT=(-Onone -g -DDEBUG)
else
    C_OPT=(-Os)
    SWIFT_OPT=(-O -wmo)
fi
C_COMMON=(-isysroot "$SDK" -I"$SRC/Engine" -I"$SRC/Platform" -std=c++17 -stdlib=libc++
          -Wno-shorten-64-to-32 "${C_OPT[@]}")

echo "==> Building $APP_NAME ($CONFIG, $ARCHS, SDK $(basename "$SDK"))"
BINARIES=()
for arch in $ARCHS; do
    target="$arch-apple-macos$MIN_MACOS"
    obj="$BUILD_DIR/obj/$CONFIG/$arch"
    mkdir -p "$obj"
    OBJS=()

    for f in "${CPP[@]}"; do
        o="$obj/$(basename "${f%.*}").o"
        xcrun clang++ -c "$f" -o "$o" -target "$target" "${C_COMMON[@]}"
        OBJS+=("$o")
    done
    for f in "${MM[@]}"; do
        o="$obj/$(basename "${f%.*}").o"
        xcrun clang++ -x objective-c++ -fobjc-arc -c "$f" -o "$o" -target "$target" "${C_COMMON[@]}"
        OBJS+=("$o")
    done

    xcrun swiftc "${SWIFT[@]}" "${OBJS[@]}" -o "$obj/$APP_NAME" \
        -target "$target" -sdk "$SDK" -swift-version 5 \
        -module-name "$APP_NAME" -parse-as-library \
        -module-cache-path "$BUILD_DIR/ModuleCache" \
        -import-objc-header "$SRC/Support/goviet-Bridging-Header.h" \
        -Xcc "-I$SRC/Engine" -Xcc "-I$SRC/Platform" \
        -F "$SPARKLE_DIR" -framework Sparkle -Xlinker -rpath -Xlinker @executable_path/../Frameworks \
        -lc++ -framework Carbon -framework Cocoa -framework ServiceManagement \
        -Xlinker -dead_strip "${SWIFT_OPT[@]}"
    BINARIES+=("$obj/$APP_NAME")
done

echo "==> Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
if [ ${#BINARIES[@]} -eq 1 ]; then
    cp "${BINARIES[0]}" "$APP/Contents/MacOS/$APP_NAME"
else
    lipo -create "${BINARIES[@]}" -output "$APP/Contents/MacOS/$APP_NAME"
fi
[ "$CONFIG" = debug ] || strip -x "$APP/Contents/MacOS/$APP_NAME"

sed -e "s/\$(EXECUTABLE_NAME)/$APP_NAME/g" \
    -e "s/\$(PRODUCT_BUNDLE_IDENTIFIER)/$BUNDLE_ID/g" \
    -e "s/\$(MACOSX_DEPLOYMENT_TARGET)/$MIN_MACOS/g" \
    "$SRC/Support/Info.plist" > "$APP/Contents/Info.plist"
plutil -lint -s "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# actool needs full Xcode, so build the .icns straight from the asset PNGs.
ICONSET="$BUILD_DIR/AppIcon.iconset"
rm -rf "$ICONSET" && mkdir -p "$ICONSET"
cp "$SRC/Support/Assets.xcassets/AppIcon.appiconset"/icon_*.png "$ICONSET/"
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"
cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
cp "$SPARKLE_DIR/LICENSE" "$APP/Contents/Resources/Sparkle-LICENSE.txt"

mkdir -p "$APP/Contents/Frameworks"
ditto "$SPARKLE_DIR/Sparkle.framework" "$APP/Contents/Frameworks/Sparkle.framework"
SPARKLE_FW="$APP/Contents/Frameworks/Sparkle.framework"
# Sparkle's XPC services are only needed by sandboxed apps; GoViet isn't one.
rm -rf "$SPARKLE_FW/XPCServices" "$SPARKLE_FW/Versions/B/XPCServices"

echo "==> Signing ($SIGN_IDENTITY)"
xattr -cr "$APP"
SIGN_FLAGS=(--force --sign "$SIGN_IDENTITY")
case "$SIGN_IDENTITY" in
    "Developer ID"*) SIGN_FLAGS+=(--options runtime --timestamp) ;;
esac
# Sign from the inside out: Sparkle's helpers, the framework, then the app.
codesign "${SIGN_FLAGS[@]}" "$SPARKLE_FW/Versions/B/Autoupdate"
codesign "${SIGN_FLAGS[@]}" "$SPARKLE_FW/Versions/B/Updater.app"
codesign "${SIGN_FLAGS[@]}" "$SPARKLE_FW"
codesign "${SIGN_FLAGS[@]}" --entitlements "$SRC/Support/goviet.entitlements" "$APP"
codesign --verify --deep --strict "$APP"
echo "==> Built $APP"

if [ "${1:-}" = install ]; then
    DEST="/Applications/$APP_NAME.app"
    requirement() { codesign -d -r- "$1" 2>/dev/null | sed -n -E 's/^(# )?designated => //p'; }
    OLD_REQ="$( [ -d "$DEST" ] && requirement "$DEST" || true )"

    if pgrep -x "$APP_NAME" >/dev/null; then
        echo "==> Quitting running $APP_NAME"
        pkill -x "$APP_NAME" || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do pgrep -x "$APP_NAME" >/dev/null || break; sleep 0.5; done
    fi

    rm -rf "$DEST"
    ditto "$APP" "$DEST"

    # macOS ties the Accessibility grant to the designated requirement. With a
    # certificate it stays the same and the grant carries over; an ad-hoc one
    # changes every build, leaving a stale entry (switched on but no longer
    # valid). Reset that so macOS asks again cleanly.
    if [ -n "$OLD_REQ" ] && [ "$OLD_REQ" != "$(requirement "$DEST")" ]; then
        echo "==> Signing identity changed: resetting Accessibility permission for $BUNDLE_ID"
        tccutil reset Accessibility "$BUNDLE_ID" >/dev/null 2>&1 || true
    fi

    open "$DEST"
    echo "==> Installed $DEST"
fi
