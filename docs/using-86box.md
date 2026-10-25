# Using the PCX2 emulation in 86Box

The `powervr-pcx` branch of [z2k-retro/86Box](https://github.com/z2k-retro/86Box/tree/powervr-pcx) adds a PowerVR PCX2 (Matrox m3D) as a second graphics card, next to the emulated 2D card, as in a real PC. It runs the real Matrox drivers. On every game compared so far, its output matches a real m3D to within a few pixels per frame.

This is a test build, not part of official 86Box. Please report problems to this repository's issues, not to the 86Box project.

## Getting it

- **Windows**: download `86Box-PowerVR-Windows-64-….zip` from the fork's [Releases](https://github.com/z2k-retro/86Box/releases), unzip it anywhere, and use it with the [86Box ROM set](https://github.com/86Box/roms) like any 86Box build. Builds of every commit are also under the fork's Actions tab (GitHub login needed).
- **Linux**: `bash scripts/build86box-pcx.sh deps` (Debian/Ubuntu packages), then `bash scripts/build86box-pcx.sh build` and `… run`. It builds in `~/86box-pcx`, separate from any 86Box you already have.

The `-devsettings` builds add developer and test settings (below). You don't need them to play.

## Setting up a machine

1. Make a Windows 95 or 98 machine with PCI slots: a Pentium MMX or Pentium II board, at least 32 MB of RAM, and any 2D card with Windows drivers. The m3D renders into the 2D card's memory, as the real card does.
2. In **Settings → Display**, tick **PowerVR PCX1/PCX2 Graphics**. **Configure** opens the settings below.
3. Install Windows and the 2D card's driver, then a PowerVR driver: the one on the Matrox m3D CD (4.0.x) or a later 4.1.x. Most games here were compared with the m3D CD's driver, and World Cup 98 and Forsaken with PowerVR 4.1.0.2. Windows 98 SE already includes a new enough DirectX (6.1). The PowerVR SGL demos and the m3D's Direct3D driver should then work.

Matrox's `MGASETUP` utility crashes in the VM because it expects Matrox's own 2D card. The driver installer works, and nothing else needs `MGASETUP`.

## Settings

| Setting | Default | What it does |
| --- | --- | --- |
| Render threads | 4 | Host threads drawing each frame. Set it to your CPU's performance-core count (6–8 on a current desktop). The output is identical whatever the setting. |
| Logging | Summary | **Summary**: start-up, problems, and every 100 frames the host render time and how long emulation waited for it. **Every render and interrupt** and **Every register access** are for debugging and slow things down. |
| Dump N frames to powervr_dumps | 0 | Saves the complete input of N frames, starting at the frame below, to the VM's `powervr_dumps` folder. A dump can be rendered without the game (`pcxsim`) or drawn on a real card (`pcx2play`). |
| Start dumping at frame | 1 | Frames are counted from when 86Box starts. The log's last line, "PowerVR: N frames rendered", helps you pick a number. |

### Developer settings

These appear only in builds configured with `-D POWERVR_PCX_DEV=ON`, which is also the default with `DEV_BRANCH`. They exist to test the emulation against hardware; ordinary builds fix them to the values measured on a Matrox m3D.

| Setting | Fixed value without the flag | Purpose |
| --- | --- | --- |
| Chip | PCX2 | PCX1 registers only; there is no PCX1 renderer |
| PCX2 REVISION register | 1 (m3D) | 3 reports a PCX2 rev 003 |
| PCI revision ID, subsystem vendor/ID | 1, 0, 0 | As read from the m3D |
| Interrupt acknowledge | Reading INTSTATUS clears it | The other modes were hypotheses before the card was measured |
| Render time (µs) | 7600 | Delay from STARTRENDER to the end-of-render interrupt |
| Debug marker tile | Off | Draws a fixed tile at the frame origin, to check output addressing |

## Speed

The frame is drawn on its own threads while the emulated PC keeps running. Every 100 frames the log shows how the host is keeping up:

```
PowerVR: frames 501-600: render 9.8 ms average (14.0 max) on 8 thread(s), emulation waited 0.0 ms and wrote output 0.35 ms per frame; emulated render time 7.6 ms + 2.4 ms held back for the host
```

- **render**: host time to draw a frame. More render threads bring it down.
- **held back for the host**: when the host is slower than the emulated card's 7.6 ms, the card simply stays busy longer, as a real m3D does on a heavy frame (13–80 ms in the games tested). The emulated PC keeps running at full speed meanwhile; the game just gets fewer frames per second, never slow motion.
- **emulation waited**: time the emulated PC itself had to stop; it should be 0.

The emulated CPU is usually the real limit: a Pentium II at 450 MHz is demanding for 86Box on any host, with or without the PowerVR. A slower emulated CPU (a Pentium 166–266) is plenty for most PowerVR games.

Builds before pcx-v0.3 stopped the emulated PC whenever the host had not finished a frame. Games that count timer ticks, such as the DOS Tomb Raider and native PowerVR Quake, then ran in slow motion while their own counters showed a normal frame rate.

## Compatibility

"Compared on the card" means frames captured in 86Box were drawn on a real m3D and checked pixel by pixel ([reference](reference.md#games-compared-on-the-m3d)).

| Software | Status | Notes |
| --- | --- | --- |
| SGL demos (SGLTri, logo, mask and sphere, lit room) | Compared on the card: exact | |
| Moto Racer (demo) | Compared on the card: 0–13 pixels differ; exact at 512×384, 0–2 at 640×400, exact in 16:9 and 0–7 in split screen | The demo is slow at 640×480 on real hardware too |
| Shadows of the Empire | Compared on the card: 0–1 pixels with the 1.1 patch, at 640×480 and 320×240 | **Use LucasArts' 1.1 update (`shadowsp.exe`)**, which added PCX2 and m3D support; it fixes the menus and the see-through probe droid legs. Fog is missing on the PCX2 even with 1.1: LucasArts' own 1.1 notes say "There will be no fog in the game" on this chipset. Version 1.0 also runs, with a "Hardware Assessment" warning, menu seams and pale droid legs, and matches the card (0–5 pixels) |
| Ultim@te Race | Compared on the card: exact | Fog and mip-mapping |
| Quake (PowerVR) timedemo | Compared on the card: exact | |
| Unreal (castle flyby) | Compared on the card: 0–2 pixels | |
| GLQuake (PowerVR minigl) | Compared on the card: exact, at 640×480 and 1024×768 (`glquake -width 1024 -height 768`) | Busiest scenes tested; the card takes about 44 ms per frame at 640×480 and 79 ms at 1024×768 |
| Half-Life | Compared on the card: exact (opening tram ride) | Loads and renders as on a real m3D; how well it plays on PowerVR is a separate question |
| Quake II | Compared on the card: 1 pixel | Needs a fast host: smooth on a Core Ultra 7 270K, slow on an i7-3960X. The card itself takes about 37 ms per frame here |
| MDK | Compared on the card: exact | |
| MoonRacer | Compared on the card: exact | Shadow and light volumes in every frame |
| Winter Race 3D (demo) | Compared on the card: exact | |
| Flight Unlimited (Compaq OEM) | Compared on the card: 0–1 pixels | Mirrors all its textures |
| Sub Culture | Compared on the card: 0–1 pixels | |
| X demo (non-interactive) | Compared on the card: exact | 800×600; needs a build from after pcx-v0.2 |
| World Cup 98 | Compared on the card: 0–2 pixels | Its installer offers to install PowerVR driver 4.1.0.2 |
| Forsaken | Compared on the card: 0–3 pixels | The card itself takes about 80 ms per frame |
| Wipeout XL | Compared on the card: 0–1 pixels | |
| PureVex | Compared on the card: exact | Uses the card's adaptive bilinear filtering |
| MechWarrior 2 (PowerVR patch) | Compared on the card: exact | The patch is for the Windows 95 version; it does not run with the DOS version |
| Tomb Raider (DOS PowerVR demo) | Compared on the card: 0–3 pixels | Uses PowerVR's DOS driver; needs a build from after pcx-v0.1 for frame dumps |
| Hexen II demo | Looks right | Not yet compared |
| Terracide | Minor flicker | Not yet captured |
| POD | Compared on the card: 0–1 pixels | Needs a 4.1 PowerVR driver; it crashed at launch with the m3D CD's older driver |

Not yet seen in any game captured so far, so not yet checked against the card:

- textures mirrored in v with bilinear filtering (8-bit textures and every other mirroring case match the card in edited frames; no game seen so far uses either);
- windowed Direct3D (every screen size from 320×200 to 1024×768 matches);
- bilinear modes other than the adaptive one PureVex uses (the others were tested only by changing the register);
- scenes too large for one pass, which the drivers split into several renders;
- the `FASTFOG` mode.

## Known limitations

- Render time is fixed at 7.6 ms per frame. On the real card it depends on the scene (13–27 ms in the games tested), so the emulation can run faster than real hardware.
- PCX1 boards (Apocalypse 3D) are not emulated.
- With a Matrox Millennium as the 2D card, 320-wide screen modes (320×240, 320×200, some split-screen modes) show the picture doubled side by side. That is 86Box's Millennium display, not the PowerVR: the 3D frames match the real card, and the S3 Trio64 shows the same modes correctly.
- 86Box may log "Illegal instruction … (FF)" after the driver starts. That is Windows software deliberately executing an invalid opcode, which a real CPU traps the same way; it is harmless.

## Reporting a problem

Please include:

- a screenshot;
- the 86Box log with Logging on Summary;
- the game and version;
- your machine and 2D card.

If a frame looks wrong, dump a few frames around it (settings above). Dumps contain the game's textures, so send them privately rather than posting them publicly. Someone with a card can then draw the same frames on real hardware and show whether the problem is in the emulation or in the game.
