mkdir -p /mnt/ramfs
mkdir -p /tmp/ramfs_backend
mount -t ramfs -o persist_dir=/tmp/ramfs_backend none /mnt/ramfs