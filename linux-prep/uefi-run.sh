#!/bin/sh
# Runs the A1b test on the phone: UEFI firmware (AAVF) plus one FAT disk, both
# as ordinary files on userdata, with the CPU emulated in software.
#
# What this decides. The installer for Windows is a FAT volume with
# \EFI\BOOT\BOOTAA64.EFI on it, and the only question that matters is whether a
# firmware can find a FAT filesystem on a medium the phone can actually provide.
# QEMU can present a file as either a PCI virtio disk or a USB mass-storage
# device, so a FAT image on /data becomes both. If the marker below appears, the
# read path works end to end and the next step is a real installer; if it does
# not, no amount of Windows imaging helps.
#
# BUS=chooses which of the two the disk is attached as. Both are worth having:
# Windows carries an inbox USB mass-storage driver but no inbox virtio driver, so
# BUS=usb is the configuration the installer can boot by itself, while BUS=virtio
# is the one that reaches full speed once drivers are added.
#
# Run from a root shell in recovery, after the qemu bundle and this bundle are
# both unpacked:
#
#     adb push dist/aavf/courbet-aavf.tar.gz /tmp/
#     adb shell
#     mkdir -p /data/aavf && tar xzf /tmp/courbet-aavf.tar.gz -C /data/aavf
#     sh /data/aavf/run.sh
#
# Override with the environment: QEMU, BUS, MEM_MB, SMP, CODE, VARS, DISK, LOG.

set -eu

D=$(dirname "$0")

Q="${QEMU:-/data/qemu/bin/qemu-system-aarch64}"
CODE="${CODE:-$D/efi/AAVF_CODE.fd}"
VARS="${VARS:-$D/efi/AAVF_VARS.fd}"
DISK="${DISK:-$D/disks/fat-boot.img}"
BUS="${BUS:-virtio}"
MEM_MB="${MEM_MB:-2048}"
SMP="${SMP:-2}"
LOG="${LOG:-$D/uefi.log}"
TIMEOUT="${TIMEOUT:-900}"

for f in "$Q" "$CODE" "$VARS" "$DISK"; do
    [ -f "$f" ] || { echo "FAIL: missing $f"; exit 1; }
done
[ -x "$Q" ] || { echo "FAIL: $Q is not executable"; exit 1; }

# QEMU writes the variable store, so it gets a throwaway copy per run. A stale
# BootOrder from an earlier attempt would boot something else and the test would
# report a failure that belongs to the firmware's memory, not to the disk.
RUNVARS="$D/VARS.runtime.fd"
cp "$VARS" "$RUNVARS"

echo "qemu    : $Q"
echo "firmware: $CODE ($(wc -c < "$CODE") bytes)"
echo "vars    : $WORK/VARS.fd"
echo "disk    : $DISK as $BUS"
echo "budget  : ${MEM_MB} MB, ${SMP} vcpu, ${TIMEOUT} s"
echo ""

case "$BUS" in
    virtio)
        ATTACH="-drive if=none,id=esp,format=raw,file=$DISK
                -device virtio-blk-pci,drive=esp"
        ;;
    usb)
        # nec-1527 is a PCI EHCI host controller. Nothing on the virt machine
        # provides USB by itself, so the controller has to be plugged in.
        ATTACH="-device nec-1527
                -drive if=none,id=usb,format=raw,file=$DISK
                -device usb-storage,drive=usb"
        ;;
    *)
        echo "FAIL: BUS must be virtio or usb, not $BUS"
        exit 1
        ;;
esac

set +e
# shellcheck disable=SC2086  # ATTACH is a deliberate word-split list of options.
#
# A guest that ignores its timeout costs battery the phone does not have spare,
# so the run is bounded. timeout is in toybox but not in every recovery build, so
# its absence degrades to an unbounded run rather than to "command not found"
# killing the test before QEMU starts.
if command -v timeout >/dev/null 2>&1; then
    WRAP="timeout $TIMEOUT"
else
    WRAP=""
    echo "note: no timeout binary; the run is unbounded, Ctrl-C to stop it"
fi

$WRAP "$Q" -machine virt -cpu max -accel tcg,thread=multi \
    -nic none -smp "$SMP" -m "$MEM_MB" \
    -drive if=pflash,format=raw,readonly=on,file="$CODE" \
    -drive if=pflash,format=raw,file="$RUNVARS" \
    $ATTACH \
    -nographic -no-reboot 2>&1 | tee "$LOG"
rc=$?
set -e

echo ""
if grep -q "A1B-FS-OK" "$LOG"; then
    echo "PASS: firmware mounted the FAT disk and read a file from it"
elif grep -qi "failed to find romfile\|unsupported configuration\|Could not open" "$LOG"; then
    echo "FAIL: QEMU refused the configuration, the firmware never ran"
else
    echo "FAIL: no A1B-FS-OK marker in $LOG (exit $rc)"
    echo "      last lines:"
    tail -25 "$LOG"
fi
