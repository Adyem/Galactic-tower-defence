cmake_minimum_required(VERSION 3.22)
project(TowerAscend VERSION 0.1 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_library(ta_core STATIC src/game.cpp src/profile.cpp src/content.cpp src/daily.cpp)
target_include_directories(ta_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(ta_core PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ta_core PUBLIC TA_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/content")

add_executable(tower_ascend src/main.cpp)
target_link_libraries(tower_ascend PRIVATE ta_core PkgConfig::SDL2)
target_compile_options(tower_ascend PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ta_sim_tests tests/test_sim.cpp)
target_link_libraries(ta_sim_tests PRIVATE ta_core)
target_compile_options(ta_sim_tests PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(ta_sim_tests PRIVATE TA_CONTENT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/content")

add_executable(ta_content_check tools/content_check.cpp)
target_link_libraries(ta_content_check PRIVATE ta_core)
target_compile_options(ta_content_check PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ta_replay_check tools/replay_check.cpp)
target_link_libraries(ta_replay_check PRIVATE ta_core)
target_compile_options(ta_replay_check PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ta_balance_check tools/balance_check.cpp)
target_link_libraries(ta_balance_check PRIVATE ta_core)
target_compile_options(ta_balance_check PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME ta_sim_tests COMMAND ta_sim_tests)
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
install(FILES assets/manifest.json README.md TOWER_ASCEND_BUILD_PLAN.md DESTINATION ${CMAKE_INSTALL_DATADIR}/tower_ascend)
