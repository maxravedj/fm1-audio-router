#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu
src_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$src_dir/scripts/build_gateway.py" --host
exec python3 "$src_dir/scripts/test_gateway.py" "$@"
