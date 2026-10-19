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
3. Install Windows and the 2D card's driver, then the Matrox m3D driver (tested: the retail 4.1.1 driver). Windows 98 SE already includes a new enough DirectX (6.1). The PowerVR SGL demos and the m3D's Direct3D driver should then work.

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

The frame is drawn on its own threads while the emulated PC keeps running. The summary log line shows whether your host keeps up:

```
PowerVR: frames 501-600: render 6.2 ms average (11.0 max) on 8 thread(s), emulation waited 0.0 ms per frame; emulated render time 7.6 ms
```

If **emulation waited** is near 0, the renderer is not what is slowing the VM; the emulated CPU is. If it is high, add render threads. For comparison, a real m3D takes about 13–27 ms per frame in the games tested (reference, Appendix E).

## Compatibility

"Compared on the card" means frames captured in 86Box were drawn on a real m3D and checked pixel by pixel ([reference](reference.md#games-compared-on-the-m3d)).

| Software | Status | Notes |
| --- | --- | --- |
| SGL demos (SGLTri, logo) | Compared on the card: exact | |
| Moto Racer (demo) | Compared on the card: 0–13 pixels differ | The demo is slow at 640×480 on real hardware too |
| Shadows of the Empire | Compared on the card: 0–5 pixels | The "Hardware Assessment" warning at start-up comes from the Matrox driver's capabilities and appears on a real m3D too. Menu seams and dim text are on the real card as well. |
| Ultim@te Race | Compared on the card: exact | Fog and mip-mapping |
| Quake (PowerVR) timedemo | Compared on the card: exact | |
| Unreal (castle flyby) | Compared on the card: 0–2 pixels | |
| MDK | Compared on the card: exact | |
| Tomb Raider, Hexen II demo | Look right | Not yet compared |
| Terracide | Minor flicker | Not yet captured |
| POD | Crashes at launch | Probably the game |
| MechWarrior 2 (PowerVR patch) | Not working yet | Needs the Windows 95 version of MechWarrior 2 |

Not yet tested: shadow volumes and light volumes, and the `FASTFOG` mode.

## Known limitations

- Render time is fixed at 7.6 ms per frame. On the real card it depends on the scene (13–27 ms in the games tested), so the emulation can run faster than real hardware.
- PCX1 boards (Apocalypse 3D) are not emulated.
- 86Box may log "Illegal instruction … (FF)" after the driver starts. That is Windows software deliberately executing an invalid opcode, which a real CPU traps the same way; it is harmless.

## Reporting a problem

Please include:

- a screenshot;
- the 86Box log with Logging on Summary;
- the game and version;
- your machine and 2D card.

If a frame looks wrong, dump a few frames around it (settings above). Dumps contain the game's textures, so send them privately rather than posting them publicly. Someone with a card can then draw the same frames on real hardware and show whether the problem is in the emulation or in the game.
