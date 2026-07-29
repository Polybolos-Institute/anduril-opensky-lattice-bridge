cmake_minimum_required(VERSION 3.21)
project(opensky_lattice_bridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "This bridge uses WinHTTP (Windows). Match HOTL sandbox TLS path.")
endif()

add_executable(opensky_lattice_bridge
  src/main.cpp
  src/lattice_rest.cpp
  src/opensky_client.cpp
)

target_include_directories(opensky_lattice_bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
  target_compile_options(opensky_lattice_bridge PRIVATE /W4 /permissive-)
  target_compile_definitions(opensky_lattice_bridge PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(opensky_lattice_bridge PRIVATE winhttp)

install(TARGETS opensky_lattice_bridge RUNTIME DESTINATION bin)
