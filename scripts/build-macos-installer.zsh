#!/usr/bin/env zsh
# Builds the universal macOS installer package.
#
# Optional environment for public distribution:
#   CODESIGN_IDENT            "Developer ID Application: Name (TEAMID)" for the plugin
#   CODESIGN_TEAM             Apple developer team ID
#   CODESIGN_IDENT_INSTALLER  "Developer ID Installer: Name (TEAMID)" for the package
#   NOTARIZATION_PROFILE      notarytool keychain profile (xcrun notarytool store-credentials)
# Without them the package is unsigned and suitable for local testing only.

emulate -L zsh
setopt ERR_EXIT NO_UNSET PIPE_FAIL

readonly script_dir=${0:A:h}
readonly project_root=${script_dir:h}
readonly configuration=${1:-Release}

fail() {
  print -u2 -- "error: $*"
  exit 2
}

if [[ ${OSTYPE} != darwin* ]]; then
  fail 'This script must be run on macOS.'
fi

case ${configuration} in
  Debug|RelWithDebInfo|Release|MinSizeRel) ;;
  *) fail "Unsupported configuration: ${configuration}" ;;
esac

require_identity() {
  local identity=$1 purpose=$2
  if ! /usr/bin/security find-identity -v -p basic | /usr/bin/grep -Fq -- "\"${identity}\""; then
    fail "${purpose} identity not found in the keychain: ${identity}"
  fi
}

readonly app_identity=${CODESIGN_IDENT:-}
readonly installer_identity=${CODESIGN_IDENT_INSTALLER:-}
readonly notarization_profile=${NOTARIZATION_PROFILE:-}

# Validate credentials before spending time on a build.
if [[ -n ${app_identity} && ${app_identity} != '-' ]]; then
  require_identity ${app_identity} 'Code signing'
fi
if [[ -n ${installer_identity} && ${installer_identity} != '-' ]]; then
  [[ ${installer_identity} == 'Developer ID Installer:'* ]] ||
    fail "CODESIGN_IDENT_INSTALLER must be a 'Developer ID Installer' identity."
  require_identity ${installer_identity} 'Installer signing'
fi
if [[ -n ${notarization_profile} ]]; then
  [[ ${app_identity} == 'Developer ID Application:'* ]] ||
    fail "Notarization requires CODESIGN_IDENT='Developer ID Application: …'."
  [[ -n ${installer_identity} && ${installer_identity} != '-' ]] ||
    fail 'Notarization requires CODESIGN_IDENT_INSTALLER.'
  /usr/bin/xcrun notarytool history --keychain-profile ${notarization_profile} >/dev/null 2>&1 ||
    fail "notarytool profile '${notarization_profile}' is missing or invalid."
fi

cd ${project_root}

readonly version=$(jq -r '.version' buildspec.json)
readonly name=$(jq -r '.name' buildspec.json)
readonly installer="${project_root}/release/${name}-${version}-macos-universal.pkg"
readonly unsigned_installer="${project_root}/release/${configuration}/${name}.pkg"
readonly plugin_bundle="${project_root}/build_macos/${configuration}/${name}.plugin"

cmake --preset macos
cmake --build build_macos --config ${configuration}
cmake -E remove_directory "${project_root}/release/${configuration}/${name}.plugin"
cmake -E rm -f ${unsigned_installer} ${installer} "${installer}.sha256"
cmake --install build_macos --config ${configuration} --prefix "${project_root}/release/${configuration}"

/usr/bin/codesign --verify --strict --deep ${plugin_bundle} || fail 'Plugin code signature is invalid.'
readonly architectures=$(/usr/bin/lipo -archs "${plugin_bundle}/Contents/MacOS/${name}")
if [[ ${architectures} != *arm64* || ${architectures} != *x86_64* ]]; then
  fail "Plugin is not universal: ${architectures}"
fi

"${script_dir}/clean-macos-pkg.zsh" ${unsigned_installer}

