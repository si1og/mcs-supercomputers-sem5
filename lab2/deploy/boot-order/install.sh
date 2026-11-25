#!/bin/bash
set -euo pipefail
test "$(id -u)" -eq 0
task_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$(hostname -s)" in
    server01) task_uuid=b4e477f0-960e-4456-928c-2415f14398f9 ;;
    server02) task_uuid=ec357133-66eb-4684-8a90-753498765073 ;;
    server03) task_uuid=4103a624-11a8-479f-891b-da09ad0b9abf ;;
    *) echo 'Unknown Gluster server hostname' >&2; exit 1 ;;
esac
test "$(nmcli -g GENERAL.CON-UUID device show enp0s9)" = "$task_uuid"
for task_pair in '60-boot-order.conf:/etc/systemd/system/glusterd.service.d/60-boot-order.conf' 'gluster-wait-network:/usr/local/libexec/gluster-wait-network'; do
    task_name=${task_pair%%:*}
    task_target=${task_pair#*:}
    if [ -e "$task_target" ] && ! cmp -s "$task_source_dir/$task_name" "$task_target"; then
        echo "Existing different configuration: $task_target" >&2
        exit 1
    fi
done

nmcli connection modify uuid "$task_uuid" connection.autoconnect yes ipv4.may-fail no
if [ "$(hostname -s)" = server03 ]; then
    # Stale client profile competes for the same enp0s9 at boot; preserve it.
    nmcli connection modify uuid 8e657444-284b-45d9-88f6-176cbab5a709 connection.autoconnect no
fi
install -d -m 0755 /usr/local/libexec /etc/systemd/system/glusterd.service.d
install -m 0755 "$task_source_dir/gluster-wait-network" /usr/local/libexec/gluster-wait-network
install -m 0644 "$task_source_dir/60-boot-order.conf" /etc/systemd/system/glusterd.service.d/60-boot-order.conf
if command -v restorecon >/dev/null; then
    restorecon /usr/local/libexec/gluster-wait-network
fi
systemctl enable NetworkManager-wait-online.service
systemctl daemon-reload
systemd-analyze verify glusterd.service
/usr/local/libexec/gluster-wait-network
systemctl restart glusterd
systemctl is-active network-online.target NetworkManager-wait-online.service bricks-rep.mount glusterd
echo GLUSTER_BOOT_ORDER_INSTALLED
