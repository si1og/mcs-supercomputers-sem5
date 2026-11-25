#!/bin/bash
set -euo pipefail
test "$(id -u)" -eq 0
test "$(hostname -s)" = client
task_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_entry=$(<"$task_source_dir/repvol.fstab")
task_existing=$(awk '$1 !~ /^#/ && ($2 == "/mnt/repvol" || $1 == "server01:/repvol") {print}' /etc/fstab)
if [ -n "$task_existing" ] && [ "$task_existing" != "$task_entry" ]; then
    echo 'Existing different Gluster fstab entry; refusing to replace it.' >&2
    exit 1
fi
task_uuid=41db1fb6-476e-47ea-8b14-2bbde55458c9
test "$(nmcli -g GENERAL.CON-UUID device show enp0s9)" = "$task_uuid"
nmcli connection modify uuid "$task_uuid" ipv4.may-fail no
systemctl enable NetworkManager-wait-online.service
mkdir -p /mnt/repvol
if [ -z "$task_existing" ]; then
    cp -an /etc/fstab /etc/fstab.before-gluster-client-20261123
    printf '\n%s\n' "$task_entry" >> /etc/fstab
fi
systemctl daemon-reload
findmnt --verify --tab-file /etc/fstab
systemd-analyze verify /run/systemd/generator/mnt-repvol.mount

# Exercise exactly the new mount options without disrupting the existing mount.
task_check_dir=$(mktemp -d /run/gluster-mount-check.XXXXXXXX)
task_options=$(awk '{print $4}' "$task_source_dir/repvol.fstab")
task_mounted=0
task_cleanup() {
    if [ "$task_mounted" -eq 1 ]; then
        umount "$task_check_dir" || return
    fi
    rmdir "$task_check_dir"
}
trap task_cleanup EXIT
timeout 100 mount -t glusterfs -o "$task_options" server01:/repvol "$task_check_dir"
task_mounted=1
findmnt --mountpoint "$task_check_dir"
timeout 15 stat -f "$task_check_dir"
umount "$task_check_dir"
task_mounted=0
systemctl start mnt-repvol.mount
findmnt --mountpoint /mnt/repvol
systemctl show mnt-repvol.mount -p FragmentPath -p ActiveState -p Wants -p After
echo GLUSTER_CLIENT_AUTOMOUNT_CONFIGURED
