#!/usr/bin/env bash
# pcx2pack.sh - pack a pcx2session folder for sending.
#
#   bash pcx2pack.sh pcx2session-DATE
#
# Keeps only what the comparison needs: report.txt, the card's raw output
# (*-card.raw) and the frame dumps. The .ppm pictures are left out, since
# they can be regenerated from those.
#
# Makes pcx2session-DATE.tar.gz, a single file. Some chat apps open .zip
# attachments and count every file inside towards a per-message limit;
# a .tar.gz is sent as one file. If a .zip is needed anyway, set
# ZIP_PARTS=N to make zips of at most N files each instead
# (pcx2session-DATE-part1.zip, -part2.zip, ...), to send one per message.

set -e
DIR="${1:?usage: pcx2pack.sh SESSION_FOLDER}"
DIR="${DIR%/}"
[ -f "$DIR/report.txt" ] || { echo "$DIR/report.txt not found"; exit 1; }
NAME="$(basename "$DIR")"
cd "$(dirname "$DIR")"

mapfile -t FILES < <(cd "$NAME" && ls report.txt *-card.raw frame_*.bin 2>/dev/null)

if [ -z "$ZIP_PARTS" ]; then
	tar -czf "$NAME.tar.gz" -C "$NAME" "${FILES[@]}"
	echo "Please send $(pwd)/$NAME.tar.gz"
	exit 0
fi

# report.txt goes in every part so each one can be read on its own.
rest=("${FILES[@]:1}")
per=$((ZIP_PARTS - 1))
[ "$per" -ge 1 ] || { echo "ZIP_PARTS must be at least 2"; exit 1; }
part=0
for ((i = 0; i < ${#rest[@]}; i += per)); do
	part=$((part + 1))
	(cd "$NAME" && zip -q "../$NAME-part$part.zip" report.txt "${rest[@]:i:per}")
done
echo "Please send these $part files, one per message:"
ls -1 "$(pwd)/$NAME"-part*.zip
