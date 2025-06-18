#!/bin/bash
# Test 3: Deep nested directory creation

BASE_DIR="/tmp/ramfs/deep_test"
DEPTH=1000

mkdir -p $BASE_DIR
current_path=$BASE_DIR

for i in $(seq 1 $DEPTH); do
    current_path="$current_path/dir_$i"
    mkdir $current_path
    echo "Test at depth $i" > "$current_path/file_$i.txt"
done

echo "Deep nested directories of depth $DEPTH created."

# Verify
find $BASE_DIR -type f | wc -l

