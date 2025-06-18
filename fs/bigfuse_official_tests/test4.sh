#!/bin/bash
# Test 4: Large number of small files

SMALL_FILES_DIR="/tmp/ramfs/small_files_test"
FILE_COUNT=10000

mkdir -p $SMALL_FILES_DIR

for i in $(seq 1 $FILE_COUNT); do
    echo "small data $i" > "$SMALL_FILES_DIR/small$i.txt"
done

echo "Created $FILE_COUNT small files."

ls $SMALL_FILES_DIR | wc -l

for i in $(seq 1 $FILE_COUNT); do
    rm "$SMALL_FILES_DIR/small$i.txt"
done

echo "Removed $FILE_COUNT small files."

ls $SMALL_FILES_DIR | wc -l

