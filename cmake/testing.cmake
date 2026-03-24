function(linbox_add_unit_test)
  set(options)
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs LIBRARIES)
  cmake_parse_arguments(LINBOX "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT LINBOX_NAME OR NOT LINBOX_SOURCE)
    message(FATAL_ERROR "linbox_add_unit_test(NAME <name> SOURCE <path>) is required")
  endif()

  add_executable(${LINBOX_NAME} ${LINBOX_SOURCE})
  if(DEFINED LINBOX_CRITERION_INCLUDE_DIR)
    target_include_directories(${LINBOX_NAME} PRIVATE ${LINBOX_CRITERION_INCLUDE_DIR})
  endif()
  target_link_libraries(${LINBOX_NAME} PRIVATE criterion ${LINBOX_LIBRARIES})
  add_test(NAME ${LINBOX_NAME} COMMAND ${CMAKE_COMMAND} -E env LINBOX_DISABLE_SECCOMP=1 $<TARGET_FILE:${LINBOX_NAME}>)
endfunction()

function(linbox_add_preload_test)
  set(options)
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs LIBRARIES)
  cmake_parse_arguments(LINBOX "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT LINBOX_NAME OR NOT LINBOX_SOURCE)
    message(FATAL_ERROR "linbox_add_preload_test(NAME <name> SOURCE <path>) is required")
  endif()

  add_executable(${LINBOX_NAME} ${LINBOX_SOURCE})
  if(LINBOX_LIBRARIES)
    target_link_libraries(${LINBOX_NAME} PRIVATE ${LINBOX_LIBRARIES})
  endif()
  add_test(
    NAME ${LINBOX_NAME}
    COMMAND ${CMAKE_COMMAND} -E env LINBOX_DISABLE_SECCOMP=1 LD_PRELOAD=$<TARGET_FILE:linbox> $<TARGET_FILE:${LINBOX_NAME}>
  )
endfunction()
