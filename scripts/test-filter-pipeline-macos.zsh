#!/usr/bin/env zsh
set -euo pipefail
project_dir=${0:A:h:h}
obs_source="$project_dir/.deps/obs-studio-31.1.1"
obs_opengl="$obs_source/build_universal/libobs-opengl/Release/libobs-opengl.dylib"
obs_deps_lib="$obs_source/.deps/obs-deps-2025-07-11-universal/lib"
plugin="$project_dir/build_macos/Release/smart-chroma-key.plugin/Contents"
test_binary="${TMPDIR:-/tmp}/smart-chroma-key-pipeline-smoke"
if [[ ! -f "$plugin/MacOS/smart-chroma-key" ]]; then
  print -u2 'Build the macOS Release plugin before running this test.'
  exit 2
fi
cc "$project_dir/tests/filter-pipeline-smoke.c" \
  -std=c17 -Wall -Wextra -Werror \
  -I"$obs_source/libobs" -I"$obs_source/build_universal/config" \
  -F"$project_dir/.deps/Frameworks" -framework libobs \
  -Wl,-rpath,"$project_dir/.deps/Frameworks" -Wl,-rpath,"$obs_deps_lib" \
  -o "$test_binary"
"$test_binary" "$obs_opengl" "$plugin/MacOS/smart-chroma-key" "$plugin/Resources"
