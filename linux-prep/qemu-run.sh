#!/bin/sh
# Runs the A1 smoke test on the phone: a real aarch64 Linux guest inside QEMU,
# with the CPU emulated in software.
#
# Why emulation at all. The firmware route needs a storage medium Windows can be
# read from, and the firmware mounts FAT32 only while every volume on this phone
# is ext4 or f2fs; changing that means rewriting the GPT, which is the one edit
# that can strand the device in EDL. Running Windows as a guest costs speed and
# touches no partition table: userdata is already mounted rw and not noexec, so
# an executable can be dropped straight onto /data and a disk image lives there
# as an ordinary file.
#
# Nothing KVM-related is assumed. CONFIG_VIRTUALIZATION is not set in any of the
# three courbet kernels on this device, so /dev/kvm does not exist and the
# thread is TCG only. If A2 later proves the bootloader hands control to EL2, the
# same guest runs faster with a rebuilt kernel; that is a separate question.
#
# Run it from a root shell in recovery:
#
#     adb push dist/qemu/courbet-qemu.tar.gz /tmp/
#     adb shell
#     cd /data && mkdir -p qemu && tar xzf /tmp/courbet-qemu.tar.gz -C /data/qemu
#     sh /data/qemu/run.sh
#
# Override with the environment: MEM_MB, SMP, IMAGE, INITRD.

set -eu

D=$(dirname "$0")
B="$D/bin/busybox"
[ -x "$B" ] || B=busybox

IMAGE="${IMAGE:-$D/guest/vmlinuz}"
INITRD="${INITRD:-$D/guest/initrd-smoke.cpio.gz}"
MEM_MB="${MEM_MB:-2048}"
SMP="${SMP:-2}"
LOG="${LOG:-$D/smoke.log}"

Q="$D/bin/qemu-system-aarch64"

echo "bundle  : $D"
echo "qemu    : $Q"
echo "guest   : $IMAGE + $INITRD"
echo "budget  : ${MEM_MB} MB, ${SMP} vcpu"

if [ ! -x "$Q" ]; then
    echo "FAIL: $Q is missing or not executable"
    exit 1
fi

# Executing from /data is the assumption the whole branch rests on, so check it
# before anything heavier. If the volume were mounted noexec this fails here
# with a clear message instead of inside QEMU.
if ! "$Q" --version >/dev/null 2>&1; then
    echo "FAIL: qemu cannot be executed from $(dirname "$Q")"
    echo "      check the mount options: mount | grep ' on /data '"
    exit 1
fi
echo "version : $("$Q" --version | head -1)"

FREE=$($B awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo 2>/dev/null || echo "")
[ -n "$FREE" ] && echo "ram free: ${FREE} MB"
if [ -n "$FREE" ] && [ "$FREE" -lt "$(( MEM_MB + 512 ))" ]; then
    echo "FAIL: ${MEM_MB} MB guest needs headroom for the kernel and TCG's own"
    echo "      translation tables. Lower MEM_MB or close something."
    exit 1
fi

CAP=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null || echo unknown)
echo "battery : ${CAP}%"

# Eight emulated threads on eight physical cores, at full tilt, is the fastest
# way to reach the thermal governor, which throttles and then blames TCG for
# being slow. Two is enough to measure and keeps the board quiet.
echo "starting (Ctrl-C stops it)"
echo ""

"$Q" \
    -machine virt \
    -cpu max \
    -accel tcg,thread=multi \
    -smp "$SMP" \
    -m "$MEM_MB" \
    -kernel "$IMAGE" \
    -initrd "$INITRD" \
    -append "console=ttyAMA0 rdinit=/init" \
    -nographic \
    -no-reboot 2>&1 | tee "$LOG"

echo ""
echo "log kept at $LOG"
if $B grep -q QEMU-SMOKE-OK "$LOG" 2>/dev/null || grep -q QEMU-SMOKE-OK "$LOG"; then
    echo "PASS: the guest booted and ran under TCG on this phone."
else
    echo "INCONCLUSIVE: QEMU exited without the guest marker. Read $LOG."
fi
