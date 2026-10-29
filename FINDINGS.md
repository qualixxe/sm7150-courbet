
## What the working firmware actually is

The device tree carried by the surya firmware that boots on courbet, and by the
sweet firmware that also boots, both identify as:

```
model      = Qualcomm Technologies, Inc. SDMMAGPIE SoC
compatible = qcom,sdmmagpie
qcom,msm-id   = <0x16d>          0x16d = 365 = SM7150
qcom,msm-name = "SDMMAGPIE"
qcom,pmic-name = "PM6150"
```

**Correction, made against the tree rather than from memory:** an earlier
version of this file claimed `SDMMAGPIE` is SM8150 / Snapdragon 855, and a
sibling document called courbet's SoC `SM7325`. Both are wrong, and two
independent properties in the tree say so:

- `qcom,msm-id = 0x16d` is 365, which is SM7150. SM8150 is `0x15b`.
- `qcom,pmic-name = "PM6150"` is the PMIC of the SM6150 family (Snapdragon
  730/732/665). SM8150 pairs with PM8150.

There is no such part as SM7325. The SoC is SM7150-AC, Snapdragon 732G, and
`SDMMAGPIE` is its kernel-side name.

That settles the question the file was written to answer. courbet and surya are
**the same SoC**, so a firmware that runs on one is expected to run on the
other. The surya firmware booting courbet is not luck, and the sweet firmware
booting courbet is not luck either. `sweet.dtb` carries `qcom,sdmmagpie-qrd`,
a Qualcomm reference design, which is the likely reason the buttons and display
come up without a device-specific description.

Consequences worth remembering:

- Touch is a Windows-side question, and it needs a driver plus a real courbet
  tree. The DSDT here describes sweet's Goodix GT9896 on I2C 0x5D.
- `7150.dtb` is not related to SM7150 either. It identifies as
  `qcom,apq8016-sbc`.

Verify all of this yourself rather than trusting the paragraph above:

```
python tools/fdt.py work/sm7150-sweet/courbet.dtb --node /
```

## Touchscreen in the firmware

Two different firmwares, two different answers, and they were being conflated.

The **sweet** image has no touch driver. A working GUI on sweet means display
and input buttons are fine, and touch is left to Windows.

The **surya-derived** image does register an absolute pointer. Its logger shows

```
guidrv: try init input driver uefi-touch
uefitouch: found new uefi absolute 0x...
uefitouch: receive touch device hot-plug event
```

and no events ever arrive afterwards. That is the same shape as the failure
documented in `docs/TOUCH-SAGA.md` on sweet: the device object exists, the
controller is never brought up, the interrupt line stays asserted and nothing
is ever reported. It is a wiring problem, not a missing driver.
## Emulation reaches the point where a measurement was worth taking

The firmware route is blocked on a medium, not on a driver. The shipping
firmware carries one filesystem driver, `Fat`, and every volume on this phone is
ext4 or f2fs, so Windows has nothing it can read. Making a FAT volume means
writing a partition table, and a partition table written wrongly on courbet
means EDL 9008 with an unlock the vendor gates behind an authorized Mi account.
That is not a risk to take for a convenience.

The alternative is a second firmware, one that reads a file: QEMU's `virt`
machine takes an image on userdata as a block device, and its own EDK2 build
mounts FAT from it. userdata is mounted `rw` without `noexec`, so an executable
can be dropped straight onto `/data`. Software emulation was measured rather
assumed, and it turned out fast enough to be worth the detour:

```
guest   : Alpine 6.6.134-0-virt, 2 vcpu, 1911 MB
host    : courbet, Kryo 475 under QEMU 8.2.2 TCG, thread=multi
bench   : sha256 of 64 MB through a pipe, no I/O
result  : 720 cs, 9102 KB/s
```

The same benchmark inside the same QEMU on a GitHub runner gives 21072 KB/s, so
the phone is about 2.3x slower than a cloud core at translating arm64 to arm64.
That is the number branch A was built to obtain, and it is the number that says
whether a Windows guest is an experiment or an afternoon of waiting.

KVM is not an option on this kernel and the measurement above says nothing about
it: `CONFIG_VIRTUALIZATION` is unset in all three courbet kernels on the device,
`/dev/kvm` does not exist, and the guest's own log closes the question with
`kvm [1]: HYP mode not available`.

