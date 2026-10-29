# PowerVR PCX2 technical reference

What a PowerVR Series 1 PCX2 (as on the Matrox m3D) does, and how its Windows drivers drive it. It is written to be enough to emulate the chip or write a driver for it. Facts come from three places, and each section says which:

- Imagination's [Series 1 source release](https://github.com/powervr-graphics/PowerVR-Series1) (MIT licence);
- static analysis of the 4.x drivers;
- measurement on a real m3D with the tools in this repository.

## Documents

- **[using-86box.md](using-86box.md)**: user guide to the 86Box emulation (setup, settings, speed, compatibility, reporting problems).
- **[reference.md](reference.md)** — the main document:
  - the driver stack, board start-up, the per-frame sequence and the end-of-render interrupt;
  - the memory model, fog and documented rendering behaviour;
  - everything measured on the m3D, and the rendering model;
  - behaviours found by comparing game frames, the list of games compared, and open questions.

## Appendices

| | Appendix | Contents |
| --- | --- | --- |
| A | [Registers](appendix-a-registers.md) | Register map, defaults, when drivers write each register |
| B | [Scene formats](appendix-b-formats.md) | Tile list, plane words and instructions, fixed-point rules, TSP parameter words |
| C | [Texture memory](appendix-c-textures.md) | Pixel addressing, banks, twiddling, mip-chain layout, texel formats, filtering, mirroring under the filter |
| D | [Built-in tables](appendix-d-tables.md) | Fog and divider ROM contents |
| E | [Pixel output](appendix-e-output.md) | Frame-buffer writes, clipping, masking plane, formats, dither, fog, fast fog, render times |
| F | [VxD interface](appendix-f-vxd.md) | Private IOCTLs, shared structures, notes on earlier analysis |
| G | [PCXDUMP1 format](appendix-g-pcxdump.md) | The frame dump written by the 86Box device |
| H | [Measuring and testing](appendix-h-workflow.md) | How each measurement was made, step by step |
| I | [PCX1 differences](appendix-i-pcx1.md) | What changes for the PCX1, from source; not measured |
| J | [Driver settings](appendix-j-driver-settings.md) | Registry, App Hints and `sglhw.ini` settings; which reach chip registers; the INF's per-program hints |
