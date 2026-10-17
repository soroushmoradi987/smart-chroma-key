#!/usr/bin/env zsh

set -euo pipefail

project_dir=${0:A:h:h}
obs_opengl="$project_dir/.deps/obs-studio-31.1.1/build_universal/libobs-opengl/Release/libobs-opengl.dylib"
obs_deps_lib="$project_dir/.deps/obs-studio-31.1.1/.deps/obs-deps-2025-07-11-universal/lib"
test_binary="${TMPDIR:-/tmp}/smart-chroma-key-hair-regression"

if [[ ! -f "$obs_opengl" ]]; then
  print -u2 "Missing OBS OpenGL test backend: $obs_opengl"
  exit 2
fi

cc "$project_dir/tests/shader-hair-regression.c" \
  -std=c17 -Wall -Wextra -Werror \
  -I"$project_dir/.deps/obs-studio-31.1.1/libobs" \
  -F"$project_dir/.deps/Frameworks" \
  -framework libobs \
  -Wl,-rpath,"$project_dir/.deps/Frameworks" \
  -Wl,-rpath,"$obs_deps_lib" \
  -o "$test_binary"

"$test_binary" "$obs_opengl" "$project_dir/data/smart-chroma-key.effect"
