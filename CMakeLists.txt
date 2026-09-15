cmake_minimum_required(VERSION 3.16)
project(xiaoai_plus LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SHERPA_ONNX_STATIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/assets/sherpa_onnx_static" CACHE PATH "Path to sherpa_onnx static package root")

include(FetchContent)

# Force ZLIB and OpenSSL to resolve to static .a BEFORE FetchContent
# so that ixwebsocket picks up the static versions.
find_library(ZLIB_STATIC_LIB NAMES libz.a HINTS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)
find_library(SSL_STATIC_LIB NAMES libssl.a HINTS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)
find_library(CRYPTO_STATIC_LIB NAMES libcrypto.a HINTS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)
if(ZLIB_STATIC_LIB)
  set(ZLIB_LIBRARY ${ZLIB_STATIC_LIB} CACHE FILEPATH "" FORCE)
endif()
if(SSL_STATIC_LIB)
  set(OPENSSL_SSL_LIBRARY ${SSL_STATIC_LIB} CACHE FILEPATH "" FORCE)
endif()
if(CRYPTO_STATIC_LIB)
  set(OPENSSL_CRYPTO_LIBRARY ${CRYPTO_STATIC_LIB} CACHE FILEPATH "" FORCE)
endif()

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
)
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.1
)
FetchContent_MakeAvailable(spdlog)

find_package(Threads REQUIRED)

# --- sherpa-onnx static libraries ---
find_path(SHERPA_ONNX_INCLUDE_DIR
  NAMES sherpa-onnx/c-api/cxx-api.h
  HINTS ${SHERPA_ONNX_STATIC_ROOT}
  PATH_SUFFIXES include
  NO_CMAKE_FIND_ROOT_PATH)

set(_sherpa_static_libs
  sherpa-onnx-cxx-api
  sherpa-onnx-c-api
  sherpa-onnx-core
  kaldi-decoder-core
  sherpa-onnx-kaldifst-core
  sherpa-onnx-fstfar
  sherpa-onnx-fst
  kaldi-native-fbank-core
  kissfft-float
  onnxruntime
  ssentencepiece_core
)
set(SHERPA_ONNX_STATIC_LIBRARIES "")
foreach(_lib ${_sherpa_static_libs})
  find_library(_found_${_lib}
    NAMES ${_lib}
    HINTS ${SHERPA_ONNX_STATIC_ROOT}
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH)
  if(NOT _found_${_lib})
    message(FATAL_ERROR "Static lib not found: ${_lib} in ${SHERPA_ONNX_STATIC_ROOT}/lib")
  endif()
  list(APPEND SHERPA_ONNX_STATIC_LIBRARIES ${_found_${_lib}})
endforeach()

# --- webrtc static library ---
find_library(WEBRTC_STATIC_LIB NAMES libwebrtc_audio_processing.a HINTS ${CMAKE_SYSROOT}/usr/lib NO_DEFAULT_PATH)

if(NOT SHERPA_ONNX_INCLUDE_DIR)
  message(FATAL_ERROR "sherpa-onnx headers not found. Set SHERPA_ONNX_STATIC_ROOT")
endif()
if(NOT ZLIB_STATIC_LIB)
  message(FATAL_ERROR "libz.a not found in sysroot")
endif()
if(NOT WEBRTC_STATIC_LIB)
  message(FATAL_ERROR "libwebrtc_audio_processing.a not found in sysroot")
endif()

find_path(ZLIB_INCLUDE_DIR NAMES zlib.h HINTS ${CMAKE_SYSROOT}/usr/include)
find_path(WEBRTC_INCLUDE_DIR NAMES webrtc/modules/audio_processing/include/audio_processing.h
  HINTS ${CMAKE_SYSROOT}/usr/include/webrtc_audio_processing ${CMAKE_SYSROOT}/usr/include)

# --- core library ---
add_library(xiaoai_plus_core
  src/app/app.cpp
  src/audio/recorder_arecord.cpp
  src/audio/player_aplay.cpp
  src/config/config.cpp
  src/audio/aec_webrtc.cpp
  src/realtime/client.cpp
  src/wakeup/gate.cpp
  src/wakeup/trigger.cpp
  src/wakeup/local_listener.cpp
  src/wakeup/kws_zipformer.cpp
)

target_include_directories(xiaoai_plus_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SHERPA_ONNX_INCLUDE_DIR}
  ${ZLIB_INCLUDE_DIR}
)
if(WEBRTC_INCLUDE_DIR)
  target_include_directories(xiaoai_plus_core PUBLIC ${WEBRTC_INCLUDE_DIR})
endif()
target_compile_definitions(xiaoai_plus_core PUBLIC WEBRTC_POSIX WEBRTC_AUDIO_PROCESSING_ONLY_BUILD)

target_link_libraries(xiaoai_plus_core
  PUBLIC
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket
    spdlog::spdlog
    ${SHERPA_ONNX_STATIC_LIBRARIES}
    ${WEBRTC_STATIC_LIB}
    ${ZLIB_STATIC_LIB}
    ${SSL_STATIC_LIB}
    ${CRYPTO_STATIC_LIB}
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# --- main executable ---
add_executable(xiaoai_plus_speaker src/main.cpp)
target_link_libraries(xiaoai_plus_speaker PRIVATE xiaoai_plus_core)

# Fully static binary so it runs on older glibc devices (device has glibc 2.25)
if(CMAKE_CROSSCOMPILING)
  target_link_options(xiaoai_plus_speaker PRIVATE -static)
endif()
