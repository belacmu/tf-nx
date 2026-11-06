#!/bin/bash
# plugins/build.sh <Plugin>: builds plugins/<Plugin>/*.c into plugins/<Plugin>/plugin.elf, a code plugin for the core module
# (hw-probe/exlaunch/source/program/plugins.cpp; API plugin_api.h; docs/DISTRIBUTION-2026-11-04.md "Code plugins").
# Position independent, no libc, no imports (everything comes through the API table), code and data on separate pages.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
name=${1:?usage: plugins/build.sh <Plugin>}
dir="$here/$name"
bin=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin
"$bin/aarch64-none-elf-gcc" -O2 -fPIC -shared -nostdlib -nostartfiles -ffreestanding -fno-builtin -mgeneral-regs-only \
  -fvisibility=hidden -I "$here/../hw-probe/exlaunch/source/program" \
  -Wl,-e,xplay_plugin_main -Wl,-z,max-page-size=0x1000 -Wl,-z,separate-code -Wl,-z,norelro -Wl,--no-undefined \
  -o "$dir/plugin.elf" "$dir"/*.c
"$bin/aarch64-none-elf-readelf" -lW "$dir/plugin.elf" | grep -E 'LOAD|DYNAMIC|Entry'
if "$bin/aarch64-none-elf-nm" -u "$dir/plugin.elf" | grep -q .; then echo "plugins/build.sh: FAILED: $name imports symbols:" >&2; "$bin/aarch64-none-elf-nm" -u "$dir/plugin.elf" >&2; exit 1; fi
echo "plugins/build.sh: OK -> $dir/plugin.elf ($(wc -c < "$dir/plugin.elf") bytes)"
