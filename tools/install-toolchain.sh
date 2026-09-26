#!/bin/sh
# Download the m68k-atari-mintelf cross compiler (GCC 15.2) and the Atari
# libraries (mintlib, fdlibm, gemlib) into ~/.local/opt/cross-mint.
# Prebuilt Linux x86_64 binaries by Thorsten Otto: https://tho-otto.m68k.eu/crossmint.php
# No root needed. The Makefile looks here by default (override with TOOLCHAIN=).
set -eu

DEST="${1:-$HOME/.local/opt/cross-mint}"
BASE=https://tho-otto.m68k.eu/download/mint
FILES="
binutils-2.45-mintelf-20250812-bin-linux64.tar.xz
gcc-15.2.0-mintelf-20250810-bin-linux64.tar.xz
mintbin-0.4-mintelf-20230911-bin-linux64.tar.xz
mintlib-0.60.1-mintelf-20240718-dev.tar.xz
fdlibm-20240425-mintelf-dev.tar.xz
gemlib-0.44.0-mintelf-20240425-dev.tar.xz
"

mkdir -p "$DEST"
cd "$DEST"
for f in $FILES; do
    echo "fetching $f"
    curl -sSfLO "$BASE/$f"
    tar xJf "$f"
    rm "$f"
done
echo "installed into $DEST/usr/bin"
"$DEST/usr/bin/m68k-atari-mintelf-gcc" --version | head -1
