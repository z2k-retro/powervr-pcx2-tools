# Appendix H — Measuring a card and testing an emulator against it

Everything in the reference marked "measured" was produced with the tools in this repository, on Linux with the card in a PCI slot. The card tools need root, an IOMMU (VT-d or AMD-Vi enabled in firmware and on the kernel command line) and the card bound to `vfio-pci`. Only `pcx2dump` works without VFIO.

The rig used: Matrox m3D (PCX2 rev 1), Intel Core i7-3960X, Ubuntu, QEMU with KVM, 86Box built from the `powervr-pcx` branch.

## 1. Read the card: `pcx2dump`

```sh
make
sudo bin/pcx2dump              # config space, option ROM, power-on registers
sudo bin/pcx2dump --masks      # also: implemented bits of the harmless registers
sudo bin/pcx2dump --texmem     # also: set up and test all 4 MB of texture memory
```

Run it with no VM using the card. It finds the first PCX1/PCX2 by PCI ID; `--dev DDDD:BB:DD.F` picks another. `--texmem` first programs the memory registers exactly as the driver does, because reading texture memory before that can hang the machine.

## 2. Trace the real driver: `scripts/win98vm.sh`

A Windows 98 SE VM under QEMU with the card passed through. Every register and texture-memory access is logged.

```sh
bash scripts/win98vm.sh create      # once: make the disk
bash scripts/win98vm.sh install     # install Windows 98 from $ISO
bash scripts/win98vm.sh bind        # once per host boot: give the card to vfio-pci
bash scripts/win98vm.sh trace       # boot with the card; log to $VMDIR/trace-*.log
```

Lessons from getting this working:
- Keep ACPI off in the VM; otherwise Windows 98 Setup installs ACPI support and hangs. If Windows then shows no PCI bus, add "PCI bus" with Add New Hardware and point it at `C:\WINDOWS\SYSTEM\IOSUBSYS`.
- `x-no-mmap=on` routes register accesses through QEMU so they are logged, and `x-no-kvm-intx=on` does the same for interrupts. The script sets both.
- The driver points `SOFADDR` at the emulated VGA card's legacy window, so the real card's pixel writes land in guest RAM and crash Windows. Take the trace anyway; the replay in step 3 avoids the problem.
- Right after the frame starts, save guest memory at the `(qemu)` prompt with `pmemsave 0 0x1000000 ram16.bin`.

## 3. Replay a traced frame: `pcx2replay`

```sh
sudo bin/pcx2replay trace-….log ram16.bin
```

This maps the saved RAM into the card's view of memory at the addresses the VM used, replays every logged write, starts the render and records what the card drew and how the interrupt behaves. `--dry-run` parses the trace without a card. `pcxsim replay` renders the same pair in software for comparison.

## 4. Capture frames from 86Box

In the PowerVR device settings, set **Dump N frames** and **Start dumping at frame**, then play to the scene you want. 86Box prints "PowerVR: N frames rendered" to its log on exit, which helps pick the start frame. The file format is in Appendix G.

## 5. Draw the captures on the card: `scripts/pcx2session.sh`

```sh
bash scripts/pcx2session.sh ~/86box-pcx-vms/win98-pcx/powervr_dumps
```

For each dump, the card draws the frame as captured, and then the first frame again with one register changed at a time (output formats, dither, filter modes, fog settings, `CAMERA`, `PREC_BASE`). The renderer draws the same input, and the script writes a folder with:

- `NAME-card.ppm`, `NAME-emu.ppm` and `NAME-diff.ppm` (white where they differ);
- `NAME-card.raw`, the card's frame;
- `report.txt`, with render times, `INTSTATUS` and the count of differing pixels.

Before drawing, `pcx2play` fills the frame with 0xA5A5, so pixels the card did not write are easy to spot.

To share the results, the script then runs `scripts/pcx2pack.sh`, which packs `report.txt`, the `.raw` files and the dumps into one `pcx2session-DATE.tar.gz` (the pictures can be regenerated from those). Some chat apps open `.zip` attachments and count each file inside against a per-message limit; a `.tar.gz` goes as one file. `ZIP_PARTS=19 bash scripts/pcx2pack.sh FOLDER` instead makes zips of at most 19 files, to send one per message.

## 6. Investigate offline: `pcxsim`

```sh
bin/pcxsim dump frame_000600.bin out.ppm NAME-card.raw
SET=10=2,19=1 bin/pcxsim dump frame_000600.bin out.ppm bilinear-adaptive-card.raw
```

With a card frame given, it prints the first differing pixels. Every behaviour in the reference's "Behaviour found with game frames" table was found this way:
1. Trace one differing pixel through the planes and TSP words.
2. Form a hypothesis.
3. Check that the fix leaves every earlier capture exact.

## Safety

The card only reaches memory these tools map for it (the IOMMU enforces this). The worst outcome seen was a card that stopped responding until the next reboot. Do not run any card tool while a VM is using the card.
