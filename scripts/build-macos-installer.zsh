#!/usr/bin/env zsh

emulate -L zsh
setopt ERR_EXIT NO_UNSET PIPE_FAIL

readonly script_dir=${0:A:h}
readonly project_root=${script_dir:h}
readonly configuration=${1:-Release}

if [[ ${OSTYPE} != darwin* ]]; then
  print -u2 'This script must be run on macOS.'
  exit 2
fi

case ${configuration} in
  Debug|RelWithDebInfo|Release|MinSizeRel) ;;
  *)
    print -u2 "Unsupported configuration: ${configuration}"
    exit 2
    ;;
esac

cd ${project_root}

cmake --preset macos
cmake --build build_macos --config ${configuration}
cmake -E remove_directory "${project_root}/release/${configuration}/smart-chroma-key.plugin"
cmake -E rm -f "${project_root}/release/${configuration}/smart-chroma-key.pkg"
cmake --install build_macos --config ${configuration} --prefix "${project_root}/release/${configuration}"

readonly version=$(jq -r '.version' buildspec.json)
readonly installer="${project_root}/release/smart-chroma-key-${version}-macos-universal.pkg"
readonly unsigned_installer="${project_root}/release/${configuration}/smart-chroma-key.pkg"

"${script_dir}/clean-macos-pkg.zsh" ${unsigned_installer}

if [[ -n ${CODESIGN_IDENT_INSTALLER:-} && ${CODESIGN_IDENT_INSTALLER} != '-' ]]; then
  /usr/bin/productsign --sign ${CODESIGN_IDENT_INSTALLER} ${unsigned_installer} ${installer}
else
  cmake -E copy_if_different ${unsigned_installer} ${installer}
fi

if [[ ! -s ${installer} ]]; then
  print -u2 "Installer was not created: ${installer}"
  exit 2
fi

readonly payload=$(/usr/sbin/pkgutil --payload-files ${installer})
if [[ ${payload} != *'smart-chroma-key.plugin/Contents/MacOS/smart-chroma-key'* ||
      ${payload} != *'smart-chroma-key.effect'* ]]; then
  print -u2 'Installer payload is incomplete.'
  exit 2
fi

readonly plugin_binary="${project_root}/build_macos/${configuration}/smart-chroma-key.plugin/Contents/MacOS/smart-chroma-key"
readonly architectures=$(/usr/bin/lipo -archs ${plugin_binary})
if [[ ${architectures} != *arm64* || ${architectures} != *x86_64* ]]; then
  print -u2 "Plugin is not universal: ${architectures}"
  exit 2
fi

if print -r -- ${payload} | /usr/bin/grep -Eq '/\._|/\.DS_Store|\.dSYM'; then
  print -u2 'Unexpected metadata or debug symbols remain in installer payload.'
  exit 2
fi

print "Created ${installer}"
print "Architectures: ${architectures}"
/usr/sbin/pkgutil --check-signature ${installer} || true
