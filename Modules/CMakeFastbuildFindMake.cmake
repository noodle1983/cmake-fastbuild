# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

find_program(CMAKE_MAKE_PROGRAM
  NAMES fbuild
  DOC "Program used to build from fastbuild .bff files.")
mark_as_advanced(CMAKE_MAKE_PROGRAM)
