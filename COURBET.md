# courbet notes

This fork builds the sweet (Redmi Note 10 Pro) Windows-on-ARM UEFI and packages
it for **courbet**, the Xiaomi Mi 11 Lite 4G.

## Why that should work at all

Both boards are the same SoC:

| | sweet | courbet |
|---|---|---|
| device | Redmi Note 10 Pro | Mi 11 Lite 4G |
| SoC | SM7150-AC, Snapdragon 732G | SM7150-AC, Snapdragon 732G |
| kernel name | `qcom,sdmmagpie` | `qcom,sdmmagpie` |

Same silicon, so the firmware carries over; what differs between the boards is
the device tree. That is the same reasoning that made the surya UEFI boot on
courbet.

There is no SM7325. An earlier revision of this table said SM7325, which does
not exist as a part. The evidence is in courbet's own tree: `qcom,msm-id =
<0x16d>` (365 = SM7150) and `qcom,pmic-name = "PM6150"`. Run

    python tools/fdt.py work/sm7150-sweet/courbet.dtb --node /

to see it rather than take it on trust.

This is also the important part, and it is not true of the surya firmware this
repository's parent project used: **the sweet firmware drives internal UFS
storage.** That was the blocker for reaching Windows at all, because without a
storage driver the firmware cannot read any installation medium.

## What was changed here

Upstream, unchanged, builds one image:

```bash
./sweetbuild.sh          # -> sweet_uefi.img
```

Here, the same firmware is packaged three times with three device trees, so the
right one can be chosen by testing rather than by guessing. `fastboot boot`
writes nothing, so every variant is safe to try.

| variant | device tree | rationale |
|---|---|---|
| `courbet-with-sweet-dtb` | `sweet.dtb` | baseline, exactly what upstream builds |
| `courbet-with-courbet-dtb` | `courbet.dtb` | courbet's own Android tree, the one the working surya image used |
| `courbet-with-7150-dtb` | `7150.dtb` | the SoC-family tree in this repository |

```bash
fastboot boot courbet-with-courbet-dtb.img
```

## Tools

- `tools/swap_dtb.py` — replaces the tree appended to a boot image. It refuses
  to write unless it can first rebuild the input byte for byte, so a packaging
  error cannot quietly produce an unbootable image.
- `tools/pad_to_partition.py` — grows an image to 128 MiB for a future
  `fastboot flash boot`. Not needed for `fastboot boot`, which transfers to RAM.
- `tools/verify_boot_image.py` — checks the contract the loader enforces:
  Android boot header, page size 4096, an inflating gzip stream, the `ARM\x64`
  magic at offset 0x38, a flattened device tree after the stream, and
  `kernel_size == len(gzip) + tree totalsize`.

## Known limits of the sweet firmware

From upstream's own status report, on sweet:

- boots to a usable Windows 11 ARM64 desktop
- internal storage (UFS), display, ACPI and buttons work
- **no GPU acceleration** — Windows renders through the framebuffer
- touchscreen disabled; the I2C path deadlocks a core
- no Wi-Fi, no audio, no modem

Those are expected to carry over. The ACPI is the one that had to be corrected,
and it now is — see the next section.

## The DSDT is courbet's now

Upstream's `F11/AcpiTables/DSDT.dsl` describes sweet's hardware. Everything in
it is SoC-level (UFS, USB, PMIC, PCIe, CPUs, PEP, GPIO) and therefore correct
for courbet too — except the one device that is wired differently: the
touchscreen.

Where the facts come from: Xiaomi's own kernel source for this board, branch
`courbet-r-oss` of `MiCode/Xiaomi_Kernel_OpenSource` (the same drop that
`courbet.dtb` in this repository was built from).

| | sweet | courbet |
|---|---|---|
| device-tree node | `&qupv3_se7_i2c > gt9896@5d` | `&qupv3_se4_spi > xiaomi-spits@0` |
| bus | I2C7, GENI SE7, 0xA84000, GSI 0x182 | SPI, GENI SE4, 0x890000, GSI 0x27D, CS0, 8 MHz |
| chip | Goodix GT9896 (`goodix,gt9896`) | Goodix GT9897S/T (`goodix_brl_spi`) **or** FocalTech FT3658U (`focaltech_spi`) |
| kernel driver enabled | GTX8 (I2C) | `CONFIG_TOUCHSCREEN_GOODIX_BRL=y` + `CONFIG_TOUCHSCREEN_FTS=y`, both SPI; `CONFIG_TOUCHSCREEN_GOODIX_GTX8` is **not** set |
| INT | TLMM GPIO9, falling edge | TLMM GPIO9, falling edge |
| RESET | TLMM GPIO8 | TLMM GPIO8 |
| I/O rail (iovdd) | GPIO90 | GPIO72 (`touch_vddio_vreg`) |
| analog rail (avdd) | — | GPIO21 (`touch_vddv_vreg`) |
| PMIC rail (pmavdd) | PM6150L LDO11_C (`vreg_l11c`) | PM6150L LDO11_C (`pm6150l_l11`) |
| power-on order | — | iovdd → 3 ms → avdd → pmavdd → 15 ms → release reset (`brl_power_on`), first SPI op 5 ms after |

Two conclusions worth writing down, because both were guesses before:

- **courbet has no touch on I2C7.** `courbet-sdmmagpie.dtsi` does carry a
  `goodix@5d` node on `qupv3_se7_i2c`, but no driver for it is enabled in
  `courbet_user_defconfig`, so no Android build of this board can use it. The
  node is shared boilerplate, not this phone's wiring. The live node is the SPI
  one, and the FocalTech driver contains code that parses the `k9a` panel name
  out of the kernel command line — courbet-specific code in a SPI driver.
- **The PMIC vote does not move.** The RPMh vote in `I2cClockDxe`
  (`RpmhVoteLdo11C`, resource `ldoc11`) was written for sweet, but courbet's
  touch PMIC rail is the same LDO11_C. Only the two GPIO-gated rails are new.

What changed in `DSDT.dsl`:

- `I2C5` (SE4 at 0x890000, which no board used) became **`SPI5`** with `_HID`
  `QCOM040F` — the QUP SPI controller the touch sits on. The PEP0 table's
  `\\_SB.I2C5` string moved with it.
- `TSC1` now hangs off `\_SB.SPI5` instead of `\_SB.IC10`: `SpiSerialBusV2`
  (CS0, 8 MHz, mode 0), `GpioInt` edge-low on GPIO9, `GpioIo` outputs for reset
  (GPIO8), iovdd (GPIO72) and avdd (GPIO21), plus the matching operation
  regions so `_PS0` can drive all three pins. The `_HID` is `GDGT9897` — a chip
  family the SPI drivers above actually bind; the sweet driver's `GDGT9889`
  stays out of the way on purpose.
- `_PS0` follows the vendor power order above.

`tools/fdt.py` can be pointed at any of the trees in this repository to
cross-check, and `sweetbuild.sh` now fails loudly if `GDGT9897` did not make it
into the firmware volumes.

### Checked against the phone itself

The table above is not just kernel-source archaeology. With the phone in
recovery (adb root), its running device tree can be read directly, and every
value in the DSDT was compared against it:

    adb shell od -An -tx1 /proc/device-tree/soc/spi@0x890000/interrupts        # 00 00 02 5d -> GSI 0x27D
    adb shell od -An -tx1 /proc/device-tree/soc/spi@0x890000/reg              # 00890000 4000
    adb shell od -An -tx1 /proc/device-tree/soc/spi@0x890000/xiaomi-spits@0/reg  # CS0
    adb shell od -An -c  /proc/device-tree/soc/spi@0x890000/xiaomi-spits@0/compatible  # "xiaomi,spits"
    adb shell od -An -tx1 /proc/device-tree/soc/spi@0x890000/xiaomi-spits@0/spi-max-frequency  # 0x007A1200 = 8 MHz
    adb shell od -An -tx1 /proc/device-tree/soc/spi@0x890000/xiaomi-spits@0/goodix,irq-gpio    # tlmm 9, 0x2800

and the same for `touch_vddio_vreg` (GPIO72), `touch_vddv_vreg` (GPIO21) and
the pmavdd phandle, which resolves to `rpmh-regulator-ldoc11` →
`regulator-pm6150l-l11` with `qcom,resource-name = "ldoc11"` — the exact string
`RpmhVoteLdo11C` searches for in cmd-db. The board also still reports
`qcom,msm-id = <0x16d>` and `qcom,msm-name = "SDMMAGPIE"`.


## A fixed upstream workflow

The workflow inherited here triggered on `push` to `master` while the branch is
`main`, so it never ran. It also did not check out submodules, which the build
needs. Both are corrected in `.github/workflows/build.yml`.