# Appendix I — How the PCX1 differs

The PCX1 (NEC µPD62010, PCI 1033:002A, driver type MIDAS4) is the first Series 1 chip. Nothing here was measured: this project has only a PCX2. It comes from the driver source, the simulator and the 4.x VxDs, and is collected so that a PCX1 emulation can start from it.

| Area | PCX1 | PCX2 |
| --- | --- | --- |
| PCI ID | 1033:002A | 1033:0046; `REVISION` 1 (rev 002) or 3 (rev 003) |
| Registers | 0x00–0x17 | 0x00–0x1E; adds `IEEEFP`, `BILINEAR`, `PCI21COMP`, `CLKSELECT`, `FASTFOG`, `POWERDOWN` |
| Divider table (0x200–0x3FF) | RAM; the VxD loads `ReciprocalTable` at start-up | ROM (measured) |
| Fog table (0x080–0x0FF) | Loaded by the driver | ROM (measured); the driver still writes it |
| Region / object list | One contiguous DMA block; `OBJECT_OFFSET` is its physical address | Inside the TLB-mapped ISP space; `OBJECT_OFFSET` is a byte offset, OR 1 |
| Plane format | 3 words, fixed point (below) | 4 words, IEEE floats |
| First TSP tag | Index 12 | Index 6 |
| Texture writes | Driver does read-modify-write | Write only |
| Background | Always writes complete tiles | Masking plane leaves pixels untouched (Appendix E) |
| Filtering | No `BILINEAR` register | Three filter modes |

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
- A PCX1 card and the steps in Appendix H would settle the table and plane questions quickly.
