
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