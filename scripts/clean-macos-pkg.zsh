#!/usr/bin/env zsh

emulate -L zsh
setopt ERR_EXIT NO_UNSET PIPE_FAIL

if (( $# != 1 )) || [[ ! -f ${1} ]]; then
  print -u2 "Usage: ${0:t} <unsigned-package.pkg>"
  exit 2
fi

readonly package_path=${1:A}
readonly original_payload=$(/usr/sbin/pkgutil --payload-files ${package_path})

if ! print -r -- ${original_payload} | /usr/bin/grep -Eq '/\._|/\.DS_Store'; then
  exit 0
fi

readonly work_dir=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/smart-chroma-key-pkg.XXXXXX")
cleanup() {
  /bin/rm -rf -- ${work_dir}
}
trap cleanup EXIT HUP INT TERM

/usr/sbin/pkgutil --expand ${package_path} "${work_dir}/expanded"
readonly components=("${work_dir}"/expanded/*.pkg(/N))
if (( ${#components} != 1 )); then
  print -u2 "Expected one component package, found ${#components}."
  exit 2
fi

readonly component_dir=${components[1]}
/bin/mkdir "${work_dir}/payload"
(
  cd "${work_dir}/payload"
  /usr/bin/gzip -dc "${component_dir}/Payload" | /usr/bin/cpio -idm
  /usr/bin/find . \( -name '._*' -o -name '.DS_Store' \) -delete
  COPYFILE_DISABLE=1 /usr/bin/find . -print | COPYFILE_DISABLE=1 /usr/bin/cpio -o --format odc |
    /usr/bin/gzip -c >"${component_dir}/Payload.clean"
)

/bin/mv "${component_dir}/Payload.clean" "${component_dir}/Payload"
/usr/bin/mkbom "${work_dir}/payload" "${component_dir}/Bom"
/usr/sbin/pkgutil --flatten "${work_dir}/expanded" "${work_dir}/clean.pkg"
/bin/mv "${work_dir}/clean.pkg" ${package_path}

readonly cleaned_payload=$(/usr/sbin/pkgutil --payload-files ${package_path})
if print -r -- ${cleaned_payload} | /usr/bin/grep -Eq '/\._|/\.DS_Store'; then
  print -u2 'Could not remove metadata from installer payload.'
  exit 2
fi

print 'Removed macOS metadata from installer payload.'
