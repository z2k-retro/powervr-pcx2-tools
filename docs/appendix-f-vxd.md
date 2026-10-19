# Appendix F — VxD interface and shared structures

These are needed only to emulate or replace the Windows driver stack itself, not the chip.

## VxD private IOCTL interface

User-mode DLLs talk to `VSGL.VXD` through `DeviceIoControl`. The numbers come from the VxD's dispatch table (code = table index − 1; entries −1 and 0 are the standard close and open stubs), and the names from the `VSGL_*` call sites in `win32/system.c`; the header that defined them, `vsgl.h`, is not in the release. The table layout is the same in every 4.x VxD checked.

| Code | Name | In → out | Notes |
| --- | --- | --- | --- |
| 0x01 | GET_VERSION | — → version | Used by `pvrinit.exe` |
| 0x02 / 0x03 | ALLOCATEBLOCK / FREEBLOCK | pages → `SHAREDBLOCK` | Contiguous locked blocks (blk1/blk2 pools) |
| 0x04 / 0x05 | ALLOC / FREE PHYSALIAS | `SHAREDBLOCK` | Map physical memory |
| 0x06 / 0x07 | INSTALL / DEINSTALL ISR | — | Obsolete; fail |
| 0x08 | WBINVD | — | Flush CPU caches |
| 0x09 | SETCACHEMODE | 12 bytes | Page cache bits (PWT/PCD) |
| 0x0A | COPYBUFFER | 16 bytes |  |
| 0x0B / 0x0C | PM_SETSTATE / PM_GETSTATISTICS | — |  |
| 0x0D | GET_SELECTOR_BASE | — |  |
| 0x0E | GETPHYSADDRESS | linear (4) → physical (4) | Linear to physical address |
| 0x0F / 0x10 | ALLOC / FREE PCX ISP BUFFER | — |  |
| 0x11 / 0x12 | REGISTER_EOR_ASYNC_CB | — | End-of-render callback to a ring-3 thread |
| 0x13 | RESET_VXD | — | Recovers the VxD after a crashed app |
| 0x14 / 0x15 | READ / WRITE MSR | — | Model-specific registers |
| 0x16 | ENUM_PHYSICAL_BOARDS | — → count + 16-byte `PciDeviceTable` records |  |
| 0x17 / 0x18 | GET / RELEASE PHYSICAL_BOARD | board id |  |
| 0x19 / 0x1A | ENABLE / DISABLE PHYSICAL_BOARD | board id |  |
| 0x1B | GET_PHYSICAL_BOARD_INFO | board id → `BoardDataBlock` (0xA4) |  |
| 0x1C / 0x1D | CREATE / DESTROY DMA_SCATTER_BUFFER | bytes (4) → `DMASCATTERBUFFER` (0x410) | ISP buffer |
| 0x1E / 0x1F | ALLOC / FREE VIRTUAL_PAGES | pages → pointer |  |
| 0x20 | PAGE_RESERVE | pages (4) → linear (4) | TSP buffer: 500 pages reserved |
| 0x21 | PAGE_COMMIT | {page, count} (8) → (4) |  |
| 0x22 / 0x23 | PAGE_DECOMMIT / PAGE_FREE | — |  |
| 0x24 | SCHEDULE_RENDER | {LDEVICE*, BUFFER_LIST*} (8) | Starts a frame ([reference](reference.md#per-frame-render-sequence)) |
| 0x25 / 0x26 | SCHEDULER_LOCK / UNLOCK | board | Keeps the scheduler from starting renders |
| 0x27 | PAGE_REALLOCATE | {address, pages} (8) → linear | Grows the TSP buffer |
| 0x28 | GET_MAN_TYPE | — → 0 VideoLogic/NEC, 1 Matrox |  |

## Shared structures

These are the offsets the VxD dereferences, matched against the definitions in `pvrosapi.h` (MAX_MEM_IO_WINDOWS = 6). They are what make the raw disassembly readable.

| Structure | Offset | Field |
| --- | --- | --- |
| `BoardDataBlock` (0xA4, array of 4 at 10000ce0 in the developer build) | +0x00 | DeviceType (2 PCX1, 3 PCX2, 4 PCX2_003) |
|  | +0x18 | RevisionID |
|  | +0x1C | IRQ |
|  | +0x20 | nMemWindows |
|  | +0x24 / +0x28 | PhysMemWindows[0] base / size (BAR0) |
|  | +0x2C / +0x30 | PhysMemWindows[1] base / size (BAR1) |
|  | +0x54 / +0x58 | LinearMemWindows[0] (registers) / [1] (texture memory) |
|  | +0x6C, +0x70 | nIOWindows, IOWindows[0] |
|  | +0xA0 | pStatus (volatile status DWORD, bit 0 = render ended) |
| `BUFFER_LIST` | +0x08 | PCXBuffer.PageSize (0/1/2 = 4/8/16 KB) |
|  | +0x10 | PCXBuffer.SlotsUsed |
|  | +0x14 | PCXBuffer.PhysAddresses[128] |
|  | +0x41C | PVRParamBuf[3] (pBuffer, uBufferPos, uBufferLimit) |
|  | +0x440 | DestAddr[3] |
|  | +0x44C | Registers[32] (SOFADDR at +0x498) |
| `LDEVICE` (logical device) | +0x28 | RenderStatus (1 = end of render) |
|  | +0x2C | RenderTimeout |
|  | +0x30 | Registers[32] shadow |
|  | +0xB8 | VirtualBuffer (BUFFER_LIST*) |
|  | +0xBC | RendersPending |
| `DMASCATTERBUFFER` (0x410) | +0x00 | LinearAddress, PagesAllocated, BlockSize, SlotsUsed, PhysAddress[128], PhysPages[128] |

## Status of the earlier third-party analysis

The static analysis supplied with the driver collection observed the binaries accurately. Most of its open questions are answered by names in the source release.

| Its finding | Status now |
| --- | --- |
| 512-byte exponential table copied to +0x200 | Confirmed: `FogTable` → `FOG_TABLE`; formula verified for all 128 entries; consumer is the fog unit (Fog) |
| 2 KB mask/coefficient table copied to +0x800, PCX1 only | Confirmed and named: `ReciprocalTable` → `DIVIDER_TABLE`. Its edge/coverage hypothesis is not supported; PCX2 skips it because the table is built into the chip as ROM (measured 2026-10-17), not because of `IEEEFP` floating point |
| Both tables shared across 4.x vendors, absent from 3.0.x VxDs | Confirmed; in 3.0.x they live in `SGLMID4.DLL` (and `SGLMID3.DLL`) |
| "Modes 2/3/4" | `DEVICE_TYPE` MIDAS4 (PCX1), MIDAS5 (PCX2 rev 002), MIDAS5_003 |
| Mapped +4 selects the PCX2 mode | `REVISION` register: 1 or 3 |
| +8 written 1 then 0 with a delay | `SOFTRESET` pulse; also before every render |
| 41-DWORD (0xA4) four-entry record | `BoardDataBlock[4]` (below) |
| IOCTL 0x20/0x21, 0x1C, 0x1D, 0x01, 0x16, 0x28 | PAGE_RESERVE/COMMIT, CREATE/DESTROY_DMA_SCATTER_BUFFER, GET_VERSION, ENUM_PHYSICAL_BOARDS, GET_MAN_TYPE |
| IOCTL 0x0E in pvrhal32 | GETPHYSADDRESS |
| CONFIGMG Get_Alloc_Log_Conf / Get_Device_ID thunks | Confirmed (object 2 thunks) |
| "No defensible register map yet" | Superseded by `pcx/hwregs.h` ([Appendix A](appendix-a-registers.md)) |
