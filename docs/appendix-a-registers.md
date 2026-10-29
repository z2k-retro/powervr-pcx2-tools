# Appendix A — Register map

BAR0 is DWORD-indexed: register *n* is at byte 4·*n*. The measured write masks of the PCX2 registers are in the reference, under "Measured on the Matrox m3D".


The VxD walks registers 0x00–0x17 on PCX1 and 0x00–0x1E on PCX2. "Init" means written once at device start from the VxD's default table; "Frame" means copied from the user-mode shadow before every render. Defaults are identical in all 4.x VxDs, except that 4.0.x writes ARBMODE at init only. Bitfields come from `pcx/hwregs.c` and `pcx/texas.c`.

| Reg | Byte | Name | Chip | Written | Default PCX1 / PCX2 | Meaning |
| --- | --- | --- | --- | --- | --- | --- |
| 0x00 | 0x000 | ID | both | never | — | Reads 0x10330046 (vendor and device ID) on the Matrox m3D; not read by the 4.x drivers |
| 0x01 | 0x004 | REVISION | both | read | — | PCX2: 1 = rev 002, 3 = rev 003; the Matrox m3D reads 1 |
| 0x02 | 0x008 | SOFTRESET | both | pulse | — | Write 1 then 0, then about 1000 loop iterations; done at init and before every render |
| 0x03 | 0x00C | INTSTATUS | both | read | — | Bit 1 = end of render; the ISR's read is the only acknowledge |
| 0x04 | 0x010 | INTMASK | both | Init | 2 / 2 | Enables bit 1 |
| 0x05 | 0x014 | STARTRENDER | both | strobe | — | VxD writes 0 to start the render |
| 0x06 | 0x018 | FOGAMOUNT | both | Frame | 0 | Fog shift (SGL density 0–31); 0 when fog is off |
| 0x07 | 0x01C | OBJECT_OFFSET | both | Frame | 0 | PCX1: physical address of the region/object-pointer buffer. PCX2: byte offset of that buffer inside the TLB-mapped ISP space, OR 1 |
| 0x08 | 0x020 | PAGE_CTRL | both | Init + VxD per frame | 0 | Bits 0–7 = ISP buffer physical address >> 24; bits 8–9 = TLB page size (00 4 KB, 01 8 KB, 11 16 KB) |
| 0x0A | 0x028 | ISP_BASE | both | Init | 0x00080000 | ISP overflow area, "bottom of the second bank" |
| 0x0B | 0x02C | PREC_BASE | both | Init | 0 | TSP parameter area, "bottom of the first bank" of texture memory |
| 0x0C | 0x030 | TMEM_SETUP | both | Init | 0x22212275 | 4 MB setting; 2 MB = 0x12212275, 1 MB = 0x02211275; power-on value 0x377777FF |
| 0x0D | 0x034 | TMEM_REFRESH | both | Init | 0x480 | Texture SDRAM refresh; power-on value 0x700 |
| 0x0E | 0x038 | FOGCOL | both | Frame | 0 | 0x01000000 \| R≪16 \| G≪8 \| B (bit 24 = fog enable, measured) |
| 0x0F | 0x03C | CAMERA | both | Frame | 0 | "CFR scale"; drivers always write 0x3FF |
| 0x10 | 0x040 | PACKMODE | both | Frame | 0 | Output format: 0 = 32 bpp, 1 = 24, 2 = 16 (565), 3 = 15 (555); bit 4 = dither |
| 0x11 | 0x044 | ARBMODE | both | Init + Frame | 0x1 / 0xD | Bus arbitration mode |
| 0x12 | 0x048 | LSTRIDE | both | Frame | 0 | Frame-buffer stride in bytes |
| 0x13 | 0x04C | SOFADDR | both | Frame | 0 | Physical start-of-frame address (VxD converts from linear) |
| 0x14 | 0x050 | XCLIP | both | Init + Frame | 0 | Bit 12 left enable, bits 0–9 left (1–1023); bit 28 right enable, bits 16–25 right |
| 0x15 | 0x054 | ABORTADDR | both | Init | 0 | Unknown |
| 0x16 | 0x058 | GPPORT | both | Init | 0 | General-purpose port; reads 0xF616 at power-on (possibly board straps) |
| 0x18 | 0x060 | IEEEFP | PCX2 | Init | — / 7 | Bit 0 IEEE floats; bit 1 edge scaling for perpendicular edges; bit 2 pointer concatenation |
| 0x19 | 0x064 | BILINEAR | PCX2 | Init + Frame | — / 0 | Bits 0–1: 00 bilinear, 01 adaptive, 11 "linear mip-mapping", used as filtering off (user-mode default); 10 behaves exactly like 11 **(measured)**; bit 2 = SDRAM caching off. See Appendix C and, for the driver settings, Appendix J |
| 0x1B | 0x06C | PCI21COMP | PCX2 | Init | — / 0 | Bit 0 clear = PCI 2.1 compliance on |
| 0x1C | 0x070 | CLKSELECT | PCX2 | Init | — / 0 | Bits 0–2: SDRAM MEMCLK delay (000 = 1 PVRCLK) |
| 0x1D | 0x074 | FASTFOG | PCX2 | Init + Frame | — / 0 | Bit 8 enable, bits 0–7 fog-factor threshold: opaque pixels fogged at least that much are written as plain fog colour **(measured)**; see [Appendix E](appendix-e-output.md). 0x1FF when the `FastFogON` setting is used ([Appendix J](appendix-j-driver-settings.md)) |
| 0x1E | 0x078 | POWERDOWN | PCX2 | Init | — / 1 | Bit 0 = internal RAM power-down enabled |
| 0x7D–0x7F | 0x1F4 | MEMTEST_* | both | never | — | Memory test data/mode/result |
| 0x080–0x0FF | 0x200 | FOG_TABLE | both | Init | FogTable | 128 entries, see Fog |
| 0x100–0x1FF | 0x400 | TLB | both | VxD per frame | — | Entry *i* = physical page number (address >> 12) of ISP buffer slot *i* |
| 0x200–0x3FF | 0x800 | DIVIDER_TABLE | PCX1 | Init | ReciprocalTable | 256 pairs of (mask, coefficient); PCX1 only |

Registers 0x09, 0x17 and 0x1A are never written. In `hwregs.c` the user-mode soft reset is disabled with the comment "Apparently this doesn't work", yet the 4.x VxD pulses SOFTRESET before every render.
