#!/bin/sh
# Runs the A1b test: UEFI firmware (AAVF) plus one FAT disk, both as ordinary
# files on userdata, with the CPU emulated in software.
#
# What this decides. Windows is read from a FAT volume with
# \EFI\BOOT\BOOTAA64.EFI on it, and the only question that matters is whether a
# firmware can find a FAT filesystem on a medium the phone can actually provide.
# QEMU can present a file as either a PCI virtio disk or a USB mass-storage
# device, so a FAT image on /data becomes both. The marker the guest prints is
# the content of a file on that volume, not an echo, so it can only appear if the
# firmware mounted the ESP and read from it.
#
# BUS chooses the attachment, and both are worth having: Windows carries an inbox
# USB mass-storage driver and no inbox virtio driver, so BUS=usb is what an
# installer could boot unaided, while BUS=virtio is the fast path once drivers
# exist.
#
# This script is also what CI runs, with QEMU pointed at the runner's emulator.
# Testing the same file on both machines is the point: a command line that works
# only in one of them proves nothing about the other.
#
# Run from a root shell in recovery, after the qemu bundle and this bundle are
# both unpacked:
#
#     adb push dist/aavf/courbet-aavf.tar.gz /tmp/
#     adb shell
#     mkdir -p /data/aavf && tar xzf /tmp/courbet-aavf.tar.gz -C /data/aavf
#     sh /data/aavf/run.sh
#
# Override with the environment: QEMU, BUS, MEM_MB, SMP, CODE, VARS, DISK, LOG,
# TIMEOUT.

set -eu

D=$(dirname "$0")

Q="${QEMU:-/data/qemu/bin/qemu-system-aarch64}"
# QEMU is a path on the phone and a bare program name in CI. Anything without a
# slash is therefore resolved from PATH before the existence check below, so one
# variable can mean either.
case "$Q" in
    */*) ;;
    *) Q=$(command -v "$Q" 2>/dev/null || true) ;;
esac
[ -n "$Q" ] || { echo "FAIL: no emulator for QEMU=${QEMU:-default}"; exit 1; }
CODE="${CODE:-$D/efi/AAVF_CODE.fd}"
VARS="${VARS:-$D/efi/AAVF_VARS.fd}"
DISK="${DISK:-$D/disks/fat-boot.img}"
BUS="${BUS:-virtio}"
MEM_MB="${MEM_MB:-2048}"
SMP="${SMP:-2}"
LOG="${LOG:-$D/uefi.log}"
TIMEOUT="${TIMEOUT:-900}"
MARKER="${MARKER:-A1B-FS-OK}"

for f in "$Q" "$CODE" "$VARS" "$DISK"; do
    [ -f "$f" ] || { echo "FAIL: missing $f"; exit 1; }
done
[ -x "$Q" ] || { echo "FAIL: $Q is not executable"; exit 1; }

# QEMU writes the variable store, so both flash files are runtime copies: the
# shipped ones stay untouched, and a stale BootOrder from an earlier attempt
# cannot make this run boot something else. They go next to the log, so a caller
# that redirects the log somewhere else does not litter this directory.
RUNDIR=$(dirname "$LOG")
RC="$RUNDIR/CODE.runtime.fd"
RV="$RUNDIR/VARS.runtime.fd"
mkdir -p "$RUNDIR"
cp "$CODE" "$RC"
cp "$VARS" "$RV"

# A flash file has to be exactly the size the emulator's pflash device models,
# and that size is not portable: Ubuntu's qemu-system-arm 6.2 carries a patch
# that makes the arm "virt" flash banks 64 MB so that its own 64 MB AAVMF fits,
# while pristine upstream QEMU 8.2 - the one built for the phone - uses the 2 MB
# the FDF asks for. Padding is therefore done from the emulator's own error
# rather than from a number written into this file.
pad_to() {
    _need=$1
    for _f in "$RC" "$RV"; do
        if [ "$(wc -c < "$_f")" -lt "$_need" ]; then
            if command -v truncate >/dev/null 2>&1; then
                truncate -s "$_need" "$_f"
            else
                dd if=/dev/zero of="$_f" bs=1 seek="$_need" count=0 conv=notrunc \
                   >/dev/null 2>&1
            fi
            echo "padded $_f to $_need bytes for this emulator"
        fi
    done
}

case "$BUS" in
    virtio)
        # shellcheck disable=SC2086  # a deliberate word-split list of options
        ATTACH="-drive if=none,id=esp,format=raw,file=$DISK
                -device virtio-blk-pci,drive=esp"
        ;;
    usb)
        # The virt machine has no USB of its own, so the host controller is part
        # of what this tests. Which controller exists is a property of the
        # emulator binary, not of the machine: the runner's qemu-system-arm 6.2
        # has no nec-1527 at all and quits with "not a valid device model name",
        # while the 8.2 build on the phone does. So the first name this binary
        # actually registers is the one used, xhci first because it is the one
        # Windows and a modern EDK2 both handle best.
        CTRL=""
        for c in qemu-xhci nec-1527 piix3-usb-uhci; do
            if "$Q" -device help 2>/dev/null | grep -q "name \"$c\""; then
                CTRL=$c; break
            fi
        done
        if [ -z "$CTRL" ]; then
            echo "SKIP: this emulator registers no PCI USB host controller,"
            echo "      so the USB mass-storage path cannot be tested with it."
            echo "      Try: $Q -device help | grep -i usb"
            exit 3
        fi
        # shellcheck disable=SC2086
        ATTACH="-device $CTRL
                -drive if=none,id=usb,format=raw,file=$DISK
                -device usb-storage,drive=usb"
        echo "usb ctrl: $CTRL"
        ;;
    *)
        echo "FAIL: BUS must be virtio or usb, not $BUS"
        exit 1
        ;;
esac

echo "qemu    : $Q"
echo "firmware: $RC ($(wc -c < "$RC") bytes) + $RV ($(wc -c < "$RV") bytes)"
echo "disk    : $DISK as $BUS"
echo "budget  : ${MEM_MB} MB, ${SMP} vcpu, ${TIMEOUT} s"
echo ""

# Multi-threaded TCG is a QEMU 8.0 thing. Asking the runner's 6.2 emulator for
# thread=multi is a startup error rather than a degradation, so the option comes
# from the version string of the emulator actually being used, not from an
# assumption about which one that is. Command substitution rather than a process
# substitution: CI runs this file with sh, and sh on the runner is dash.
QV=$("$Q" --version 2>/dev/null \
     | sed -n 's/^QEMU emulator version \([0-9][0-9]*\)\..*/\1/p' | head -1)
