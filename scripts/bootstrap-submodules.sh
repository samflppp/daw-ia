#!/usr/bin/env bash
# Initializes the external/ submodules at their pinned commits.
#
# Not recursive on purpose:
#  - tracktion_engine has its own JUCE submodule (modules/juce). We use
#    external/JUCE instead, pinned to the commit Tracktion expects.
#  - vst3sdk pulls vstgui4, doc and tutorials (hundreds of MB). Hosting only
#    needs base, pluginterfaces, public.sdk and cmake.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

git submodule sync
git submodule update --init external/JUCE external/tracktion_engine external/clap external/vst3sdk external/BLAKE3

git -C external/vst3sdk submodule update --init base pluginterfaces public.sdk cmake

echo "Submodules ready:"
git submodule status
