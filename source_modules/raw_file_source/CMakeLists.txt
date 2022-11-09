cmake_minimum_required(VERSION 3.13)
project(raw_file_source)

file(GLOB SRC "src/*.cpp")

add_library(raw_file_source SHARED ${SRC})
target_link_libraries(raw_file_source PRIVATE sdrpp_core)
set_target_properties(raw_file_source PROPERTIES PREFIX "")

target_include_directories(raw_file_source PRIVATE "src/")

if (MSVC)
    target_compile_options(raw_file_source PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(raw_file_source PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(raw_file_source PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS raw_file_source DESTINATION lib/sdrpp/plugins)
