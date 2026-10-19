# Appendix G — PCXDUMP1 frame dump format

The 86Box PCX2 device can save the complete input of a frame at the moment the driver writes `STARTRENDER`. In its settings, **Dump N frames** sets how many and **Start dumping at frame** sets the first; frames are counted from when 86Box starts. Files are named `frame_NNNNNN.bin` and go in the VM's `powervr_dumps` folder.

`pcxsim dump` renders these files without a card, and `pcx2play` draws them on a real card. A dump holds everything the chip reads, including texture memory, so dumps from commercial games contain those games' textures and models; share them accordingly.

All fields are little-endian.

| Offset | Size | Contents |
| --- | --- | --- |
| 0 | 64 | Header (below) |
| 64 | 4096 | Register file: BAR0 dwords 0x000–0x3FF as the device holds them |
| 4160 | `tlb_entries × page_bytes` | ISP buffer: each TLB page in order, read through the TLB, so the buffer appears as one contiguous range |
| … | `object_bytes` | PCX1 only: the region/object buffer at the physical address in `OBJECT_OFFSET` (0 on PCX2) |
| … | `texmem_bytes` | Texture memory from the start of BAR1 (4 MB) |

## Header

| Offset | Type | Field | Meaning |
| --- | --- | --- | --- |
| 0 | char[8] | magic | `PCXDUMP1` |
| 8 | uint32 | version | 1 |
| 12 | uint32 | frame | Frame number (STARTRENDER count since 86Box started) |
| 16 | uint32 | chip | 1 = PCX1, 2 = PCX2 |
| 20 | uint32 | page_bytes | ISP page size from `PAGE_CTRL` bits 9:8 (4096, 8192 or 16384) |
| 24 | uint32 | tlb_entries | TLB entries the driver wrote for this frame (0 if bus mastering was off) |
| 28 | uint32 | object_bytes | Size of the PCX1 object buffer |
| 32 | uint32 | texmem_bytes | 4194304 |
| 36 | uint32 | pci_command | PCI command register at the time |
| 40 | uint32[6] | reserved | 0 |

## Using a dump

- **Renderer input**: point `pcxr_frame_t.isp` at the ISP block (`isp_words` = `tlb_entries × page_bytes / 4`) and `tex` at the texture block. The registers give `OBJECT_OFFSET` (0x07), `FOGAMOUNT` (0x06), `FOGCOL` (0x0E), `CAMERA` (0x0F), `PACKMODE` (0x10), `XCLIP` (0x14) and `BILINEAR` (0x19). `tools/pcxsim.c` is a complete, short example.
- **Card input**: `pcx2play` maps the ISP block at an IOVA of its own and rewrites `PAGE_CTRL` and the TLB to point there. It also points `SOFADDR` at its own buffer, then replays the registers and texture memory exactly as the driver set them.
