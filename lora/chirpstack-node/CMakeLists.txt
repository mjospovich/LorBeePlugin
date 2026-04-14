cmake_minimum_required(VERSION 3.18)
project(chirpstack_lora_node CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED RADIOLIB_ROOT)
  set(RADIOLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/RadioLib")
endif()

if(NOT EXISTS "${RADIOLIB_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR
    "RadioLib not found at ${RADIOLIB_ROOT}.\n"
    "  Docker: built automatically.\n"
    "  Host: run ./scripts/clone-radiolib.sh from this directory.")
endif()

add_subdirectory(${RADIOLIB_ROOT} ${CMAKE_BINARY_DIR}/radiolib)

find_package(yaml-cpp REQUIRED)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(lorawan-node main.cpp config_yaml.cpp snapshot.cpp payload_builder.cpp lora_state.cpp downlink_handler.cpp)
target_link_libraries(lorawan-node PRIVATE RadioLib lgpio pthread yaml-cpp nlohmann_json::nlohmann_json)
target_compile_options(lorawan-node PRIVATE -Wall -Wextra)
