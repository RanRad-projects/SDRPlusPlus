cmake_minimum_required(VERSION 3.13)
project(audio_source)

file(GLOB SRC "src/*.cpp")

add_library(audio_source SHARED ${SRC})
target_link_libraries(audio_source PRIVATE sdrpp_core)
set_target_properties(audio_source PROPERTIES PREFIX "")

target_include_directories(audio_source PRIVATE "src/")

if (MSVC)
    target_compile_options(audio_source PRIVATE /O2 /Ob2 /std:c++17 /EHsc)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(audio_source PRIVATE -O3 -std=c++17 -Wno-unused-command-line-argument -undefined dynamic_lookup)
else ()
    target_compile_options(audio_source PRIVATE -O3 -std=c++17)
endif ()

if (MSVC)
    # Lib path
    target_link_directories(audio_source PRIVATE "C:/Program Files (x86)/RtAudio/lib")

    # Misc headers
    target_include_directories(audio_source PRIVATE "C:/Program Files (x86)/RtAudio/include/rtaudio")

    target_link_libraries(audio_source PRIVATE rtaudio)
else (MSVC)
    find_package(PkgConfig)

    pkg_check_modules(RTAUDIO REQUIRED rtaudio)

    target_include_directories(audio_source PRIVATE ${RTAUDIO_INCLUDE_DIRS})
    target_link_directories(audio_source PRIVATE ${RTAUDIO_LIBRARY_DIRS})
    target_link_libraries(audio_source PRIVATE ${RTAUDIO_LIBRARIES})

endif ()

# Install directives
install(TARGETS audio_source DESTINATION lib/sdrpp/plugins)