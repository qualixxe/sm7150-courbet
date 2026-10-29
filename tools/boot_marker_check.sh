#!/bin/sh
# Prints whether a QEMU console transcript carries the marker a guest was asked
# to echo, and shows enough of the transcript to diagnose the run either way.
#
# It exists because the same check is needed three times over - the distro
# firmware as a control, then our own build over virtio and over USB - and
# because a bare grep tells nothing about a firmware that stopped at a different
# line every time.
#
# usage: boot_marker_check.sh LABEL MARKER LOG [tail lines]
#
# Exits 0 when the marker is present, 1 when it is not. Callers decide whether
# that stops the workflow.

set -eu

label=${1:?label}
marker=${2:?marker}
log=${3:?log}
n=${4:-40}

if [ ! -s "$log" ]; then
    echo "$label: FAIL - the transcript is empty, QEMU produced no console output"
    exit 1
fi

echo "$label: transcript is $(wc -l < "$log") lines, $(wc -c < "$log") bytes"

if grep -q "$marker" "$log"; then
    echo "$label: PASS ($marker found)"
    status=0
else
    echo "$label: FAIL ($marker not found)"
    status=1
fi

echo "----- $label: last $n lines -----"
tail -n "$n" "$log"
echo "----- $label: end -----"

exit "$status"
