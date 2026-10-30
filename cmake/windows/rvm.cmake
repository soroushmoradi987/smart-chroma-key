# Pin both the inference runtime and the official RVM model. Installed privately
# under data/runtime, so another OBS plugin's onnxruntime.dll cannot shadow ours.
set(_rvm_deps "${CMAKE_CURRENT_BINARY_DIR}/rvm-deps")
file(MAKE_DIRECTORY "${_rvm_deps}")
function(sck_download url path sha)
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
  endif()
  if(NOT actual STREQUAL sha)
    file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH "SHA256=${sha}" TLS_VERIFY ON)
  endif()
endfunction()
sck_download(
  "https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip"
  "${_rvm_deps}/ort.zip" "0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd")
file(ARCHIVE_EXTRACT INPUT "${_rvm_deps}/ort.zip" DESTINATION "${_rvm_deps}")
set(_ort "${_rvm_deps}/onnxruntime-win-x64-1.23.2")
sck_download(
  "https://github.com/PeterL1n/RobustVideoMatting/releases/download/v1.0.0/rvm_mobilenetv3_fp32.onnx"
  "${_rvm_deps}/rvm.onnx" "88d4531297118f595bf2fd60f6f566aec2e559393802d1f436c380f0cbbd2828")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/rvm-windows.cpp)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${_ort}/include")
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SCK_RVM_EMBEDDED=1)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
install(FILES "${_rvm_deps}/rvm.onnx" DESTINATION "${CMAKE_PROJECT_NAME}/data")
install(FILES "${_ort}/lib/onnxruntime.dll" DESTINATION "${CMAKE_PROJECT_NAME}/data/runtime" RENAME sck-onnxruntime.dll)
install(FILES "${_ort}/LICENSE" DESTINATION "${CMAKE_PROJECT_NAME}/data/runtime" RENAME ONNX-Runtime-LICENSE.txt)
install(FILES "${_ort}/ThirdPartyNotices.txt" DESTINATION "${CMAKE_PROJECT_NAME}/data/runtime")
