#!/usr/bin/env bash
# win98vm.sh - Windows 98 SE virtual machine for tracing the real PowerVR card.
#
#   bash win98vm.sh create          make the virtual disk (once)
#   bash win98vm.sh install         boot the Windows 98 CD to install
#   bash win98vm.sh patch           boot the Patcher9x floppy
#   bash win98vm.sh run             normal boot from the hard disk
#   bash win98vm.sh bind            hand the real m3D to vfio-pci (once per host boot)
#   bash win98vm.sh trace           boot with the real m3D passed through, logging
#                                   every register access to $VMDIR/trace-*.log
#   bash win98vm.sh snapshot NAME   save the disk's current state under NAME (VM off)
#   bash win98vm.sh restore NAME    roll the disk back to snapshot NAME (VM off)
#   bash win98vm.sh list            list snapshots
#
# Paths below can be overridden from the environment (VMDIR, ISO, QEMU, ...).

# Override per run, e.g.  QEMU=/usr/bin/qemu-system-i386 bash win98vm.sh install
QEMU="${QEMU:-$HOME/qemu/build/qemu-system-i386}"
QIMG="${QIMG:-$HOME/qemu/build/qemu-img}"
VMDIR="${VMDIR:-$HOME/win98-qemu}"
DISK="$VMDIR/win98.qcow2"
ISO="${ISO:-$HOME/Downloads/Windows 98 Second Edition.iso}"
M3DISO="${M3DISO:-$VMDIR/m3d.iso}"   # the card's driver CD as an image, if you have one
PCXDEV="${PCXDEV:-$(lspci -D -n -d 1033:0046 2>/dev/null | head -1 | cut -d" " -f1)}"   # the card's host address; set PCXDEV to override
PATCHER="$(ls "$HOME"/Downloads/*atcher9x*.ima "$HOME"/Downloads/*atcher9x*.img 2>/dev/null | head -1)"

# The emulated machine: 440FX chipset, Pentium III, 256 MB (Windows 98 dislikes >512 MB),
# Cirrus Logic 5446 PCI video (driver built into Windows 98), no network.
# ACPI is off: Windows 98 Setup otherwise installs its ACPI support, which is a
# common cause of hangs under QEMU. Sound is off unless SOUND=1 (not needed for
# tracing; the emulated SB16 is another suspect). The QEMU monitor is on this
# terminal: type "info status" or "info kvm" there, "quit" to stop the VM.
SOUNDDEV=(); [ "${SOUND:-0}" = 1 ] && SOUNDDEV=(-device sb16)
COMMON=(
	-machine pc,accel=kvm,hpet=off,acpi=off
	-cpu pentium3
	-m 256
	-rtc base=localtime
	-vga "${VGA:-cirrus}"
	"${SOUNDDEV[@]}"
	-nic none
	-monitor stdio
	-drive "file=$DISK,if=ide,index=0,media=disk,format=qcow2"
	-display sdl
	-name "Win98 tracing VM"
)

need() { [ -e "$1" ] || { echo "Missing: $1"; exit 1; }; }

case "$1" in
	create)
		need "$QIMG"
		mkdir -p "$VMDIR"
		[ -e "$DISK" ] && { echo "$DISK already exists; not overwriting."; exit 1; }
		"$QIMG" create -f qcow2 "$DISK" 2G
		;;
	install)
		need "$QEMU"; need "$DISK"; need "$ISO"
		exec "$QEMU" "${COMMON[@]}" -drive "file=$ISO,if=ide,index=2,media=cdrom" -boot order=dc
		;;
	patch)
		need "$QEMU"; need "$DISK"
		[ -n "$PATCHER" ] || { echo "Patcher9x floppy image (.ima) not found in ~/Downloads"; exit 1; }
		exec "$QEMU" "${COMMON[@]}" -drive "file=$PATCHER,if=floppy,format=raw" -boot order=a
		;;
	run)
		need "$QEMU"; need "$DISK"
		exec "$QEMU" "${COMMON[@]}" -drive "file=$ISO,if=ide,index=2,media=cdrom" -boot order=c
		;;
	bind)
		# Give the card to vfio-pci so QEMU can pass it through. Lasts until the
		# host reboots. Reads nothing from and writes nothing to the card itself.
		sudo modprobe vfio-pci
		echo vfio-pci | sudo tee "/sys/bus/pci/devices/$PCXDEV/driver_override" >/dev/null
		if [ -e "/sys/bus/pci/devices/$PCXDEV/driver" ]; then
			echo "$PCXDEV" | sudo tee "/sys/bus/pci/devices/$PCXDEV/driver/unbind" >/dev/null
		fi
		echo "$PCXDEV" | sudo tee /sys/bus/pci/drivers_probe >/dev/null
		lspci -k -s "$PCXDEV" | grep -i "driver in use"
		# Let your own user (not root) open the card's VFIO group, so QEMU can
		# run with your normal audio and display.
		GRP="$(basename "$(readlink "/sys/bus/pci/devices/$PCXDEV/iommu_group")")"
		sudo chown "$USER" "/dev/vfio/$GRP"
		ls -l "/dev/vfio/$GRP"
		;;
	trace)
		need "$QEMU"; need "$DISK"
		drv="$(basename "$(readlink -f "/sys/bus/pci/devices/$PCXDEV/driver" 2>/dev/null)" 2>/dev/null)"
		[ "$drv" = "vfio-pci" ] || { echo "The m3D is not bound to vfio-pci; run: bash $0 bind"; exit 1; }
		GRP="$(basename "$(readlink "/sys/bus/pci/devices/$PCXDEV/iommu_group")")"
		[ -w "/dev/vfio/$GRP" ] || { echo "/dev/vfio/$GRP is not writable by you; run: bash $0 bind"; exit 1; }
		# VFIO pins all guest RAM; raise this shell's locked-memory limit so the
		# QEMU it starts (as you, not root) is allowed to.
		sudo prlimit --memlock=unlimited:unlimited --pid $$ || exit 1
		CD="$ISO"; [ -e "$M3DISO" ] && CD="$M3DISO"
		LOG="$VMDIR/trace-$(date +%Y%m%d-%H%M%S).log"
		echo "CD: $CD"
		echo "Tracing to $LOG"
		# x-no-mmap=on routes every BAR access through QEMU so it can be logged;
		# x-no-kvm-intx=on does the same for the card's interrupts.
		# VGA=std picks the standard VGA card (for the VBEMP driver) instead of Cirrus.
		exec "$QEMU" "${COMMON[@]}" \
			-drive "file=$CD,if=ide,index=2,media=cdrom" -boot order=c \
			-device "vfio-pci,host=$PCXDEV,x-no-mmap=on,x-no-kvm-intx=on" \
			-trace "vfio_region_read" -trace "vfio_region_write" -trace "vfio_intx*" \
			-D "$LOG"
		;;
	snapshot) need "$QIMG"; [ -n "$2" ] || { echo "usage: $0 snapshot NAME"; exit 1; }; "$QIMG" snapshot -c "$2" "$DISK" && echo "Saved snapshot '$2'";;
	restore)  need "$QIMG"; [ -n "$2" ] || { echo "usage: $0 restore NAME"; exit 1; }; "$QIMG" snapshot -a "$2" "$DISK" && echo "Restored '$2'";;
	list)     need "$QIMG"; "$QIMG" snapshot -l "$DISK";;
	*) sed -n '2,15p' "$0"; exit 1;;
esac
