cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c src/semantic-mask.h src/processing-method.c)

# Optional model path keeps ordinary Classic builds (including Windows) independent.
set(SCK_RVM_MODEL "" CACHE FILEPATH "Official RVM 1080p FP16 Core ML model to bundle on macOS")

if(OS_MACOS)
  enable_language(OBJC)
  find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
  find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
  find_library(IMAGEIO_FRAMEWORK ImageIO REQUIRED)
  find_library(VISION_FRAMEWORK Vision REQUIRED)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/semantic-mask-macos.m)
  if(SCK_RVM_MODEL)
    if(NOT EXISTS "${SCK_RVM_MODEL}")
      message(FATAL_ERROR "SCK_RVM_MODEL does not exist: ${SCK_RVM_MODEL}")
    endif()
    find_library(COREML_FRAMEWORK CoreML REQUIRED)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/rvm-preview.m)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SCK_RVM_EMBEDDED=1)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${COREML_FRAMEWORK})
    set(_rvm_dir "${CMAKE_CURRENT_BINARY_DIR}/rvm-model")
    file(MAKE_DIRECTORY "${_rvm_dir}")
    configure_file("${SCK_RVM_MODEL}" "${_rvm_dir}/rvm.mlmodel" COPYONLY)
    execute_process(COMMAND xcrun coremlcompiler compile "${_rvm_dir}/rvm.mlmodel" "${_rvm_dir}"
      COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE _rvm_files "${_rvm_dir}/rvm.mlmodelc/*")
    foreach(_rvm_file IN LISTS _rvm_files)
      file(RELATIVE_PATH _rvm_relative "${_rvm_dir}" "${_rvm_file}")
      get_filename_component(_rvm_parent "${_rvm_relative}" DIRECTORY)
      target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${_rvm_file}")
      set_property(SOURCE "${_rvm_file}" PROPERTY MACOSX_PACKAGE_LOCATION "Resources/${_rvm_parent}")
    endforeach()
  endif()
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      ${COREGRAPHICS_FRAMEWORK}
      ${COREVIDEO_FRAMEWORK}
      ${FOUNDATION_FRAMEWORK}
      ${IMAGEIO_FRAMEWORK}
      ${VISION_FRAMEWORK}
  )
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/semantic-mask-stub.c)
endif()

if(OS_WINDOWS)
  option(SCK_WINDOWS_AI "Bundle the Windows ONNX RVM engine and model" ON)
  if(SCK_WINDOWS_AI)
    include(cmake/windows/rvm.cmake)
  endif()
  option(SCK_BUILD_WINDOWS_TESTS "Build real libobs/Direct3D pipeline tests" OFF)
  if(SCK_BUILD_WINDOWS_TESTS)
    add_executable(sck-pipeline-test tests/filter-pipeline-smoke.c)
    target_link_libraries(sck-pipeline-test PRIVATE OBS::libobs)
    add_executable(sck-windows-test tests/windows-runtime.c)
    target_link_libraries(sck-windows-test PRIVATE OBS::libobs)
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
