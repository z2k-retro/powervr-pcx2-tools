# powervr-pcx2-tools

Tools and documentation for the **PowerVR Series 1 PCX2**, the 3D chip on the Matrox m3D and VideoLogic Apocalypse 3Dx (1997). They were made to build and verify the PCX2 emulation in [86Box](https://86box.net), developed on the [`powervr-pcx` branch of z2k-retro/86Box](https://github.com/z2k-retro/86Box/tree/powervr-pcx). With them, a real card can be measured, its driver traced, and emulator output compared with the card pixel for pixel.

**Just want to try it?** See [Using the PCX2 emulation in 86Box](docs/using-86box.md): downloads, setup, settings, speed and a compatibility list.

That emulation now matches a real m3D to within a few pixels per 640×480 frame on every game captured so far, including Moto Racer, Shadows of the Empire, Ultim@te Race, MechWarrior 2, MoonRacer, Quake, GLQuake, Quake II, Unreal and MDK. The full list is in [the reference](docs/reference.md#games-compared-on-the-m3d).

## What's here

| Path | What it is | Needs a card? |
| --- | --- | --- |
| [`docs/using-86box.md`](docs/using-86box.md) | User guide to the 86Box emulation | No |
| [`docs/`](docs/README.md) | Technical reference: registers, scene and texture formats, output, measured behaviour, ROM tables, procedures | — |
| `renderer/` | The PCX2 renderer (hidden-surface removal and texturing), the same code as the 86Box device; no 86Box dependencies | No |
| `tools/pcxsim.c` | Renders a captured frame in software and compares it with the card's output | No |
| `tools/pcx2dump.c` | Reads a card: PCI config, power-on registers, implemented register bits, texture-memory test | Yes |
| `tools/pcx2replay.c` | Replays a frame traced from the Windows driver on the card, with no VM | Yes (VFIO) |
| `tools/pcx2play.c` | Draws an 86Box frame dump on the card and compares it with the renderer | Yes (VFIO) |
| `scripts/pcx2session.sh` | Runs `pcx2play` over a folder of dumps and register variants, and packs the results to send | Yes (VFIO) |
| `scripts/win98vm.sh` | Windows 98 VM under QEMU with the card passed through, logging every access | Yes (VFIO) |
| `scripts/build86box-pcx.sh` | Builds 86Box from the `powervr-pcx` branch | No |

## No card? You can still help

- **Play games in the 86Box build** and report what looks wrong, with a screenshot and the log. The [user guide](docs/using-86box.md#reporting-a-problem) says what to include.
- **Capture frames.** The 86Box device can dump the complete input of a frame (Appendix G). Anyone with a card can then draw your frame on real hardware and show whether the emulation or the game is at fault. Dumps contain the game's textures, so share them privately.
- **Work on the renderer without hardware.** `pcxsim` renders dumps and compares them with a card's output. The renderer is plain C with no 86Box dependencies, and every behaviour it models is described in the [reference](docs/README.md).
- **Card owners**: `pcx2dump` and `scripts/pcx2session.sh` (Appendix H) take a few minutes and would be especially useful for a PCX1 (Apocalypse 3D) or any board other than the Matrox m3D.

Open questions are listed at the end of the [reference](docs/reference.md#open-questions-for-emulation).

## Building

```sh
make            # bin/pcx2dump bin/pcx2replay bin/pcx2play bin/pcxsim
```

Linux, a C99 compiler and the kernel's VFIO headers. `pcxsim` and the renderer build anywhere with pthreads.

## Using a card

The card tools need root, an IOMMU enabled in firmware and on the kernel command line, and (for the VFIO tools) the card bound to `vfio-pci`: `bash scripts/win98vm.sh bind`. They find the first PCX1/PCX2 by PCI ID; `--dev` or `PCXDEV=` picks another. Appendix H of the reference walks through each step, from reading a new card to comparing game frames.

The card can only reach memory these tools map for it. The worst outcome seen in testing was a card that stopped responding until the next reboot.

## Not included

These are not in the repository, because they are not ours to distribute:

- Drivers, SDK files and their disassembly, which belong to Imagination Technologies and the board makers. The [Series 1 source release](https://github.com/powervr-graphics/PowerVR-Series1) is public, and the driver collection used is on Vogons.
- Frame dumps and traces. They contain games' textures and models, so capture your own (Appendix H).

## Licence and credits

GPL-2.0-or-later (see `LICENSE`), to match 86Box. The renderer adapts Imagination Technologies' SABRE and TEXAS software models from the Series 1 source release, under the MIT licence; that notice is kept in `renderer/vid_powervr_pcx_render.c`.

Hardware, testing and direction: Zvi Rosen. The tools, renderer and documentation were written with substantial help from Claude, Anthropic's AI model. Statements marked as measured were checked on the real card by the methods in Appendix H; the rest come from the driver source and binaries, as the reference says for each.
