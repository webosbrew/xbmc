#.rst:
# FindNlohmannJson
# --------
# Finds the nlohmann-json library
#
# This will define the following variables::
#
# NLOHMANNJSON_FOUND - system has nlohmann-json
# NLOHMANNJSON_INCLUDE_DIRS - the nlohmann-json include directory
# NLOHMANNJSON_DEFINITIONS - the nlohmann-json compile definitions
#
# and the following imported targets::
#
#   NLOHMANNJSON::NLOHMANNJSON   - The nlohmann-json library

if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_NLOHMANNJSON nlohmann_json>=3.0.0 QUIET)
endif()

find_path(NLOHMANNJSON_INCLUDE_DIR NAMES nlohmann/json.hpp
        PATHS ${PC_NLOHMANNJSON_INCLUDEDIR})

set(NLOHMANNJSON_VERSION 3.0.0)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NlohmannJson
                                  REQUIRED_VARS NLOHMANNJSON_INCLUDE_DIR
                                  VERSION_VAR NLOHMANNJSON_VERSION)

if(NLOHMANNJSON_FOUND)
  set(NLOHMANNJSON_INCLUDE_DIRS ${NLOHMANNJSON_INCLUDE_DIR})
  set(NLOHMANNJSON_LIBRARIES ${NLOHMANNJSON_LIBRARY})

  if(NOT TARGET NLOHMANNJSON::NLOHMANNJSON)
    add_library(NLOHMANNJSON::NLOHMANNJSON UNKNOWN IMPORTED)
    set_target_properties(NLOHMANNJSON::NLOHMANNJSON PROPERTIES
                                     INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANNJSON_INCLUDE_DIR}")
  endif()
endif()

mark_as_advanced(NLOHMANNJSON_INCLUDE_DIR NLOHMANNJSON_LIBRARY)
