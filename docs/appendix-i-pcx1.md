# Appendix I — How the PCX1 differs

The PCX1 (NEC µPD62010, PCI 1033:002A, driver type MIDAS4) is the first Series 1 chip. Most of this appendix comes from the driver source, the simulator and the 4.x VxDs, collected so that a PCX1 emulation can start from it. The section "Measured on an Apocalypse 3D" is the exception: it was read from a real PCX1 board with `pcx2dump`.

| Area | PCX1 | PCX2 |
| --- | --- | --- |
| PCI ID | 1033:002A | 1033:0046; `REVISION` 1 (rev 002) or 3 (rev 003) |
| Registers | 0x00–0x17 | 0x00–0x1E; adds `IEEEFP`, `BILINEAR`, `PCI21COMP`, `CLKSELECT`, `FASTFOG`, `POWERDOWN` |
| Divider table (0x200–0x3FF) | RAM (measured); the VxD loads `ReciprocalTable` at start-up | ROM (measured) |
| Fog table (0x080–0x0FF) | RAM (measured); loaded by the driver | ROM (measured); the driver still writes it |
| Region / object list | One contiguous DMA block; `OBJECT_OFFSET` is its physical address | Inside the TLB-mapped ISP space; `OBJECT_OFFSET` is a byte offset, OR 1 |
| Plane format | 3 words, fixed point (below) | 4 words, IEEE floats |
| First TSP tag | Index 12 | Index 6 |
| Texture writes | Driver does read-modify-write | Write only |
| Background | Always writes complete tiles | Masking plane leaves pixels untouched (Appendix E) |
| Filtering | No `BILINEAR` register | Three filter modes |

## Measured on an Apocalypse 3D

A volunteer ran `pcx2dump` three times on 2026-11-16 on a VideoLogic Apocalypse 3D: once plain, once with `--masks`, and once with `--texmem`. The card had not been set up by any driver. Everything below comes from those dumps.

**PCI.** The card is 1033:002A, revision 01, class 0x04 / subclass 0x80 (other multimedia). It has two 32-bit prefetchable memory BARs (BAR0 registers, BAR1 texture memory) and uses interrupt pin A. There is no option ROM and no capabilities list, as on the m3D.

**Registers at power-on.** `ID` reads 0x1033002A and `REVISION` reads 1. `TMEM_SETUP` and `TMEM_REFRESH` power up at 0x377777FF and 0x700, the same as the PCX2. `GPPORT` reads 2; the m3D reads 0xF616, so these bits are probably board straps. Every other register reads 0.

**Implemented bits** (write all ones, read back):

| Register | PCX1 | PCX2 (m3D) |
| --- | --- | --- |
| 0x06 `FOGAMOUNT` | 0x0000001F | same |
| 0x07 `OBJECT_OFFSET` | 0xFFFFFFFC | 0xFFFFFFFD: the PCX2 also stores bit 0, its "in the TLB space" flag |
| 0x08 `PAGE_CTRL` | 0x000003FF | same |
| 0x0A `ISP_BASE`, 0x0B `PREC_BASE` | 0x000FFFFF | same |
| 0x0E `FOGCOL` | 0x01FFFFFF | same, including bit 24 (fog enable) |
| 0x0F `CAMERA` | 0x000007FF | same |
| 0x10 `PACKMODE` | 0x0000007F | 0x000000FF: the PCX1 lacks bit 7 |
| 0x12 `LSTRIDE` | 0x00001FFC | same |
| 0x13 `SOFADDR` | 0xFFFFFFFC | same |
| 0x14 `XCLIP` | 0x13FF13FF | same |
| 0x09, 0x15, 0x17 | none | same |
| 0x18 `IEEEFP`, 0x19 `BILINEAR`, 0x1A, 0x1D `FASTFOG` | none: not implemented, read 0 | 0xF, 0x7, none, 0x1FF |

**Tables.**
- **The fog table (0x080–0x0FF) is RAM.** It powers up with random 8-bit values and accepts writes. On the PCX2 it is ROM holding the driver's table.
- **The divider table (0x200–0x3FF) is RAM.** It powers up with random contents: 32-bit even entries and 12-bit odd entries, matching the pairs of the driver's `ReciprocalTable`. This agrees with the driver, which loads the table only on PCX1. The contents stayed the same across the three runs, so they survive a warm restart.
- **Host access to the divider table goes through the entries in order, like the PCX2's ROM.** A sweep upward from 0x200 reads every entry. Reads and writes at other addresses do not reach the entry asked for. After a full sweep, reading 0x203, 0x2C5 or 0x3FF returned entry 0x201's value, and 0x3FE returned entry 0x200's. The `--masks` writes aimed at 0x200 and 0x3FF changed only 0x3FE and 0x3FF. Writing the whole table in one sweep from 0x200, which is what the VxD does, is the only pattern known to work.
- **The page table (0x100–0x17F) is there too:** 128 entries of 12 bits with random power-on values, the same as the PCX2. 0x180–0x1FF read 0. The PCX1 driver puts its region list in one contiguous DMA block, not in TLB-mapped pages, so what the PCX1 uses these entries for is still open.

**Texture memory.** The `--texmem` run's report stops right after the memory registers were set (`TMEM_SETUP` 0x22212275, `TMEM_REFRESH` 0x480, `ARBMODE` 1), before the first pattern pass. Whether the machine hung on the first BAR1 access or the program stopped some other way is still to be confirmed. The values are not the obvious cause. The 4.1.1 VxD's register table gives the PCX1 the same `TMEM_SETUP` (0x22212275) and `TMEM_REFRESH` (0x480) as the PCX2, and `ARBMODE` 1, which is what `pcx2dump` wrote. At start-up the VxD also loads the divider and fog tables and the other "Init" registers before any texture access. `pcx2dump` skips those, and one of them may matter on PCX1.

## PCX1 plane words

From the simulator's PCX1 path in `hwsabren.c`:

| Word | Bits | Meaning |
| --- | --- | --- |
| 0 | 19:0 | A, 20-bit packed float (the driver's `PackTo20Bit`) |
| | 25:20 | Tag bits 17:12 |
| | 29:26 | Instruction |
| 1 | 19:0 | B, 20-bit packed float |
| | 31:20 | Tag bits 11:0 |
| 2 | 31:0 | C, 32-bit fixed point |

On the PCX2, `IEEEFP` bit 1 turns on the chip's scaling of edge planes. Whether the PCX1 relies on the driver for this instead is not established. How the PCX1 ISP uses the mask half of each divider-table pair is still an open question (reference, "Open questions").

## Starting points

- The 86Box branch currently has a PCX1 chip option for register-level testing; its renderer handles only the PCX2 formats. The option may be removed before the device goes upstream.
- `Source/simulat3/` has `#if PCX2` / `#else` paths for most of the ISP; the `#else` branches are the PCX1 behaviour.
- A PCX1 card and the steps in Appendix H would settle the plane questions. The table questions are now settled (see "Measured on an Apocalypse 3D").
