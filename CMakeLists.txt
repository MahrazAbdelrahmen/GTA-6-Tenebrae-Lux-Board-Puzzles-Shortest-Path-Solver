cmake_minimum_required(VERSION 3.20...3.29)
project(tenebrae LANGUAGES C)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples")
FetchContent_Declare(raylib
    URL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5
    URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
)
FetchContent_MakeAvailable(raylib)

add_executable(tenebrae tenebrae.c puzzle.c)
target_compile_features(tenebrae PRIVATE c_std_99)
target_link_libraries(tenebrae PRIVATE raylib)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(tenebrae PRIVATE _POSIX_C_SOURCE=200809L)
endif()
configure_file(puzzles.txt puzzles.txt COPYONLY)

install(TARGETS tenebrae RUNTIME DESTINATION . COMPONENT game)
install(FILES puzzles.txt DESTINATION . COMPONENT game)
install(FILES "${raylib_SOURCE_DIR}/LICENSE" DESTINATION . RENAME raylib-LICENSE COMPONENT game)
