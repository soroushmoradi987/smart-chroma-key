#!/usr/bin/env zsh

set -euo pipefail

project_dir=${0:A:h:h}
test_binary="${TMPDIR:-/tmp}/smart-chroma-key-semantic-test"

if [[ ${OSTYPE} != darwin* ]]; then
  print -u2 'Apple Vision regression test requires macOS.'
  exit 2
fi

clang -fobjc-arc -Wall -Wextra -Werror \
  -mmacosx-version-min=12.0 \
  -I"$project_dir/src" \
  "$project_dir/tests/semantic-mask-macos-test.m" \
  "$project_dir/src/semantic-mask-macos.m" \
  -framework CoreGraphics -framework CoreVideo -framework Foundation -framework ImageIO -framework Vision \
  -o "$test_binary"

"$test_binary"
