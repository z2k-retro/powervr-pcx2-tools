# Appendix B — Scene formats: tile list, planes, TSP parameters

These are the PCX2 formats as the renderer in `renderer/vid_powervr_pcx_render.c` reads them. They come from the driver's packing code (`pkisp.c`, `pktsp.c`, `dregion.c`) and the simulator models in `Source/simulat3/` of Imagination's [Series 1 source release](https://github.com/powervr-graphics/PowerVR-Series1). Every point that differs from the simulator was measured on a Matrox m3D and is marked **(measured)**.

All addresses inside the ISP buffer are in 32-bit words of the TLB-mapped space (Appendix A, `TLB` and `PAGE_CTRL`). The frame starts at `OBJECT_OFFSET` (bytes; bit 0 is a driver flag and is masked off).

## Tile (region) list

The list is a run of 32-bit words. A **region word** starts each tile; the **object words** for that tile follow it.

| Word | Bits | Meaning |
| --- | --- | --- |
| Region | 30 | Always 1: marks a region word |
| | 4:0 | Width in 32-pixel units, minus 1 |
| | 14:5 | Height in lines, minus 1 |
| | 19:15 | X position in 32-pixel units |
| | 29:20 | Y position in lines |
| Object | 31 | Very last object of the frame |
| | 29 | Link: bits 23:0 are the word address where the list continues |
| | 28:19 | Number of planes in the object |
| | 18:0 | Word offset of the object's first plane |

- A tile ends when the next word (after following links) is a region word.
- PCX2 marks the start of a translucent pass with bits 31 and 29 both set on an object word. The bits are stripped (`word & 0x5FFFFFFF`) before the word is used.
- Tiles are normally 32×32 pixels, but the width and height fields allow larger regions.

## Planes (PCX2)

Each plane is four words:

| Word | Contents |
| --- | --- |
| 0 | A, IEEE single |
| 1 | B, IEEE single |
| 2 | C, IEEE single |
| 3 | Instruction in bits 3:0; TSP tag in bits 31:4 |

The value of a plane at pixel (x, y) is A·x + B·y + C. For a depth plane this is 1/w; larger is nearer.

### Instructions

| Code | Name | Use |
| --- | --- | --- |
| 0 | `forw_visib` | Front face, visible |
| 1 | `forw_invis` | Front face, invisible |
| 2 | `forw_perp` | Edge (perpendicular) plane |
| 3 | `test_shad_forw` | Shadow volume, front |
| 4 | `test_shad_perp` | Shadow volume, edge |
| 5 | `rev_visib` | Back face, visible |
| 6 | `rev_invis` | Back face, invisible |
| 7 | `rev_replace_if` | Back face, replace if |
| 8 | `forw_visib_fp` | First plane of an object, visible |
| 9 | `forw_invis_fp` | First plane of an object, invisible |
| 0xA | `forw_perp_fp` | First plane, edge |
| 0xB | `test_shad_forw_fp` | First plane of a shadow volume |
| 0xC | `test_shad_perp_fp` | First plane of a shadow volume, edge |
| 0xD | `test_shadow_rev` | Shadow volume, back |
| 0xE | `test_light_rev` | Light volume, back |
| 0xF | `begin_trans` | Start of a translucent pass |

The Direct3D driver builds most objects as one `forw_visib_fp` depth plane followed by `forw_perp` edges, one per polygon edge. How each instruction updates the 32 cells of a span (the I and U registers, shadow flags, translucent passes) follows the simulator's `hwsabsim.c`; the renderer's `expand_instruction()` and `cells_process()` are a direct translation.

### Fixed-point conversion

1. **Edge planes** (instructions 2, 4, 0xA, 0xC) are rescaled first: s = ApproxRecip(|C| + 2048·(|A| + |B|)). ApproxRecip is the simulator's bit trick (negated exponent, one's-complemented mantissa), not an exact reciprocal; an exact one leaves 25–42 edge pixels wrong per frame. Other planes use s = 1.
2. **A and B**: A·s and B·s, scaled by 2^39, keep 15 significant bits, truncated toward zero.
3. **C**: 2^31 · C · s, converted to a signed 32-bit integer **with saturation (measured)**. An edge plane with A = B = 0 scales to just above 1.0 and must count as inside.
4. **Evaluation**: v = A'·x + B'·y + 256·C' on a 40-bit adder. The comparators see v / 256:
   - depth planes: v / 256 rounded toward zero;
   - edge planes: the sign of the full sum, i.e. floor(v / 256) **(measured)**. A pixel is outside when the result is negative.

## TSP parameters

A tag points at texture-memory word `(tag & 0xFFFFF) × 2`. Tag 1 (word 2) is the PCX2 **masking plane** (Appendix E). The drivers reserve the lowest tags, so the first ordinary parameter block is at word 6 on PCX2 (12 on PCX1).

### Word 0 and word 1

| Word | Bits | Meaning |
| --- | --- | --- |
| 0 | 31 | Textured (words 2–7 follow) |
| | 30 | Smooth (Gouraud) shading |
| | 29 | Fog off for this object |
| | 28 | Shadow/light colour present |
| | 26 | Flat specular highlight |
| | 21:18 | Texture coordinate exponent |
| | 16:13 | Global translucency, added to texel alpha (0–15) |
| | 12:11 | Mirror U (bit 12) / V (bit 11) |
| | 10 | Translucent: blend with what is already in the tile |
| | 7:0 | Flat colour, red |
| 1 (flat) | 31:24 | Flat colour, green |
| | 23:16 | Flat colour, blue |
| | 15:0 | Shadow/light colour, 555 |
| 1 (smooth) | 31:16 | Shading origin x (signed) |
| | 15:0 | Shading origin y (signed) |

### Texture words (only when word 0 bit 31 is set)

All 16-bit fields are signed.

| Word | Bits 31:16 | Bits 15:0 |
| --- | --- | --- |
| 2 | mip scale: mantissa 31:24, exponent 23:18 | r |
| 3 | q | p |
| 4 | texture address bits 15:0 | c |
| 5 | b | a |
| 6 | flags and address bits 23:16 (below) | f |
| 7 | e | d |

Word 6 flags: bit 31 mip-mapped; bit 30 16-bit texels (else 8-bit 332); bits 29:28 map size (0 = 32×32, 1 = 64×64, 2 = 128×128, 3 = 256×256); bit 27 4444 (else 555); bits 23:16 texture address bits 23:16.

Texture coordinates are perspective-correct ratios, with k = `CAMERA` (driver value 0x3FF):

- u = (a·x + b·y + c·k) / (p·x + q·y + r·k), scaled by 2^exponent
- v = (d·x + e·y + f·k) / (p·x + q·y + r·k), same scale

The integer part is 8 bits (0–255) and 5 fraction bits drive the bilinear filter. The arithmetic, including the reduced-precision divide and the mip-level estimate from the squared 1/w, is in `texture_pixel()`.

### Shading words

These follow at word 8 (textured) or word 2 (untextured):

- **Smooth shading**: two words. Word A holds the colour (555) in bits 31:16 and the intensity at the origin in bits 15:0. Word B holds the y slope in bits 31:16 and the x slope in bits 15:0. If word 0 bit 28 is set, a second pair follows for the shadow/light colour.
- **Flat highlight** (word 0 bit 26): one word, the highlight colour (555) in bits 31:16 and the shadow highlight in bits 15:0.

Textured flat objects multiply the texel by the flat colour; smooth objects multiply it by the interpolated colour. Highlights are added with saturation, then fog (Appendix E and the reference, "Fog") and translucency are applied.
