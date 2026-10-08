#!/bin/bash
# Publishes a new GoViet version on GitHub Releases. Installed copies find it
# through the Sparkle feed and show an "update" button.
#
#   scripts/release.sh 1.2.0 [release-notes.md]
#
# Bumps the version in Info.plist, builds a universal app, signs the zip, the
# release notes and the appcast with the Sparkle EdDSA key (login keychain,
# account vn.goviet.app), then commits, tags, pushes and creates the release.
# Without a notes file, the commit subjects since the previous tag are used.
set -euo pipefail

VERSION="${1:?usage: scripts/release.sh <version> [release-notes.md]}"
NOTES_FILE="${2:-}"
REPO=tuchung95/goviet
ACCOUNT=vn.goviet.app
LOCAL_IDENTITY="GoViet Local Signing"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLIST="$ROOT/Sources/Support/Info.plist"
BUILD_DIR="${BUILD_DIR:-$HOME/Library/Caches/GoViet-build}"
TAG="v$VERSION"
FEED_URL="https://github.com/$REPO/releases/latest/download/appcast.xml"
PREFIX="https://github.com/$REPO/releases/download/$TAG/"
NAME="GoViet-$VERSION"

fail() { echo "error: $*" >&2; exit 1; }
cd "$ROOT"

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "version must look like 1.2.3"
[ "$(git branch --show-current)" = main ] || fail "release from the main branch"
[ -z "$(git status --porcelain)" ] || fail "commit or stash your changes first"
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && fail "tag $TAG already exists"
[ -z "$NOTES_FILE" ] || [ -f "$NOTES_FILE" ] || fail "notes file not found: $NOTES_FILE"
gh auth status >/dev/null 2>&1 || fail "log in with: gh auth login"
# Every release must carry the same signing identity, or Sparkle rejects the
# update and macOS drops the Accessibility permission.
if [ -z "${SIGN_IDENTITY:-}" ]; then
    security find-certificate -c "$LOCAL_IDENTITY" >/dev/null 2>&1 \
        || fail "\"$LOCAL_IDENTITY\" not found; run scripts/setup-signing.sh"
elif [ "$SIGN_IDENTITY" = "-" ]; then
    fail "ad-hoc builds can't be released"
fi

WORK="$(mktemp -d)"
cleanup() {
    rm -rf "$WORK"
    # Undo the version bump if we stopped before committing it.
    git diff --quiet -- "$PLIST" || git checkout -- "$PLIST"
}
trap cleanup EXIT

BUILD=$(( $(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$PLIST") + 1 ))
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" -c "Set :CFBundleVersion $BUILD" "$PLIST"
echo "==> Releasing GoViet $VERSION (build $BUILD)"

ARCHS="arm64 x86_64" CONFIG=release BUILD_DIR="$BUILD_DIR" "$ROOT/scripts/build.sh"
APP="$BUILD_DIR/GoViet.app"
SPARKLE_BIN="$(ls -d "$BUILD_DIR"/Sparkle-*/bin | tail -1)"

ARCHIVES="$WORK/archives"
mkdir -p "$ARCHIVES"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$APP" "$ARCHIVES/$NAME.zip"
if [ -n "$NOTES_FILE" ]; then
    cp "$NOTES_FILE" "$ARCHIVES/$NAME.md"
else
    PREV_TAG="$(git describe --tags --abbrev=0 2>/dev/null || true)"
    {
        echo "# GoViet $VERSION"
        echo
        git log --no-merges --pretty='- %s' ${PREV_TAG:+"$PREV_TAG"..HEAD}
    } > "$ARCHIVES/$NAME.md"
fi

# Keep earlier versions in the feed; never build on an unverified one.
if curl -sSLf -o "$ARCHIVES/appcast.xml" "$FEED_URL" 2>/dev/null; then
    "$SPARKLE_BIN/sign_update" --verify --account "$ACCOUNT" "$ARCHIVES/appcast.xml" \
        || fail "the published appcast has an invalid signature"
    python3 - "$ARCHIVES/appcast.xml" "$BUILD" <<'PY' || fail "build $BUILD is not newer than the published ones"
import sys, xml.etree.ElementTree as ET
ns = '{http://www.andymatuschak.org/xml-namespaces/sparkle}'
for item in ET.parse(sys.argv[1]).findall('./channel/item'):
    version = item.findtext(ns + 'version')
    enclosure = item.find('enclosure')
    if version is None and enclosure is not None:
        version = enclosure.get(ns + 'version')
    if version is None or not version.isdecimal() or int(version) >= int(sys.argv[2]):
        sys.exit(1)
PY
else
    rm -f "$ARCHIVES/appcast.xml"
    echo "==> No published appcast yet: this is the first release"
fi

echo "==> Signing the archive, release notes and appcast"
"$SPARKLE_BIN/generate_appcast" --account "$ACCOUNT" \
    --download-url-prefix "$PREFIX" --release-notes-url-prefix "$PREFIX" \
    --link "https://github.com/$REPO" \
    --maximum-versions 5 --maximum-deltas 0 "$ARCHIVES"
"$SPARKLE_BIN/sign_update" --verify --account "$ACCOUNT" "$ARCHIVES/appcast.xml"
grep -q "${PREFIX}${NAME}.zip" "$ARCHIVES/appcast.xml" || fail "appcast doesn't point at $NAME.zip"

echo "==> Committing, tagging and pushing $TAG"
git commit -q -m "Release $TAG" -- "$PLIST"
git tag -a "$TAG" -m "GoViet $VERSION"
git push -q origin main "$TAG"

echo "==> Creating the GitHub release"
gh release create "$TAG" --repo "$REPO" --title "GoViet $VERSION" --latest \
    --notes-file "$ARCHIVES/$NAME.md" \
    "$ARCHIVES/$NAME.zip" "$ARCHIVES/$NAME.md" "$ARCHIVES/appcast.xml"
echo "==> Released https://github.com/$REPO/releases/tag/$TAG"
