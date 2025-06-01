#!/bin/bash

# Usage: ./add_binary.sh /path/to/binary
# E.g.: ./add_binary.sh /usr/bin/acpidump

if [ -z "$1" ]; then
    echo "Usage: $0 /path/to/binary"
    exit 1
fi

script_dir=$(dirname "$(realpath "$0")")
project_root=$(realpath "$script_dir/..")

BINARY="$1"
BINARY_NAME=$(basename "$BINARY")

# Make sure we're not missing any directory
mkdir -p "$INITRAM_DIR/bin"
mkdir -p "$INITRAM_DIR/usr/bin"
mkdir -p "$INITRAM_DIR/lib"
mkdir -p "$INITRAM_DIR/lib64"
mkdir -p "$INITRAM_DIR/lib/x86_64-linux-gnu"

# Get and copy all dependencies
ldd "$BINARY" | grep "=>" | awk '{print $3}' | while read -r LIB; do
    if [ -n "$LIB" ]; then
        # Create target directory
        TARGET_DIR=$(dirname "$LIB")
        mkdir -p "$INITRAM_DIR$TARGET_DIR"

        # Copy library
        cp "$LIB" "$INITRAM_DIR$TARGET_DIR/"
    fi
done

# Don't forget the dynamic linker (ld-linux)
LINKER=$(ldd "$BINARY" | grep "ld-linux" | awk '{print $1}')
if [ -n "$LINKER" ]; then
    LINKER_DIR=$(dirname "$LINKER")
    mkdir -p "$INITRAM_DIR$LINKER_DIR"
    cp "$LINKER" "$INITRAM_DIR$LINKER_DIR/"
fi

echo "Added dependencies of $BINARY_NAME to initramfs"