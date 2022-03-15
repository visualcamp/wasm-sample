project(tflite)

option(TFLITE_WITH_WASM_SIMD OFF)

add_library(tflite INTERFACE)

set(TFLITE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(TFLITE_INCLUDE_PATH "${TFLITE_PATH}/include")
set(TFLITE_LIB_PATH "${TFLITE_PATH}/lib")


if(TFLITE_WITH_WASM_SIMD)
  STRING(APPEND TFLITE_LIB_PATH "/simd")
else()
  STRING(APPEND TFLITE_LIB_PATH "/nonsimd")
endif()

set(lib_tflite
    ${TFLITE_LIB_PATH}/libclog.a
    ${TFLITE_LIB_PATH}/libcpuinfo.a
    ${TFLITE_LIB_PATH}/libfarmhash.a
    ${TFLITE_LIB_PATH}/libfft2d_fftsg.a
    ${TFLITE_LIB_PATH}/libfft2d_fftsg2d.a
    ${TFLITE_LIB_PATH}/libflatbuffers.a
    ${TFLITE_LIB_PATH}/libpthreadpool.a
    ${TFLITE_LIB_PATH}/libruy.a
    ${TFLITE_LIB_PATH}/libtensorflow-lite.a
    ${TFLITE_LIB_PATH}/libXNNPACK.a)

target_link_libraries(tflite INTERFACE ${lib_tflite})
target_include_directories(tflite INTERFACE ${TFLITE_INCLUDE_PATH})
