#!/usr/bin/env bash
# build86box-pcx.sh - build 86Box with the PowerVR PCX2 device (the powervr-pcx
# branch of github.com/z2k-retro/86Box), in its own folder, separate from any
# release 86Box you already have.
#
#   bash build86box-pcx.sh deps      install build tools (Debian/Ubuntu; asks for sudo)
#   bash build86box-pcx.sh build     clone the branch and build
#   bash build86box-pcx.sh update    fetch the latest branch and rebuild
#   bash build86box-pcx.sh run       start it with its own VM folder
#
# SRC, VMS and REPO can be overridden from the environment.

set -e
SRC="${SRC:-$HOME/86box-pcx}"                    # source and build
VMS="${VMS:-$HOME/86box-pcx-vms/win98-pcx}"      # the VM folder (its own 86box.cfg and disk)
REPO="${REPO:-https://github.com/z2k-retro/86Box.git}"
BRANCH="${BRANCH:-powervr-pcx}"

configure_and_build() {
	# NEW_DYNAREC: the old recompiler stops with "Over limit!" under Windows 98 Setup.
	cmake -S "$SRC" -B "$SRC/build" --preset regular -D NEW_DYNAREC=ON
	cmake --build "$SRC/build"
	if [ ! -d "$SRC/build/src/roms" ]; then
		git clone --depth 1 https://github.com/86Box/roms.git "$SRC/build/src/roms"
	fi
	echo "Built: $SRC/build/src/86Box"
}

case "$1" in
	deps)
		sudo apt update
		sudo apt install -y build-essential cmake ninja-build git libfreetype-dev libgles2-mesa-dev \
			libpng-dev librtmidi-dev libopenal-dev libslirp-dev libfluidsynth-dev libvdeplug-dev \
			libserialport-dev libzstd-dev qtbase5-dev qtbase5-private-dev qttools5-dev \
			qttranslations5-l10n libevdev-dev libxkbcommon-x11-dev
		sudo apt install -y libsdl3-dev || sudo apt install -y libsdl2-dev
		;;
	build)
		[ -d "$SRC/.git" ] || git clone --branch "$BRANCH" "$REPO" "$SRC"
		configure_and_build
		;;
	update)
		[ -d "$SRC/.git" ] || { echo "Run '$0 build' first"; exit 1; }
		git -C "$SRC" fetch "$REPO" "$BRANCH"
		git -C "$SRC" checkout -B "$BRANCH" FETCH_HEAD
		git -C "$SRC" log --oneline -5
		configure_and_build
		;;
	run)
		mkdir -p "$VMS"
		exec "$SRC/build/src/86Box" -P "$VMS"
		;;
	*) sed -n '2,11p' "$0"; exit 1;;
esac
