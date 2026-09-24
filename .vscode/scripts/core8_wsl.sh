#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir=$1
action=$2
mount_dir=/mnt/fome-core8-build

mkdir -p "$mount_dir"
if mountpoint -q "$mount_dir"; then
	echo "${mount_dir} e gia montato: smontalo prima di avviare il task." >&2
	exit 1
fi

mount --bind "$repo_dir" "$mount_dir"
tool_dir=$(mktemp -d /tmp/fome-arm-tools.XXXXXX)
cleanup() {
	umount "$mount_dir"
	rm -f "$tool_dir/objcopy" "$tool_dir/objdump"
	rmdir "$tool_dir"
}
trap cleanup EXIT

arm_bin="$mount_dir/firmware/ext/build-tools/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-eabi/bin"
ln -s "$arm_bin/arm-none-eabi-objcopy" "$tool_dir/objcopy"
ln -s "$arm_bin/arm-none-eabi-objdump" "$tool_dir/objdump"
chmod 755 "$tool_dir"

echo "Filesystem build: $(findmnt -T "$mount_dir" -no FSTYPE)"
runuser -u deffie -- env PATH="$tool_dir:$PATH" \
	bash "$mount_dir/.vscode/scripts/core8_dfu.sh" "$action"
