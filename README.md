# edk2-courbet — Windows on ARM for the Xiaomi Mi 11 Lite 4G

UEFI (EDK2 / Project Mu) firmware and a Windows 11 ARM64 install path for the
**Xiaomi Mi 11 Lite 4G** — codename **courbet**, Qualcomm **SM7150-AC**
(SDMMAGPIE, Snapdragon 732G, PM6150).

Forked from [edk2-porting/edk2-sm7150](https://github.com/edk2-porting/edk2-sm7150)
through [Cristiano1103/sm7150-sweet](https://github.com/Cristiano1103/sm7150-sweet).
The parent project targets the Redmi Note 10 Pro (**sweet**); this branch
retargets the same silicon at courbet and does the Windows installation work on
the phone itself. What the two boards share, and where they diverge, is written
down in [`COURBET.md`](COURBET.md).

> **Brick risk.** Experimental, hobbyist firmware. `fastboot boot` writes
> nothing and is the only supported way to run it; `fastboot flash boot`
> overwrites the Android boot partition, so keep a backup of it (`dd` from
> recovery) before you flash. Nothing here needs EDL, and nothing here should
> ever be tried after the phone has fallen into EDL — courbet has no
> user-accessible EDL mode.

---

## Status

Honest, current state.

### Firmware
- Builds in CI (`.github/workflows/build.yml`) and publishes three boot images
  that differ only in the device tree they carry: `sweet.dtb`,
  `courbet.dtb`, `7150.dtb`. All three are `fastboot boot`-able and written
  into `dist/`.
- Internal **UFS** storage works, so the firmware can read an installation
  medium — that was the blocker upstream had to solve first.
- **ACPI/DSDT is courbet's**: the touch controller hangs off SPI (GENI SE4,
  `0x890000`, CS0, 8 MHz) as `GDGT9897`, with iovdd on GPIO72, avdd on GPIO21,
  reset on GPIO8, interrupt on GPIO9 and the PMIC rail on PM6150L LDO11_C.
  Every value was cross-checked against the phone's own running device tree.
- Display through the framebuffer, buttons, watchdog.

### Windows 11 ARM64
Installed the slow, safe way first: **Windows itself runs on the phone under
QEMU/TCG**, with the install ISO and an 80 GB disk image as ordinary files on
`/data`. No partitions are touched. WinPE boots, Setup passes the TPM/Secure
Boot/RAM gates with `LabConfig`, and the image is staged onto the disk image.
The phone-side scripts live in [`linux-prep/`](linux-prep/).

Booting Windows this way is a bring-up step, not the goal: it proves the
Windows stack (bootmgfw → winload → WinPE → Setup) on courbet hardware while
the native path (firmware → Windows on UFS) is still being worked on.

### Not working yet
- **Touchscreen.** The DSDT device is right; the QUP/SPI driver path is not.
  See [`docs/TOUCH-SAGA.md`](docs/TOUCH-SAGA.md) and task notes in
  `COURBET.md`.
- **GPU acceleration** — Windows renders in software through the framebuffer.
- **Wi-Fi, Bluetooth, modem, audio, camera, sensors.**
- **USB host mode / OTG power** — USB device mode works.

## Build

Requires a Linux environment (not Windows, not Android).

```bash
./courbetbuild.sh          # -> courbet_uefi.img
./package_variants.sh      # -> out/courbet-with-{sweet,courbet,7150}-dtb.img
```

`courbetbuild.sh` recompiles `F11/AcpiTables/DSDT.dsl` first and fails loudly if
`GDGT9897` (the courbet touch) did not make it into the firmware volumes.

## Run

```bash
fastboot boot out/courbet-with-courbet-dtb.img
```

`fastboot boot` transfers the image to RAM and writes nothing, so a wrong
package cannot brick the phone — it just does not boot. Try the three trees and
keep the one that works.

The Windows install path is described in [`linux-prep/`](linux-prep/): QEMU on
the phone in recovery, the ISO and disk image as files under `/data`, QMP for
input and screenshots.

## Documentation

- [`COURBET.md`](COURBET.md) — what differs between sweet and courbet, the DSDT
  changes, and how they were verified against the live device tree.
- [`FINDINGS.md`](FINDINGS.md) — reverse-engineering notes.
- [`STORAGE.md`](STORAGE.md) — storage bring-up.
- [`docs/TOUCH-SAGA.md`](docs/TOUCH-SAGA.md) — the touch investigation.

## Acknowledgements

- [edk2-porting / edk2-sm7150](https://github.com/edk2-porting/edk2-sm7150) (Renegade Project) — the SM7150 UEFI base.
- [Cristiano1103/sm7150-sweet](https://github.com/Cristiano1103/sm7150-sweet) — the sweet port this forks from, and its driver pack.
- [Project Aloha](https://github.com/Project-Aloha) and [Project Silicium](https://github.com/Project-Silicium) — the Windows-on-Snapdragon ecosystem.
- Gustave Monce and [mu_andromeda_platforms](https://github.com/WOA-Project/mu_andromeda_platforms) / the [WOA-Project](https://github.com/WOA-Project).
- [Project Mu](https://microsoft.github.io/mu/).
- Everyone in the WoA / dopaemon community who documented the SM7150.

## License

The UEFI code inherits the licenses of its upstream EDK2 / Project Mu sources.
Driver binaries are the property of their respective owners and are
redistributed for interoperability and research purposes only. See
[LICENSE](LICENSE) if present.
