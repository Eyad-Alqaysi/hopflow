#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
# SPDX-License-Identifier: MIT
#
# Installs the Hopflow virtual microphone so apps can record "Hopflow Microphone".
# Usage: install-hopflow-mic.sh [path/to/HopflowMic.driver]
# Without a path it uses the driver shipped in Hopflow.app/Contents/PlugIns, found from this
# script's place in Hopflow.app/Contents/Resources.

set -eu

driver="${1:-$(cd "$(dirname "$0")/.." && pwd)/PlugIns/HopflowMic.driver}"
destination="/Library/Audio/Plug-Ins/HAL"

if [ ! -f "$driver/Contents/Info.plist" ]; then
  echo "No driver at $driver" >&2
  echo "Pass the path to HopflowMic.driver, for example build/lib/HopflowMic.driver" >&2
  exit 1
fi

echo "Installing $driver into $destination (needs your password)"
sudo mkdir -p "$destination"
sudo rm -rf "$destination/HopflowMic.driver"
sudo cp -R "$driver" "$destination/"
sudo chown -R root:wheel "$destination/HopflowMic.driver"

# Core Audio loads drivers when it starts; sound stops for a moment while it restarts
sudo killall coreaudiod

echo "Done: choose \"Hopflow Microphone\" as the microphone in your apps"
