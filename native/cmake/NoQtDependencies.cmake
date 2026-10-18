include_guard(GLOBAL)

# Check the configured graph, including dependencies hidden behind an imported
# wrapper or a generator expression. No SDK or scripting runtime is needed.
function(_echo_no_qt_value owner property value chain)
  string(TOLOWER "${value}" _lower)
  if(_lower MATCHES "(^|[^a-z0-9_])qt[456]?::"
      OR _lower MATCHES "(^|[ /\\\\;:<>,])(-l)?(lib)?qt[456][a-z0-9_]+([.](lib|dll|a|dylib|so([.][0-9]+)*))?($|[ /\\\\;:<>,])")
    message(FATAL_ERROR
      "Native Zero-Qt boundary: ${chain}\n"
      "${owner}.${property} contains a Qt dependency: ${value}")
  endif()

  # Do not evaluate configuration-dependent expressions: inspect every branch.
  # Extracting target tokens also handles LINK_ONLY, IF and TARGET_NAME_IF_EXISTS.
  string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.+/-]*(::[A-Za-z_][A-Za-z0-9_.+/-]*)*" _tokens "${value}")
  foreach(_token IN LISTS _tokens)
    if(TARGET "${_token}")
      _echo_no_qt_target("${_token}" "${chain}" "${ARGN}")
    endif()
  endforeach()
endfunction()

function(_echo_no_qt_target target chain visited)
  if(target IN_LIST visited)
    return()
  endif()
  list(APPEND visited "${target}")
  if(chain)
    string(APPEND chain " -> ${target}")
  else()
    set(chain "${target}")
  endif()
  get_target_property(_alias "${target}" ALIASED_TARGET)
  if(_alias)
    _echo_no_qt_target("${_alias}" "${chain}" "${visited}")
    return()
  endif()

  string(TOLOWER "${target}" _lower_target)
  if(_lower_target MATCHES "^qt[456]?::")
    message(FATAL_ERROR "Native Zero-Qt boundary: ${chain}")
  endif()
  set(_properties LINK_LIBRARIES INTERFACE_LINK_LIBRARIES
    INTERFACE_LINK_LIBRARIES_DIRECT LINK_OPTIONS INTERFACE_LINK_OPTIONS
    IMPORTED_LOCATION IMPORTED_IMPLIB IMPORTED_LINK_INTERFACE_LIBRARIES
    IMPORTED_LINK_DEPENDENT_LIBRARIES)
  get_target_property(_configs "${target}" IMPORTED_CONFIGURATIONS)
  list(APPEND _configs DEBUG RELEASE RELWITHDEBINFO MINSIZEREL NOCONFIG)
  list(REMOVE_DUPLICATES _configs)
  foreach(_config IN LISTS _configs)
    string(TOUPPER "${_config}" _config)
    foreach(_property IMPORTED_LOCATION IMPORTED_IMPLIB
        IMPORTED_LINK_INTERFACE_LIBRARIES IMPORTED_LINK_DEPENDENT_LIBRARIES)
      list(APPEND _properties "${_property}_${_config}")
    endforeach()
  endforeach()
  foreach(_property IN LISTS _properties)
    get_target_property(_value "${target}" "${_property}")
    if(_value AND NOT _value MATCHES "-NOTFOUND$")
      _echo_no_qt_value("${target}" "${_property}" "${_value}" "${chain}" ${visited})
    endif()
  endforeach()
endfunction()

function(_echo_no_qt_directory directory)
  # Non-GLOBAL imported targets are only inspectable in their owning directory.
  # A future child using them must install this guard there too; fail closed
  # instead of silently skipping a wrapper that could hide a Qt dependency.
  get_property(_checked DIRECTORY "${directory}" PROPERTY ECHO_NO_QT_CHECKED)
  get_property(_imports DIRECTORY "${directory}" PROPERTY IMPORTED_TARGETS)
  foreach(_import IN LISTS _imports)
    if(NOT TARGET "${_import}" AND NOT _checked)
      message(FATAL_ERROR
        "Native Zero-Qt boundary: cannot inspect ${directory}'s scoped imported target ${_import}. "
        "Call echo_native_guard_no_qt() in that directory's CMakeLists.txt.")
    endif()
  endforeach()
  get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_target IN LISTS _targets)
    _echo_no_qt_target("${_target}" "" "")
  endforeach()
  get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    _echo_no_qt_directory("${_child}")
  endforeach()
endfunction()

function(_echo_no_qt_check)
  _echo_no_qt_directory("${CMAKE_CURRENT_SOURCE_DIR}")
  set_property(DIRECTORY PROPERTY ECHO_NO_QT_CHECKED TRUE)
endfunction()

function(echo_native_guard_no_qt)
  # Defer until targets and child directories have finished declaring links.
  cmake_language(DEFER CALL _echo_no_qt_check)
endfunction()
