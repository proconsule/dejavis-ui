#!/bin/bash

if [ -z "$1" ]; then
    echo "Error: arg must be the (.exe)"
    exit 1
fi

EXE_PATH="$1"

if [ ! -f "$EXE_PATH" ]; then
    echo "Error: '$EXE_PATH' not exist"
    exit 1
fi

TARGET_DIR=$(dirname "$EXE_PATH")

echo "Deps for: $EXE_PATH"
echo "Output Path: $TARGET_DIR"
echo "------------------------------------------------"

ldd "$EXE_PATH" | grep "=>" | while read -r line; do

    UNIX_PATH=$(echo "$line" | awk '{print $3}')

    if [ -z "$UNIX_PATH" ]; then
        continue
    fi

    UNIX_PATH_LOWER=$(echo "$UNIX_PATH" | tr '[:upper:]' '[:lower:]')
    if [[ "$UNIX_PATH_LOWER" == *"ucrt64"* ||  "$UNIX_PATH_LOWER" == */usr/local/bin/* ]]; then
        
        WIN_PATH=$(cygpath -w "$UNIX_PATH")
        
        if [ -f "$WIN_PATH" ]; then
            echo "Copy: $(basename "$WIN_PATH")"
            cp "$WIN_PATH" "$TARGET_DIR/"
        else
            echo "Warning: File not found: $WIN_PATH"
        fi
    fi
done

echo "------------------------------------------------"
echo "Operation Completed"