What is still unproven, and is the next thing the `aavf-efi` workflow answers:
whether a firmware built from this tree mounts a FAT ESP that lives inside a
file, and whether it then loads `\EFI\BOOT\BOOTAA64.EFI` off it. Windows needs
exactly those two facts, in that order.

## Both questions the last section left open are answered, and the answer is yes

Run 5 of the `aavf-efi` workflow booted the same FAT disk under three
firmwares. What it printed:

| firmware            | FAT read | EFI handoff |
| ---                 | ---      | ---         |
| distro AAVF         | yes      | yes         |
| ours, virtio        | yes      | yes         |
| ours, usb-storage   | n/a      | n/a         |

"FAT read" is the content of `hello.txt`, printed by the UEFI shell after it was
already running: it can only appear if the firmware found the GPT partition,
mounted the FAT32 volume inside an ordinary file and read a directory entry.
"EFI handoff" is `QEMU-SMOKE-OK`, printed by `/init` inside a Linux that the
firmware loaded as `\EFI\BOOT\BOOTAA64.EFI` and started with the shell's own
command line. The two are separate columns because they are separate facts, and
the second one is the fact Windows depends on: setup is an EFI binary on a FAT
volume, started by a firmware.

So a Windows disk image stored as a file on userdata is readable by a firmware
built from this tree, and bootable from, on this hardware. The medium stays a
file on `/data`, which is the part that does not touch the GPT and therefore
cannot reach EDL.

The USB row is n/a rather than no: the runner's `qemu-system-arm` is 6.2 and its
binary registers no `nec-1527`, so it exits with "not a valid device model name"
before the firmware runs. That says something about the package, not about
EDK2's `UsbMassStorageDxe`, and the phone's own QEMU is 8.2, built here, where
the controller does exist. The script now asks the emulator which PCI USB host
controller it has instead of assuming one, and reports n/a when it has none.

Two details that were only knowable by running it:

- The FAT volume needs room. A 64 MB image holds the shell, a 34 MB zboot
  kernel, an initrd and the scripts at about 90 % full; the image is 128 MB now
  so that a filesystem failure cannot be mistaken for a firmware failure.
- `mcopy` without `-o` does not overwrite an existing file, it asks. With
  nothing attached to stdin the question has no answer, and mtools exits 1 in
  1.7 ms having written nothing to either stream - which is how run 3 and run 4
  failed with no error message at all.

## The same test on the phone, which is the run that counts

The bundle CI produced was unpacked to `/data/aavf` on courbet and run twice
with the phone's own emulator, `/data/qemu/bin/qemu-system-aarch64`, a static
8.2.2 built here:

| on courbet, QEMU 8.2.2 TCG | FAT read | EFI handoff |
| ---                     | ---      | ---         |
| BUS=virtio              | yes      | yes         |
| BUS=usb                 | yes      | yes         |

Both runs ended with `RESULT: QEMU-SMOKE-OK`, the same guest hash
`3b6a07d0d404...`, and `reboot: Power down` with the emulator exiting 0. The
firmware was the one this workflow builds, not a distro file.

The USB path needed no change to the drivers: `uefi-run.sh` selects the first
PCI USB host controller the binary actually registers, and on 8.2.2 that is
`qemu-xhci`, which the firmware's own log shows it loaded (`XhciDxe`,
`UsbBusDxe`, `UsbMassStorageDxe`) before mounting the ESP. nec-1527 is not in
that build either; only `nec-usb-xhci` and `qemu-xhci` are.

What the two runs measured is worth more than the pass:

    virtio  1099 cs for 64 MB -> 5963 KB/s
    usb      558 cs for 64 MB -> 11744 KB/s
    direct kernel boot, run 11   -> 9102 KB/s

The bench is a sha256 of 64 MB inside the guest, pure CPU work, and it moved by
a factor of two between two runs of the same guest on the same machine minutes
apart. So the honest number for TCG on courbet is a band, roughly 6 to 12 MB/s,
not the single figure the first run suggested, and any plan built on it should
assume the low end.

Branch A is now closed as far as emulation goes: a firmware built from this
tree reads a FAT volume that lives in a file on userdata and boots an EFI binary
out of it, over both transports, on the device. The GPT does not have to be
touched, which was the point.
