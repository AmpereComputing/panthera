# Adapted from https://blog.kitware.com/cmake-and-the-default-build-type/ and https://github.com/openchemistry/tomviz/blob/master/cmake/BuildType.cmake
# The latter is marked "Copyright (c) 2014-2021, Kitware, Inc. All rights reserved." and licensed as BSD-3-Clause.

# Set default build type to 'Release' if not specified
set(default_build_type "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "Coverage")
endif()
