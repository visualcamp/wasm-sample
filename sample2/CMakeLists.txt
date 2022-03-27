cmake_minimum_required(VERSION 3.5)
project(WasmSample)

set(CMAKE_CXX_STANDARD 17)
set(SAMPLE_SRC_DIR ${CMAKE_SOURCE_DIR}/include)

set(EMSDK_FLAGS
        " -pthread -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4 -s WASM=1 \
        -s MODULARIZE=1 \
        -s EXPORT_NAME='\"createModule\"' \
        -s ALLOW_TABLE_GROWTH \
        -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\", \"addFunction\"]' \
        -s EXPORTED_FUNCTIONS='[\"_findFace\", \"_setFaceCallback\"]' ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EMSDK_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EMSDK_FLAGS}")

add_subdirectory(tflite)
add_subdirectory(opencv)
add_subdirectory(vccc)

add_executable(WasmSample
    ${SAMPLE_SRC_DIR}/main.cpp
    ${SAMPLE_SRC_DIR}/blaze_face_wrapper.cpp
    ${SAMPLE_SRC_DIR}/cutemodel/cute_model.cpp
    ${SAMPLE_SRC_DIR}/model/model_reader.cpp)

target_include_directories(WasmSample PUBLIC ${SAMPLE_SRC_DIR})
target_link_libraries(WasmSample tflite opencv vccc)
