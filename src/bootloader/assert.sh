#!/usr/bin/env bash
set -x # show cmds
set -e # fail globally

# Know where we at :p
SCRIPT=$(realpath "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
cd "${SCRIPTPATH}"

# The Limine version this project is developed against.
#
# Limine only publishes its prebuilt BIOS/UEFI blobs on the floating
# "v10.x-binary" branch: release tags and release tarballs are source-only and
# would need a full cross-toolchain to produce those blobs. The artifact itself
# therefore cannot be pinned, only checked - so we ask the binary what it
# actually is and record that, instead of writing down whatever we hoped for.
EXPECTED_VERSION="10.8.5"

# Ask the binary itself rather than trusting a string this script wrote.
limineVersion() {
	if [ -x "limine/limine" ]; then
		./limine/limine --version 2>/dev/null | head -1 | awk '{print $2}'
	fi
}

fetchLimine() {
	rm -rf limine/
	git clone https://github.com/limine-bootloader/limine.git --branch=v10.x-binary --depth=1
	cd limine/
	# Upstream limine is a GNU make project, so GNU make is used here on purpose.
	make
	cd ../
}

installed=$(limineVersion)

if [ -z "$installed" ]; then
	fetchLimine
	installed=$(limineVersion)
fi

if [ -z "$installed" ]; then
	echo "Could not determine the installed Limine version!" >&2
	exit 1
fi

# Record what actually got installed, not what we expected.
echo "$installed" >.version

# Not fatal: upstream ships new v10.x releases often, and blocking every build
# on one would be worse than telling you about it.
if [ "$installed" != "$EXPECTED_VERSION" ]; then
	echo "" >&2
	echo "NOTE: expected Limine $EXPECTED_VERSION, but v10.x-binary provided $installed." >&2
	echo "      Verify $installed still boots, then bump EXPECTED_VERSION; otherwise" >&2
	echo "      set EXPECTED_VERSION back to a version you have tested." >&2
	echo "      Installed version recorded in src/bootloader/.version" >&2
fi