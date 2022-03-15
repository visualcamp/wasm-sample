cmake_minimum_required(VERSION 3.5)
project(WasmSample)

set(CMAKE_CXX_STANDARD 17)
set(SAMPLE_SRC_DIR ${CMAKE_SOURCE_DIR}/include)

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
#set_target_properties(seeso_core PROPERTIES OUTPUT_NAME "seeso")
#set_target_properties(seeso_core PROPERTIES OUTPUT_NAME "seeso")