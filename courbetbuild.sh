#!/bin/bash
# Courbet UEFI build. Release is the safe/default image for phone boot.
set -e
. build_common.sh
PYTHON_COMMAND=python2.7
TARGET="${TARGET:-RELEASE}"
case "$TARGET" in
  DEBUG|RELEASE) ;;
  *) echo "TARGET must be DEBUG or RELEASE (got: $TARGET)" >&2; exit 2 ;;
esac
BUILD_DIR="Build/F11/${TARGET}_GCC5"
rm -rf "$BUILD_DIR/FV/Ffs/7E374E25-8E01-4FEE-87F2-390C23C606CDFVMAIN"
echo "[BUILD] F11 sweet TARGET=$TARGET"
echo "[DSDT] recompiling DSDT.dsl -> DSDT.aml ..."
( cd F11/AcpiTables && iasl DSDT.dsl )
GCC5_AARCH64_PREFIX=aarch64-linux-gnu- build -b "$TARGET" -s -n 0 -a AARCH64 -t GCC5 -p F11/sweet.dsc
gzip -c < "$BUILD_DIR/FV/F11_UEFI.fd" > uefi_image5
cat sweet.dtb >> uefi_image5
abootimg --create courbet_uefi.img -k uefi_image5 -r ramdisk-null -f bootimg.cfg
echo "[VERIFY] FVs with GDGT9897 (courbet touch, SPI SE4):"
grep -al GDGT9897 "$BUILD_DIR/FV"/*.Fv 2>/dev/null || echo "  none"
echo "[VERIFY] FVs with QCOM1411 (IC10):"
grep -al QCOM1411 "$BUILD_DIR/FV"/*.Fv 2>/dev/null || echo "  none"
echo "[DONE] courbet_uefi.img ($TARGET)"
rm -f uefi_image5
rm -rf Build/