# Appendix E — Pixel output

The PCX2 has no frame buffer of its own. It finishes each tile on chip, then writes pixels over PCI into the 2D card's frame buffer.

## Where pixels go

- `SOFADDR` (0x13) is the physical address of pixel (0, 0); `LSTRIDE` (0x12) is the line pitch in bytes.
- Pixel (x, y) is written at `SOFADDR + y × LSTRIDE + x × bytes_per_pixel`.
- `XCLIP` (0x14) limits the columns written:
  - with bit 28 set, columns x ≥ (bits 25:16) are not written. Measured: MDK sets `XCLIP` = 0x12590000 (right limit 601) for its 600-pixel 3D view, and the m3D writes column 600 but not 601. The drivers write the render width + 1 (`pvrd.c`, `sgllite.c`), so the chip always writes one column past the scene.
  - with bit 12 set, columns to the left of (bits 9:0) are not written. No driver sets it, and the exact limit has not been measured; the renderer leaves out x < (bits 9:0) − 1.

  At 640×480 the drivers write 0x12810000, a right limit of 641, beyond the frame.

## Masking plane

TSP tag 1 (texture-memory word 2) is the **masking plane**. Where it is the visible surface at the end of a tile, or when a translucent pass shades it, the chip writes nothing, so the frame buffer's previous contents show through. Translucent objects drawn over it blend with what the chip holds internally (black, unless something else was drawn there first). Measured on the m3D with Shadows of the Empire and Moto Racer.

The Direct3D driver uses the masking plane as its background (`PackTexasMask` in `pktsp.c`: "Masking id is always 2"), so a Direct3D frame leaves untouched every pixel no polygon covers. SGL programs use an ordinary background plane and write every pixel. PCX1 always writes complete tiles.

## Output formats

`PACKMODE` (0x10) bits 1:0 select the format; bit 4 turns on dithering.

| Value | Format | Bytes per pixel | Layout |
| --- | --- | --- | --- |
| 0 | 32 bpp | 4 | bytes B, G, R, 0 |
| 1 | 24 bpp | 3 | bytes B, G, R |
| 2 | 16 bpp 565 | 2 | 16-bit value RRRRR GGGGGG BBBBB, stored little-endian |
| 3 | 15 bpp 555 | 2 | 16-bit value 0 RRRRR GGGGG BBBBB, stored little-endian |

## Dither (measured)

With `PACKMODE` bit 4 set, an offset from a 4×4 matrix is added to each 8-bit channel before it is cut to 5 bits, saturating at the top. The 6-bit green of 565 gets half the offset.

```
D[y mod 4][x mod 4] =
    7 3 6 2
    1 5 0 4
    6 2 7 3
    0 4 1 5

red5   = min(31, (red8   + D) >> 3)
green6 = min(63, (green8 + D/2) >> 2)     /* 565 */
blue5  = min(31, (blue8  + D) >> 3)
```

This matches the m3D on every pixel of every test frame, in both 16-bit modes.

## Fog

Fog is applied per pixel after texturing and shading, when `FOGCOL` bit 24 is set **(measured)** and the object's TSP word 0 bit 29 is clear:

```
f      = fog factor from the fog table (Appendix D), 0..255
colour = colour + (((fog_colour - colour) * f) >> 8)     /* per channel */
```

`FOGCOL` bits 23:0 hold the fog colour as 0xRRGGBB.

## Render time (measured)

| Frame | Bits per pixel | Time from STARTRENDER to interrupt |
| --- | --- | --- |
| SGLTri, one triangle, 640×480 | 16 | 7.6 ms |
| SGL logo demo | 16 | 12.7 ms |
| SGL logo demo | 24 | 14.6 ms |
| SGL logo demo | 32 | 16.6 ms |
| Ultim@te Race | 16 | 13.6 ms |
| Moto Racer demo | 16 | 24 ms |
| Shadows of the Empire | 16 | 27 ms |
