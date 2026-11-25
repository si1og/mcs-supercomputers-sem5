#!/bin/bash
set -euo pipefail
test "$(id -u)" -eq 0
task_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_target=/etc/sudoers.d/90-lab-sudo-timeout
id ilyasemenov >/dev/null
task_main=/etc/sudoers
if [ ! -e "$task_main" ] && [ -f /usr/etc/sudoers ]; then
    task_main=/usr/etc/sudoers
fi
/usr/sbin/visudo -cf "$task_main"
if ! grep -Eq '^[[:space:]]*([@#]includedir)[[:space:]]+"?/etc/sudoers.d/?"?[[:space:]]*($|#)' "$task_main"; then
    echo 'No standard /etc/sudoers.d include; refusing to change sudo configuration.' >&2
    exit 1
fi
if [ -e "$task_target" ] && ! cmp -s "$task_source_dir/90-lab-sudo-timeout" "$task_target"; then
    echo "Existing different configuration: $task_target" >&2
    exit 1
fi
/usr/sbin/visudo -cf "$task_source_dir/90-lab-sudo-timeout"
install -o root -g root -m 0440 "$task_source_dir/90-lab-sudo-timeout" "$task_target"
/usr/sbin/visudo -cf "$task_main"
hostname
/usr/bin/sudo -l -U ilyasemenov | head -n 14
echo SUDO_TIMEOUT_60_INSTALLED
