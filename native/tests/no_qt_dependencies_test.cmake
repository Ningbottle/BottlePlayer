cmake_minimum_required(VERSION 3.24)

if(NOT GUARD_FILE)
  get_filename_component(GUARD_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/NoQtDependencies.cmake" ABSOLUTE)
endif()
if(NOT TEST_BINARY_DIR)
  message(FATAL_ERROR "Pass a disposable TEST_BINARY_DIR for the generated fixtures")
endif()
if(NOT TEST_GENERATOR)
  set(TEST_GENERATOR Ninja)
endif()
file(TO_CMAKE_PATH "${GUARD_FILE}" GUARD_FILE)

function(check_fixture name expect_failure content)
  set(_source "${TEST_BINARY_DIR}/${name}/source")
  file(MAKE_DIRECTORY "${_source}")
  file(WRITE "${_source}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.24)\n"
    "project(NoQtFixture LANGUAGES NONE)\n"
    "include(\"${GUARD_FILE}\")\n"
    "echo_native_guard_no_qt()\n"
    "${content}\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}"
    -B "${TEST_BINARY_DIR}/${name}/build" -G "${TEST_GENERATOR}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
  if(expect_failure)
    if(_result EQUAL 0 OR NOT "${_stdout}${_stderr}" MATCHES "Native Zero-Qt boundary")
      message(FATAL_ERROR "${name} did not reject the Qt dependency:\n${_stdout}${_stderr}")
    endif()
  elseif(NOT _result EQUAL 0)
    message(FATAL_ERROR "${name} rejected a Qt-free graph:\n${_stdout}${_stderr}")
  endif()
  message(STATUS "Native boundary fixture passed: ${name}")
endfunction()

check_fixture(clean FALSE [=[
  add_library(EchoApplication INTERFACE)
  add_library(JSON INTERFACE IMPORTED)
  add_library(Qt6::Unused INTERFACE IMPORTED)
  set(QBuffer "ordinary variable, not a dependency")
  # Qt6::Core in a comment must not be treated as a linked library.
  target_link_libraries(EchoApplication INTERFACE "$<LINK_ONLY:JSON>")
]=])
check_fixture(direct TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(Qt6::Core INTERFACE IMPORTED)
  target_link_libraries(EchoApplication INTERFACE Qt6::Core)
]=])
check_fixture(transitive TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(Wrapped INTERFACE IMPORTED)
  add_library(Qt6::Core INTERFACE IMPORTED)
  set_property(TARGET Wrapped PROPERTY INTERFACE_LINK_LIBRARIES Qt6::Core)
  target_link_libraries(EchoApplication INTERFACE Wrapped)
]=])
check_fixture(alias TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(Qt6::Core INTERFACE IMPORTED)
  add_library(CoreSDK ALIAS Qt6::Core)
  target_link_libraries(EchoApplication INTERFACE CoreSDK)
]=])
check_fixture(imported_location TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(CoreSDK SHARED IMPORTED)
  set_target_properties(CoreSDK PROPERTIES IMPORTED_LOCATION_RELEASE "C:/SDK/bin/Qt6Core.dll")
  target_link_libraries(EchoApplication INTERFACE CoreSDK)
]=])
check_fixture(imported_implib TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(CoreSDK SHARED IMPORTED)
  set_target_properties(CoreSDK PROPERTIES IMPORTED_IMPLIB "C:/SDK/lib/Qt6Core.lib")
  target_link_libraries(EchoApplication INTERFACE CoreSDK)
]=])
check_fixture(generator_expression TRUE [=[
  add_library(EchoApplication INTERFACE)
  add_library(Wrapped INTERFACE IMPORTED)
  add_library(Qt6::Core INTERFACE IMPORTED)
  set_property(TARGET Wrapped PROPERTY INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:Qt6::Core>")
  target_link_libraries(EchoApplication INTERFACE "$<$<CONFIG:Debug>:Wrapped>")
]=])
check_fixture(raw_library TRUE [=[
  add_library(EchoApplication INTERFACE)
  target_link_libraries(EchoApplication INTERFACE -lQt6Core)
]=])

# The deferred check must also discover targets declared by subdirectories.
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/child_directory/source/child")
file(WRITE "${TEST_BINARY_DIR}/child_directory/source/child/CMakeLists.txt" [=[
  add_library(Child INTERFACE)
  target_link_libraries(Child INTERFACE Qt6::Core)
]=])
check_fixture(child_directory TRUE [=[
  add_library(Qt6::Core INTERFACE IMPORTED GLOBAL)
  add_subdirectory(child)
]=])

# Scoped imports need the guard in their owning directory, where CMake can
# resolve their names. A root-only guard must reject an uninspectable wrapper.
foreach(_case scoped_clean scoped_qt scoped_unchecked)
  file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/${_case}/source/child")
endforeach()
file(WRITE "${TEST_BINARY_DIR}/scoped_clean/source/child/CMakeLists.txt" [=[
  echo_native_guard_no_qt()
  add_library(Child INTERFACE)
  add_library(SDK INTERFACE IMPORTED)
  target_link_libraries(Child INTERFACE SDK)
]=])
file(WRITE "${TEST_BINARY_DIR}/scoped_qt/source/child/CMakeLists.txt" [=[
  echo_native_guard_no_qt()
  add_library(Child INTERFACE)
  add_library(SDK INTERFACE IMPORTED)
  add_library(Qt6::Core INTERFACE IMPORTED)
  target_link_libraries(SDK INTERFACE Qt6::Core)
  target_link_libraries(Child INTERFACE SDK)
]=])
file(WRITE "${TEST_BINARY_DIR}/scoped_unchecked/source/child/CMakeLists.txt" [=[
  add_library(Child INTERFACE)
  add_library(SDK INTERFACE IMPORTED)
  target_link_libraries(Child INTERFACE SDK)
]=])
check_fixture(scoped_clean FALSE "add_subdirectory(child)")
check_fixture(scoped_qt TRUE "add_subdirectory(child)")
check_fixture(scoped_unchecked TRUE "add_subdirectory(child)")
