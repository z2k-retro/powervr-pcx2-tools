# Appendix C — Texture memory layout

The PCX2 has 4 MB of texture SDRAM, seen by the host as BAR1. TSP parameters (Appendix B) share it with the textures. Addresses in TSP words count 16-bit **pixels**.

## Pixel addressing

- Pixel address p is in BAR1 dword p >> 1. The **even** pixel is the **high** 16 bits of the dword, the odd pixel the low 16 bits.
- The memory has two banks; the second starts at pixel 0x100000 (2 MB). Mip levels alternate between banks (below).
- Drivers write texture memory with 32-bit stores. The PCX2 drivers never read it back (PCX1 drivers do read-modify-write).
- All 4 MB were measured present and independent on the m3D, with no mirroring.

## Twiddled (Morton) order

Maps are stored twiddled. For texel (u, v) of an n×n level, the pixel offset interleaves the bits: u bit i goes to offset bit 2i + 1, v bit i to offset bit 2i.

```c
offset = (spread(u) << 1) | spread(v);   /* spread(x): bit i -> bit 2i */
```

8-bit (332) maps hold two texels per pixel: the offset uses u >> 1, and an even u takes the high byte.

## Mip-mapped textures

A mip-mapped map of size S (0 = 256, 1 = 128, 2 = 64, 3 = 32) stores every level from 1×1 up. Level k (0 = full size) of such a map is at:

```c
size_index = S + k;                       /* 0 = 256x256 ... 8 = 1x1 */
base       = address;
if (size_index & 1) base ^= 0x100000;     /* odd sizes live in the other bank */
pixel      = base + 1 + mmc[size_index] + twid(u >> k, v >> k);

mmc[] = { 0x5555, 0x1555, 0x0555, 0x0155, 0x0055, 0x0015, 0x0005, 0x0001, 0x0000 };
```

`mmc[i]` is the number of pixels in all levels smaller than size i. The **+1 was measured** on the m3D (Ultim@te Race): the 1×1 level is the second pixel of the first word, and each larger level follows. The simulator's `AddressCalc()` omits it, which reads every mip-mapped texel one Morton step off and scatters thin details.

Non-mip-mapped maps start at `address` itself, with no bank toggle and no offset.

## Texel formats

| Format | Layout | Notes |
| --- | --- | --- |
| 555 | x RRRRR GGGGG BBBBB | Opaque |
| 4444 | AAAA RRRR GGGG BBBB | Alpha 0 = opaque, 15 = transparent; translucent objects |
| 332 | RRR GGG BB | 8-bit, no mip maps |

Filtering works on 5-bit channels; a channel c becomes (c << 3) + 4 when expanded to 8 bits.

## Filtering

`BILINEAR` (register 0x19) bits 1:0 select one mode for the whole frame:

| Value | Mode | Behaviour |
| --- | --- | --- |
| 0 | Bilinear | 2×2 bilinear within one mip level |
| 1 | Adaptive bilinear | Bilinear on the two finest mip steps (level estimate below 2); blending between two mip levels beyond that **(measured)** |
| 3 | Linear mip-mapping | One texel from each of two adjacent levels, blended; no bilinear |

The level estimate comes from the squared 1/w and the mip scale in TSP word 2; see `texture_pixel()` in the renderer. Mirroring (word 0 bits 12:11) reflects every other repeat of the map; on 256×256 maps it acts as if the map were 128.