if [[ -n ${installer_identity} && ${installer_identity} != '-' ]]; then
  /usr/bin/productsign --timestamp --sign ${installer_identity} ${unsigned_installer} ${installer}
else
  cmake -E copy ${unsigned_installer} ${installer}
fi
[[ -s ${installer} ]] || fail "Installer was not created: ${installer}"

# Inspect the finished package exactly as users will receive it.
readonly work_dir=$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/${name}-verify.XXXXXX")
trap '/bin/rm -rf -- ${work_dir}' EXIT HUP INT TERM
/usr/sbin/pkgutil --expand ${installer} "${work_dir}/expanded"

readonly payload=$(/usr/sbin/pkgutil --payload-files ${installer})
if [[ ${payload} != *"${name}.plugin/Contents/MacOS/${name}"* || ${payload} != *"${name}.effect"* ]]; then
  fail 'Installer payload is incomplete.'
fi
if print -r -- ${payload} | /usr/bin/grep -Eq '/\._|/\.DS_Store|\.dSYM'; then
  fail 'Unexpected metadata or debug symbols remain in installer payload.'
fi

readonly package_info="${work_dir}/expanded/${name}.pkg/PackageInfo"
[[ -f ${package_info} ]] || fail 'Component PackageInfo is missing.'
if /usr/bin/grep -q '<relocate>' ${package_info}; then
  fail 'Plugin bundle is relocatable; Installer could update a copy outside OBS.'
fi
[[ -x "${work_dir}/expanded/${name}.pkg/Scripts/preinstall" ]] || fail 'Upgrade preinstall script is missing.'

for resource in background.tiff background-dark.tiff LICENSE.txt \
                {,en.lproj/,fa.lproj/}{welcome.html,readme.html,conclusion.html} \
                {en,fa}.lproj/Localizable.strings; do
  [[ -s "${work_dir}/expanded/Resources/${resource}" ]] || fail "Installer resource is missing: ${resource}"
done
for strings in "${work_dir}"/expanded/Resources/**/Localizable.strings(.N); do
  /usr/bin/plutil -lint -s ${strings} || fail "Invalid strings file: ${strings}"
done
if /usr/bin/grep -rlq --include='*.html' --include='*.strings' '@[A-Z_][A-Z_]*@' "${work_dir}/expanded/Resources" ||
   /usr/bin/grep -q '@[A-Z_][A-Z_]*@' "${work_dir}/expanded/Distribution"; then
  fail 'Unconfigured @VARIABLE@ placeholders remain in installer resources.'
fi
/usr/bin/xmllint --noout "${work_dir}/expanded/Distribution" || fail 'Distribution XML is invalid.'

if [[ -n ${notarization_profile} ]]; then
  print 'Submitting to Apple notarization service…'
  readonly submission="${work_dir}/notarization.json"
  /usr/bin/xcrun notarytool submit ${installer} --keychain-profile ${notarization_profile} --wait \
    --output-format json >${submission}
  readonly notary_status=$(jq -r '.status' ${submission})
  if [[ ${notary_status} != Accepted ]]; then
    /usr/bin/xcrun notarytool log "$(jq -r '.id' ${submission})" --keychain-profile ${notarization_profile} || true
    fail "Notarization finished with status: ${notary_status}"
  fi
  /usr/bin/xcrun stapler staple ${installer}
  /usr/bin/xcrun stapler validate ${installer}
  /usr/sbin/spctl --assess --type install -vv ${installer}
fi

(cd ${installer:h} && /usr/bin/shasum -a 256 ${installer:t} >"${installer:t}.sha256")

print "Created ${installer}"
print "Architectures: ${architectures}"
print "SHA-256: $(cut -d' ' -f1 "${installer}.sha256")"
if [[ -z ${installer_identity} || ${installer_identity} == '-' ]]; then
  print 'Package is unsigned: for local testing only. Set CODESIGN_IDENT_INSTALLER and NOTARIZATION_PROFILE for public release.'
fi
/usr/sbin/pkgutil --check-signature ${installer} || true
