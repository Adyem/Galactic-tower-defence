cmake_minimum_required(VERSION 3.22)
project(TowerAscend VERSION 0.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Keep local runtime binaries easy to find for playtesting and headless tools.
# The build tree still contains all generated CMake/Ninja files, while every
# executable is emitted at the project root.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

find_package(SDL2 2.0.20 CONFIG QUIET)
if (TARGET SDL2::SDL2)
    set(TA_SDL_TARGET SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.20)
    set(TA_SDL_TARGET PkgConfig::SDL2)
endif()

function(ta_enable_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()

add_library(ta_core STATIC src/game.cpp src/profile.cpp src/content.cpp src/daily.cpp src/input.cpp)
target_include_directories(ta_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
ta_enable_warnings(ta_core)
target_compile_definitions(ta_core PUBLIC TA_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/content")

add_executable(tower_ascend src/main.cpp)
target_link_libraries(tower_ascend PRIVATE ta_core)
if (TARGET SDL2::SDL2main)
    target_link_libraries(tower_ascend PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(tower_ascend PRIVATE ${TA_SDL_TARGET})
ta_enable_warnings(tower_ascend)
if (WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET tower_ascend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:tower_ascend>"
        VERBATIM)
endif()

add_executable(ta_sim_tests tests/test_sim.cpp)
target_link_libraries(ta_sim_tests PRIVATE ta_core)
ta_enable_warnings(ta_sim_tests)
target_compile_definitions(ta_sim_tests PRIVATE TA_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/content")

add_executable(ta_ui_layout_tests tests/test_ui_layout.cpp)
target_include_directories(ta_ui_layout_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
ta_enable_warnings(ta_ui_layout_tests)

add_executable(ta_content_check tools/content_check.cpp)
target_link_libraries(ta_content_check PRIVATE ta_core)
ta_enable_warnings(ta_content_check)

add_executable(ta_replay_check tools/replay_check.cpp)
target_link_libraries(ta_replay_check PRIVATE ta_core)
ta_enable_warnings(ta_replay_check)

add_executable(ta_balance_check tools/balance_check.cpp)
target_link_libraries(ta_balance_check PRIVATE ta_core)
ta_enable_warnings(ta_balance_check)

# Names used by the build plan are kept as aliases so downstream tooling can
# depend on the product vocabulary without duplicating executables.
add_executable(ta_client ALIAS tower_ascend)
add_executable(ta_tests ALIAS ta_sim_tests)
add_executable(ta_sim_runner ALIAS ta_balance_check)

enable_testing()
add_test(NAME ta_sim_tests COMMAND ta_sim_tests)
add_test(NAME ta_ui_layout_tests COMMAND ta_ui_layout_tests)
add_test(NAME ta_content_check COMMAND ta_content_check ${CMAKE_CURRENT_SOURCE_DIR}/assets/content)
add_test(NAME ta_content_failure_smoke COMMAND ta_content_check ${CMAKE_CURRENT_BINARY_DIR}/tower_ascend_missing_content_pack)
set_tests_properties(ta_content_failure_smoke PROPERTIES WILL_FAIL TRUE)
add_test(NAME ta_replay_tool_smoke COMMAND ta_replay_check --self-test)
set(TA_HEADLESS_REPLAY ${CMAKE_CURRENT_BINARY_DIR}/tower_ascend_headless.replay)
add_test(NAME ta_headless_replay_record COMMAND tower_ascend --headless --record-replay ${TA_HEADLESS_REPLAY})
set_tests_properties(ta_headless_replay_record PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME ta_headless_replay_verify COMMAND ta_replay_check ${TA_HEADLESS_REPLAY} 100000)
set_tests_properties(ta_headless_replay_verify PROPERTIES DEPENDS ta_headless_replay_record)
add_test(NAME ta_balance_smoke COMMAND ta_balance_check 1)
set_tests_properties(ta_balance_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME ta_runtime_smoke COMMAND tower_ascend --headless)
set_tests_properties(ta_runtime_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME ta_render_smoke COMMAND tower_ascend --render-smoke)
set_tests_properties(ta_render_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

install(TARGETS tower_ascend ta_replay_check ta_balance_check ta_content_check RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY assets/content DESTINATION ${CMAKE_INSTALL_DATADIR}/tower_ascend)
install(FILES assets/manifest.json README.md TOWER_ASCEND_BUILD_PLAN.md THIRD_PARTY_NOTICES.md third_party/manifest.json DESTINATION ${CMAKE_INSTALL_DATADIR}/tower_ascend)
