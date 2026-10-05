cmake_minimum_required(VERSION 3.20)
project(hiccups VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "hiccups requires Linux for CPU affinity and memory locking")
endif()

include(GNUInstallDirs)
option(HICCUPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
find_package(Threads REQUIRED)

add_executable(hiccups src/hiccups.cpp)
target_compile_features(hiccups PRIVATE cxx_std_17)
set_target_properties(hiccups PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(hiccups PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
  target_compile_options(hiccups PRIVATE -Wall -Wextra -Wpedantic)
  if(HICCUPS_WARNINGS_AS_ERRORS)
    target_compile_options(hiccups PRIVATE -Werror)
  endif()
endif()

install(TARGETS hiccups RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  enable_language(C)
  add_library(hiccups_startup_hooks MODULE tests/startup_hooks.c)
  target_link_libraries(hiccups_startup_hooks PRIVATE ${CMAKE_DL_LIBS})

  add_test(NAME startup COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/startup.py $<TARGET_FILE:hiccups>
    --hooks $<TARGET_FILE:hiccups_startup_hooks>)
  set_tests_properties(startup PROPERTIES
    TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS startup)

  add_test(NAME smoke COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.py $<TARGET_FILE:hiccups>)
  set_tests_properties(smoke PROPERTIES TIMEOUT 15 LABELS smoke)
endif()
