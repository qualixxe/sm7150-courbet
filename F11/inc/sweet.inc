// per-device BSP DXEs
# FILE DRIVER = 8e9bd160-b184-11df-94e2-0800200c9a66 {
#     SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/DALSYSDxe/DALSYSDxe.depex
#     SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/DALSYSDxe/DALSYSDxe.efi
#     SECTION UI = "DALSYSDxe"
# }

FILE DRIVER = 5bd181db-0487-4f1a-ae73-820e165611b3 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/ButtonsDxe/ButtonsDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/ButtonsDxe/ButtonsDxe.efi
    SECTION UI = "ButtonsDxe"
}

FILE DRIVER = cd823a4d-7dec-4531-ae5d-4134fa4127b8 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/UsbConfigDxe/UsbConfigDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/UsbConfigDxe/UsbConfigDxe.efi
    SECTION UI = "UsbConfigDxe"
}

FILE DRIVER = 11faed4c-b21f-4d88-8e48-c4c28a1e50df {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/UsbPwrCtrlDxe/UsbPwrCtrlDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/UsbPwrCtrlDxe/UsbPwrCtrlDxe.efi
    SECTION UI = "UsbPwrCtrlDxe"
}

FILE DRIVER = 94f8a6a7-dc34-4101-88c1-99179cceae83 {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/sm7150/UsbfnDwc3Dxe/UsbfnDwc3Dxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/sm7150/UsbfnDwc3Dxe/UsbfnDwc3Dxe.efi
    SECTION UI = "UsbfnDwc3Dxe"
}

// USB host (OTG keyboard/mouse): sweet binaries, wired for sm7150/sm7325.
// XhciDxe deliberately has no DXE_DEPEX: its stock depex wants IOMMU
// protocols (HALIOMMU is off in this tree); as a binding driver it
// attaches once XhciPciEmulation publishes PCI I/O. If it hangs, revert.
FILE DRIVER = de2184cc-c141-45c3-9640-fdd784df785e {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbInitDxe/UsbInitDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbInitDxe/UsbInitDxe.efi
    SECTION UI = "UsbInitDxe"
}

FILE DRIVER = 33089622-0999-4ca3-915d-a1d1b19024cf {
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/XhciPciEmulation/XhciPciEmulation.efi
    SECTION UI = "XhciPciEmulation"
}

FILE DRIVER = 6fa34ba3-ce4d-4b08-acb2-d68936b6de9d {
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/XhciDxe/XhciDxe.efi
    SECTION UI = "XhciDxe"
}

FILE DRIVER = 1f02dd59-e90c-4281-9f54-92ad13c918ab {
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbBusDxe/UsbBusDxe.efi
    SECTION UI = "UsbBusDxe"
}

FILE DRIVER = cd7fafcb-c826-415f-8476-b98f60ea3bcd {
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbKbDxe/UsbKbDxe.efi
    SECTION UI = "UsbKbDxe"
}

FILE DRIVER = 4d41f597-51e5-49e3-ba3b-5cb8a28aa41b {
    SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbMsdDxe/UsbMsdDxe.depex
    SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/UsbMsdDxe/UsbMsdDxe.efi
    SECTION UI = "UsbMsdDxe"
}

# FILE DRIVER = 5776232E-082D-4B75-9A0E-FE1D13F7A5D9 {
#     SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/PmicDxe/PmicDxeLa.depex
#     SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/PmicDxe/PmicDxeLa.efi
#     SECTION UI = "PmicDxe"
# }

# FILE DRIVER = 9A00771F-36D4-4DD5-8916-C48ED9B16B86 {
#     SECTION DXE_DEPEX = Dxe/EFI_Binaries/Drivers/Devices/sweet/HALIOMMUDxe/HALIOMMUDxe.depex
#     SECTION PE32 = Dxe/EFI_Binaries/Drivers/Devices/sweet/HALIOMMUDxe/HALIOMMUDxe.efi
#     SECTION UI = "HALIOMMU"
# }

// Mainline device tree blob
# FILE FREEFORM = 25462CDA-221F-47DF-AC1D-259CFAA4E326 {
# 	 SECTION RAW = sm7150-xiaomi-sweet.dtb
# }
