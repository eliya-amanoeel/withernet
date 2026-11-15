#!/bin/bash
# Build one dongle's firmware and sign it for an update over Wi-Fi (on the Mac).
#
#   tools/build-signed.sh checkwatt
#   -> firmware/bridge/build/checkwatt/firmware-signed.bin  (upload this one)
#
# The private key stays outside the repo: $WITHERNET_SIGNING_KEY, by default
# ~/.withernet/signing-key.pem.
set -eu

board=${1:?usage: tools/build-signed.sh <nibe|checkwatt|checkwatt-badwifi>}
repo=$(cd "$(dirname "$0")/.." && pwd)
fw=$repo/firmware/bridge
out=$fw/build/$board
key=${WITHERNET_SIGNING_KEY:-$HOME/.withernet/signing-key.pem}

[ -f "$key" ] || { echo "signing key not found: $key" >&2; exit 1; }
. "$repo/tools/idf-env.sh"

cd "$fw"
idf.py -B "build/$board" -DBOARD="$board" build
espsecure.py sign_data --version 1 --keyfile "$key" \
    --output "$out/firmware-signed.bin" "$out/withernet-bridge.bin"
# Check it against the public key built into the firmware, so a key mix-up
# shows here and not as a refused upload.
espsecure.py verify_signature --version 1 --keyfile signature_verification_key.bin \
    "$out/firmware-signed.bin"
echo
echo "upload: $out/firmware-signed.bin"
