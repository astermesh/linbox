cmake_minimum_required(VERSION 3.24)
project(linbox LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release ASAN)

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g0 -DNDEBUG")
set(CMAKE_C_FLAGS_ASAN "-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer")
set(CMAKE_EXE_LINKER_FLAGS_ASAN "-fsanitize=address,undefined")
set(CMAKE_SHARED_LINKER_FLAGS_ASAN "-fsanitize=address,undefined")

include(CTest)
include(FetchContent)
include(cmake/testing.cmake)

FetchContent_Declare(
  criterion
  GIT_REPOSITORY https://github.com/Snaipe/Criterion.git
  GIT_TAG v2.4.2
)
set(CRITERION_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(CRITERION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CRITERION_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(criterion)
set(LINBOX_CRITERION_INCLUDE_DIR "${criterion_SOURCE_DIR}/include")

add_library(linbox-common STATIC
  src/common/sbp.c
  src/common/shm.c
)

target_compile_definitions(linbox-common PRIVATE _GNU_SOURCE)
target_include_directories(linbox-common PUBLIC src)
target_link_libraries(linbox-common PRIVATE rt)

add_library(linbox SHARED
  src/shim/linbox.c
  src/shim/time.c
  src/shim/prng.c
  src/shim/random.c
)

set_target_properties(linbox PROPERTIES
  OUTPUT_NAME linbox
)

target_compile_definitions(linbox PRIVATE _GNU_SOURCE)
target_include_directories(linbox PUBLIC src)
target_link_libraries(linbox PRIVATE dl linbox-common)

add_executable(linbox-controller
  src/controller/main.c
  src/controller/time-manager.c
)

target_compile_definitions(linbox-controller PRIVATE _GNU_SOURCE)
target_include_directories(linbox-controller PRIVATE src)
target_link_libraries(linbox-controller PRIVATE linbox-common)

linbox_add_unit_test(NAME linbox_shim_unit SOURCE src/shim/linbox.test.c LIBRARIES linbox)
linbox_add_unit_test(NAME linbox_shim_prng_unit SOURCE src/shim/prng.test.c LIBRARIES linbox)
linbox_add_preload_test(NAME linbox_shim_preload SOURCE src/shim/linbox.preload.c)
linbox_add_preload_test(NAME linbox_shim_random_preload SOURCE src/shim/random.preload.c)
linbox_add_unit_test(NAME linbox_common_sbp_unit SOURCE src/common/sbp.test.c LIBRARIES linbox-common)
linbox_add_unit_test(NAME linbox_common_shm_unit SOURCE src/common/shm.test.c LIBRARIES linbox-common pthread)
