#!/bin/bash
# One-time setup: creates a self-signed code-signing certificate in the login
# keychain. build.sh picks it up automatically, so every rebuild keeps the same
# signing identity and macOS keeps GoViet's Accessibility permission on reinstall.
#
#   scripts/setup-signing.sh
#
# Remove it later with: security delete-identity -c "GoViet Local Signing"
set -euo pipefail

NAME="GoViet Local Signing"
KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

if security find-certificate -c "$NAME" "$KEYCHAIN" >/dev/null 2>&1; then
    echo "\"$NAME\" already exists in the login keychain."
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
EOF

/usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -days 7300 -config "$TMP/cert.cnf" \
    -keyout "$TMP/key.pem" -out "$TMP/cert.pem" 2>/dev/null
# The PKCS#12 password only protects the temporary file during import.
PASS="$(/usr/bin/openssl rand -hex 16)"
/usr/bin/openssl pkcs12 -export -name "$NAME" -inkey "$TMP/key.pem" -in "$TMP/cert.pem" \
    -passout "pass:$PASS" -out "$TMP/identity.p12"

# -T lets codesign use the private key without a keychain prompt.
security import "$TMP/identity.p12" -k "$KEYCHAIN" -P "$PASS" -T /usr/bin/codesign >/dev/null
echo "Created \"$NAME\" in the login keychain."
