cmake_minimum_required(VERSION 3.13)
project(signalstrengthgraph)

file(GLOB SRC "src/*.cpp")

add_library(signalstrengthgraph SHARED ${SRC})
target_link_libraries(signalstrengthgraph PRIVATE sdrpp_core)
set_target_properties(signalstrengthgraph PROPERTIES PREFIX "")

target_include_directories(signalstrengthgraph PRIVATE "src/")

if (MSVC)
    target_compile_options(signalstrengthgraph PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(signalstrengthgraph PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(signalstrengthgraph PRIVATE -O3 -std=c++17)
endif ()

# Install directives
install(TARGETS signalstrengthgraph DESTINATION lib/sdrpp/plugins)
