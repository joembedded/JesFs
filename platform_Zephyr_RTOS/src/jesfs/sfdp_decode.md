## Example-Extracting `sfdp-bfp` from raw SFDP data (Zephyr)

The `sfdp-bfp` property in Zephyr's Devicetree contains the **Basic Flash Parameter Table** (BFPT) from the flash chip's SFDP data.

> Here the SPI Flash on an NRF54L15-DK (MX25R0435F) is used.<br>
>Output for 'file jedec' (provided `CONFIG_SPI_NOR_SFDP_RUNTIME=y` is set in `prj.conf`)<br>
>FLASH: 'mx25r6435f@0'<br>
>JEDEC ID: C2 28 17<br>
>Flash size: 8192 kB

Example raw SFDP dump:

```text
SFDP=[128]:={
    53 46 44 50 06 01 02 FF 00 06 01 10 30 00 00 FF
    C2 00 01 04 10 01 00 FF 84 00 01 02 C0 00 00 FF
    FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
    E5 20 F1 FF FF FF FF 03 44 EB 08 6B 08 3B 04 BB
    EE FF FF FF FF FF 00 FF FF FF 00 FF 0C 20 0F 52
    10 D8 00 FF 23 72 F5 00 82 ED 04 CC 44 83 48 44
    30 B0 30 B0 F7 C4 D5 5C 00 BE 29 FF F0 D0 FF FF
    FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
    }
```

The SFDP header starts with the signature:

```text
53 46 44 50 = "SFDP"
```

The first parameter header starts at offset `0x08`:

```text
00 06 01 10 30 00 00 FF
```

This describes the Basic Flash Parameter Table:

```text
Parameter ID : 0xFF00  -> BFPT
Revision     : 1.6
Length       : 0x10 DWORDs = 16 * 4 = 64 bytes
Pointer      : 0x000030
```

Therefore, the BFPT starts at offset `0x30` and has a length of 64 bytes. Taking these 64 bytes from the raw SFDP dump gives the Devicetree value:

> This is what is used in the Devicetree:

```dts
sfdp-bfp = [
	e5 20 f1 ff ff ff ff 03 44 eb 08 6b 08 3b 04 bb
	ee ff ff ff ff ff 00 ff ff ff 00 ff 0c 20 0f 52
	10 d8 00 ff 23 72 f5 00 82 ed 04 cc 44 83 48 44
	30 b0 30 b0 f7 c4 d5 5c 00 be 29 ff f0 d0 ff ff
];
```

In short: read the BFPT parameter header, use its pointer and length fields, and copy exactly that byte range into the Zephyr `sfdp-bfp` Devicetree property.
