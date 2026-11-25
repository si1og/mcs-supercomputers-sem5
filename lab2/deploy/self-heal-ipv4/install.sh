#!/bin/bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo 'Run this installer with sudo.' >&2
    exit 1
fi
task_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -x /usr/lib/systemd/systemd-socket-proxyd
findmnt --mountpoint /bricks/rep >/dev/null

# Do not silently replace configurations installed by somebody else.
for task_unit in gluster-loopback.socket gluster-loopback.service; do
    task_target="/etc/systemd/system/$task_unit"
    if [ -e "$task_target" ] && ! cmp -s "$task_source_dir/$task_unit" "$task_target"; then
        echo "Existing different configuration: $task_target" >&2
        exit 1
    fi
done
task_dropin=/etc/systemd/system/glusterd.service.d/50-loopback.conf
if [ -e "$task_dropin" ] && ! cmp -s "$task_source_dir/50-loopback.conf" "$task_dropin"; then
    echo "Existing different configuration: $task_dropin" >&2
    exit 1
fi

install -m 0644 "$task_source_dir/gluster-loopback.socket" /etc/systemd/system/gluster-loopback.socket
install -m 0644 "$task_source_dir/gluster-loopback.service" /etc/systemd/system/gluster-loopback.service
install -d -m 0755 /etc/systemd/system/glusterd.service.d
install -m 0644 "$task_source_dir/50-loopback.conf" "$task_dropin"
systemd-analyze verify /etc/systemd/system/gluster-loopback.socket /etc/systemd/system/gluster-loopback.service

if command -v getenforce >/dev/null && [ "$(getenforce)" != Disabled ]; then
    task_policy_dir=$(mktemp -d /run/gluster-loopback-policy.XXXXXXXX)
    checkmodule -M -m -o "$task_policy_dir/gluster_loopback.mod" "$task_source_dir/gluster_loopback.te"
    semodule_package -o "$task_policy_dir/gluster_loopback.pp" -m "$task_policy_dir/gluster_loopback.mod"
    /usr/sbin/semodule -i "$task_policy_dir/gluster_loopback.pp"
fi

systemctl daemon-reload
systemctl reset-failed gluster-loopback.socket gluster-loopback.service || true
systemctl enable --now gluster-loopback.socket
systemctl try-restart gluster-loopback.service
systemctl restart glusterd
systemctl is-enabled gluster-loopback.socket
systemctl is-active gluster-loopback.socket glusterd
hostname
ss -ltnp | grep 24007
echo 'GLUSTER_LOOPBACK_INSTALLED'