case "$QV" in
    ""|[0-6]|7) ACCEL=tcg ;;
    *)          ACCEL=tcg,thread=multi ;;
esac
echo "accel   : $ACCEL"

# A guest that ignores its timeout costs battery the phone does not have spare,
# so the run is bounded. timeout is in toybox but not in every recovery build, so
# its absence degrades to an unbounded run rather than to the test dying before
# QEMU starts.
if command -v timeout >/dev/null 2>&1; then
    WRAP="timeout $TIMEOUT"
else
    WRAP=""
    echo "note: no timeout binary; the run is unbounded, Ctrl-C to stop it"
fi

boot_once() {
    # shellcheck disable=SC2086  # WRAP and ATTACH are word-split on purpose
    $WRAP "$Q" -machine virt -cpu max -accel $ACCEL \
        -nic none -smp "$SMP" -m "$MEM_MB" \
        -drive if=pflash,format=raw,readonly=on,file="$RC" \
        -drive if=pflash,format=raw,file="$RV" \
        $ATTACH \
        -nographic -no-reboot > "$LOG" 2>&1
}

set +e
boot_once
rc=$?

need=$(sed -n 's/.*device requires \([0-9]*\) bytes.*/\1/p' "$LOG" | head -1)
if [ -n "${need:-}" ]; then
    echo "emulator refuses the flash size; retrying once after padding"
    pad_to "$need"
    boot_once
    rc=$?
fi
set -e

# The transcript is the answer, so it is printed whichever way the test went.
echo ""
echo "----- console: $(wc -l < "$LOG") lines, $(wc -c < "$LOG") bytes -----"
tail -n 60 "$LOG"
echo "----- end of console (emulator exit $rc) -----"
echo ""

if grep -q "$MARKER" "$LOG"; then
    echo "PASS: firmware mounted the FAT disk and read a file from it"
elif grep -q "Boot Failure\|not found\|No bootable device" "$LOG"; then
    echo "FAIL: the firmware ran but found nothing it could boot"
    exit 1
else
    echo "FAIL: no $MARKER marker in $LOG (exit $rc)"
    exit 1
fi
