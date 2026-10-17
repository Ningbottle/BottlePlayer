# Stage 0 (vip-stability remediation plan): archive PDBs so Release faults
# can always be symbolized against the exact build that shipped.
# Invoked as: cmake -DCONFIG=... -DBINARY_DIR=... -DARCHIVE_DIR=... -P ArchivePdb.cmake

if(NOT DEFINED CONFIG OR NOT DEFINED BINARY_DIR OR NOT DEFINED ARCHIVE_DIR)
  message(FATAL_ERROR "ArchivePdb.cmake requires CONFIG, BINARY_DIR and ARCHIVE_DIR")
endif()

file(GLOB_RECURSE _pdbs "${BINARY_DIR}/*.pdb")
list(FILTER _pdbs EXCLUDE REGEX "${BINARY_DIR}/pdb/")

if(NOT _pdbs)
  message(FATAL_ERROR "ArchivePdb: no PDBs found under ${BINARY_DIR} — "
    "Release builds must generate symbol files")
endif()

file(MAKE_DIRECTORY "${ARCHIVE_DIR}")
set(_copied "")
foreach(_pdb IN LISTS _pdbs)
  get_filename_component(_name "${_pdb}" NAME)
  file(COPY_FILE "${_pdb}" "${ARCHIVE_DIR}/${_name}")
  list(APPEND _copied "${_name}")
endforeach()
list(SORT _copied)
string(REPLACE ";" "\n  " _copied_multiline "${_copied}")
message(STATUS "ArchivePdb [${CONFIG}] → ${ARCHIVE_DIR}:\n  ${_copied_multiline}")
