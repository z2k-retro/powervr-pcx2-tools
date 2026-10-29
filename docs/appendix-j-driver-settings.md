# Appendix J — Driver settings (registry, App Hints and sglhw.ini)

The 4.x drivers read several dozen settings when SGL or the Direct3D HAL starts up. Most change how the driver builds a scene. A few go straight into chip registers, and those can switch on modes that no game uses by default. This appendix covers where the settings live, the order in which they are read, and what the settings that touch the chip do.

Sources: the Series 1 source release (`win32/brdcfg.c`, `profile.c`, `pcx/hwregs.c`, `pvrd.c`, `sgllite.c`, `dvdevice.c`), the 4.1.1c3 Developer driver's `PowerVR.inf`, and disassembly of its `SGLMID4.dll` (the settings routine at 0x1004E5F0). The chip effects marked **(measured)** were checked on the Matrox m3D.

## Where the settings are read from

All registry keys are under `HKEY_LOCAL_MACHINE\SOFTWARE\PowerVR\PCX1&2\`. For each setting the driver tries, in order:

1. `HWINI\<section>` — the global value. The section is `Defaults` for SGL and `PVRHAL` for the Direct3D HAL.
2. `App Hints\<program>.exe` — the per-program value. The key is named after the running program's file name, without the path.
3. `%WINDIR%\sglhw.ini`, section `[default]` — this wins over both registry values.

Points to watch:

- **Registry values must be strings (REG_SZ).** A value of any other type, such as a DWORD, is skipped (the driver logs "Weird data type"). Numbers are parsed with `strtoul`/`atoi`, so "1" works, and so does "0x1FF" for the global values.
- **The registry is read only if `SOFTWARE\PowerVR\PCX1&2\StartUp`, value `BoardType`, exists** (as a string). `pvrinit.exe` and the installer set it up. Without it, only `sglhw.ini` is read.
- The Matrox m3D uses the same `PowerVR\PCX1&2` key as VideoLogic boards: `MATROX_REGISTRY_STRING` and `VL_REGISTRY_STRING` are the same in `pvrosapi.h`.
- Settings are read once, when the program first starts SGL or the HAL. Restart the program after changing them.

**The 4.1.1c3 driver's raw register overrides.** `SGLMID4.dll` (and `SGLMID5.dll`) read three more ini keys after working out the register values, from section `[PCXRegs]` of `sglhw.ini`. Each key's integer value is written to the chip unchanged:

| Key | Register | Default (what the settings above produce) |
| --- | --- | --- |
| `PCX_BILINEAR` | 0x19 `BILINEAR` | from the filter settings below |
| `PCX_FASTFOG` | 0x1D `FASTFOG` | 0x1FF if `FastFogON` is non-zero, else 0 |
| `PCX_ARBMODE` | 0x11 `ARBMODE` | 1 or 0xD, by board type |

Windows' `GetPrivateProfileInt` reads these in decimal. This is the only way to give `BILINEAR` the value 2, or `FASTFOG` a threshold other than 0xFF.

The Display Properties PowerVR page writes only a few values. Its "Standard / Optimized (recommended)" choice is `StrictLocks` (1 / 0) in `HWINI\Defaults`, and "HAL on / off" is `EnableHAL` in `HWINI\PVRHAL`. Neither reaches a chip register.

## Settings that reach chip registers

| Setting | Read by | Effect |
| --- | --- | --- |
| `BilinearCtlEnable` (default 0) | SGL | Non-zero leaves filtering entirely to the program; `BILINEAR` starts at 3 |
| `BilinearState` (default unset) | SGL | Unset: `BILINEAR` starts at 3 and the program may change it. 0: forced to 3. Non-zero: forced to the `BilinearFilteringMode` result, overriding the program's own choice |
| `BilinearFilteringMode` (default 2; `HWINI\Defaults` sets 3) | SGL | 1 → `BILINEAR` 1 (adaptive); 2 or 3 → 0 (full bilinear); any other value counts as 2. The setting's numbering is not the register's: the registry can never produce register value 2 |
| `SDRAMCachingOff` (default 0) | SGL | Non-zero sets `BILINEAR` bit 2 (SDRAM caching off) |
| `FastFogON` (default 0) | SGL | Non-zero sets `FASTFOG` = 0x1FF: fast fog on, threshold 0xFF (see Appendix E) |
| `Dithering` (default 2) | SGL | 2 leaves dithering to the program; 0 or 1 forces `PACKMODE` bit 4 (4×4 ordered dither) off or on |
| `ForceBitsPerPixel` (default 0) | SGL | Forces the output bits per pixel; 0 means the default, 16 |
| `RenderTimeout` (default 5) | SGL | Render timeout, in tenths of a second |

With the INF's defaults, an SGL program gets `BILINEAR` = 3 (no filtering) unless its App Hints set `BilinearState` = 1. Programs can also set the filter themselves (SGL quality settings, or the Direct3D texture filter state). For example, Dashboard's own switch toggles `BILINEAR` between 0 and 3 **(measured)**.

## Settings that change how scenes are built

These never touch a register. They change the parameters the driver writes, and so they can change what a captured frame looks like.

| Setting | Effect (from source) |
| --- | --- |
| `Near Z Clip` (float) | Near clipping plane as a minimum 1/z; Flight Unlimited's hint is 0.000001 |
| `AddToXY` (float) | Offset added to screen x and y |
| `IncScreenXYby1` | Makes the device one pixel larger each way ("fix for Mech2"); MechWarrior 2's hint |
| `FullSort`, `SglTransSort` | Sorting of translucent polygons (`SglTransSort` 3 = full sort in the INF's hints) |
| `MaxNumPasses` | Maximum translucent passes per tile |
| `DisableNewPassPerTri` | Whether each translucent triangle may start a new pass |
| `FullMaskingPlane`, `SetMaskingBGColour` | Full-screen or minimal masking plane, and whether the program may set its colour |
| `SW_MergeHeight`, `SW_MinYSize` | Region sizes for the tile list |
| `ISPParamSize`, `TSPParamSize`, `ObjectParamSize` | Parameter buffer sizes, in KB |
| `Export565`, `ColourKey`, `Deferred2D`, `Need3DOn2D`, `AutoMIPMap`, `TableFog`, `VertexFog`, `GouraudSpecular`, `AllowQuads`, `Enable24BitMode` | Direct3D HAL behaviour (texture formats offered, colour keying, 2D/3D interaction, fog type) |
| `DisablePVR` | Hides the PowerVR from the program |

## The INF's per-program hints

`PowerVR.inf` (4.1.1c3) installs App Hints for 48 programs. The most common:

| Setting | Programs | Value |
| --- | --- | --- |
| `BilinearState` / `BilinearCtlEnable` | 19 (Flight Unlimited, MechWarrior 2, POD, …) | 1 / 0: driver-chosen full bilinear |
| `Need3DOn2D` | 13 (D3D SDK samples) | 1 |
| `SglTransSort`, `DisableNewPassPerTri` | 10 (WipEout, Moto Racer, G-Police, Die Hard Trilogy, …) | 3, mostly 1 |
| `AutoMIPMap` | 9 (MDK, Terracide, Independence Day, …) | 1 |
| `DisablePVR` | 6 (Agile Warrior, CART Precision Racing, Monster Truck Madness, …) | 1 |
| `Deferred2D`, `ColourKey` | 5 each | mostly 0 |
| `Near Z Clip` | Flight Unlimited (4 executables) | 0.000001 |
| `BilinearFilteringMode` | Flight Unlimited | 3 (full bilinear) |
| `Dithering` | Moto Racer, TOCA, Heavy Gear | 1 |
| `IncScreenXYby1` | MechWarrior 2 | 1 |

No hint turns on `FastFogON` or selects `BILINEAR` = 2.

## Trying the hidden modes

Settings in the registry did not reach the m3D in one test: a fogtest session with them set still showed `BILINEAR` 3 and `FASTFOG` 0. The likely causes are DWORD values, or the wrong key. The ini file needs no registry setup. To try fast fog (decimal values):

```
[PCXRegs]
PCX_FASTFOG=511
PCX_BILINEAR=2
```

Both modes were measured on the m3D with edited frame dumps instead (Appendix E, Appendix C, and the reference's test list), and the 86Box renderer models both.
