#!/usr/bin/env bash
# pcx2session.sh - have the real card draw 86Box's captured frames and compare
# each with the renderer. Any VM using the card must be off. The card must be
# bound to vfio-pci (scripts/win98vm.sh bind). Run from anywhere; it builds
# bin/pcx2play with make first.
#
#   bash pcx2session.sh [DUMP_FOLDER]
#
# DUMP_FOLDER defaults to the 86Box VM's powervr_dumps folder. Results go to
# pcx2session-DATE/ and pcx2session-DATE.tar.gz (send that file; see
# pcx2pack.sh to repack a folder or split it into small zips).

set -e
PCXDEV="${PCXDEV:-$(lspci -D -n -d 1033:0046 2>/dev/null | head -1 | cut -d" " -f1)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -n "$PCXDEV" ] || { echo "No PCX2 card found (lspci -d 1033:0046); set PCXDEV=DDDD:BB:DD.F"; exit 1; }
DUMPS="${1:-$HOME/86box-pcx-vms/win98-pcx/powervr_dumps}"
OUT="pcx2session-$(date +%Y%m%d-%H%M%S)"

drv="$(basename "$(readlink -f "/sys/bus/pci/devices/$PCXDEV/driver" 2>/dev/null)" 2>/dev/null)"
if [ "$drv" != "vfio-pci" ]; then
	echo "The card is not bound to vfio-pci. Run: bash $ROOT/scripts/win98vm.sh bind   (and keep any VM off)"
	exit 1
fi
ls "$DUMPS"/frame_*.bin >/dev/null 2>&1 || { echo "No frame dumps in $DUMPS"; exit 1; }

# 86Box never empties the dump folder, so frames from an earlier game can
# still be there. Warn if the dumps were written more than 10 minutes apart.
NEWEST=$(stat -c %Y "$DUMPS"/frame_*.bin | sort -n | tail -1)
OLD=$(for f in "$DUMPS"/frame_*.bin; do if [ $((NEWEST - $(stat -c %Y "$f"))) -gt 600 ]; then basename "$f"; fi; done)
if [ -n "$OLD" ]; then
	echo "Warning: these dumps are older than the rest and may be from another run:"
	echo "$OLD" | sed 's/^/  /'
	echo "Empty $DUMPS before the next capture to keep one game per session."
fi

make -C "$ROOT" bin/pcx2play >/dev/null
mkdir -p "$OUT"
cp "$DUMPS"/frame_*.bin "$OUT"/ 2>/dev/null || true

play() { sudo "$ROOT/bin/pcx2play" --dev "$PCXDEV" --out "$OUT" "$@" || true; }

FIRST="$(ls "$DUMPS"/frame_*.bin | head -1)"

# 1. Every captured frame exactly as the driver set it up (16-bit, dithered).
n=0
for f in "$DUMPS"/frame_*.bin; do
	n=$((n + 1))
	play --name "as-captured-$n" "$f"
done

# 2. The first frame with one setting changed at a time.
play --name dither-off          --set PACKMODE=0x02 "$FIRST"
play --name 555-dither-off      --set PACKMODE=0x03 "$FIRST"
play --name 555-dither-on       --set PACKMODE=0x13 "$FIRST"
play --name 32bpp               --set PACKMODE=0x00 "$FIRST"
play --name 24bpp               --set PACKMODE=0x01 "$FIRST"
play --name bilinear-adaptive   --set PACKMODE=0x02 --set BILINEAR=1 "$FIRST"
play --name mipmap-no-bilinear  --set PACKMODE=0x02 --set BILINEAR=3 "$FIRST"
play --name fog-strong          --set PACKMODE=0x02 --set FOGAMOUNT=2 "$FIRST"
play --name fog-weak            --set PACKMODE=0x02 --set FOGAMOUNT=8 "$FIRST"
play --name fog-red             --set PACKMODE=0x02 --set FOGAMOUNT=2 --set FOGCOL=0x01ff0000 "$FIRST"
play --name fog-red-bit24-off   --set PACKMODE=0x02 --set FOGAMOUNT=2 --set FOGCOL=0x00ff0000 "$FIRST"
play --name prec-base-0x100     --set PACKMODE=0x02 --set PREC_BASE=0x100 "$FIRST"
play --name camera-0x200        --set PACKMODE=0x02 --set CAMERA=0x200 "$FIRST"

sudo chown -R "$USER" "$OUT"
echo
grep -E "^== |rendered in|NO end|differ|skipped" "$OUT/report.txt"
echo
echo "Note: prec-base-0x100 always differs. It moves the parameter area in a way no driver uses and the emulation does not model; ignore it."
echo
bash "$ROOT/scripts/pcx2pack.sh" "$OUT"
