#!/bin/bash
# Test 2: Many hard links creation/removal

mkdir -p /tmp/ramfs/link-stress
echo "Initial test content" > /tmp/ramfs/link-stress/original_file
LINKCOUNT=1000

for i in $(seq 1 $LINKCOUNT); do
    ln /tmp/ramfs/link-stress/original_file /tmp/ramfs/link-stress/link_$i
done

echo "Created $LINKCOUNT hard links."

ls -l /tmp/ramfs/link-stress/original_file

for i in $(seq 1 $LINKCOUNT); do
    rm /tmp/ramfs/link-stress/link_$i
done

echo "Removed $LINKCOUNT hard links."

ls -l /tmp/ramfs/link-stress/original_file
rm /tmp/ramfs/link-stress/original_file

