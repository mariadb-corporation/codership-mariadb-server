# Copyright (C) 2023 Codership Oy <info@galeracluster.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

OPTION(WITH_WSREP "Build Galera library" OFF)

MESSAGE(STATUS "Building with Galera ${WITH_WSREP}")

IF(NOT WITH_WSREP)
  RETURN()
ENDIF()

IF (WITH_WSREP)

  IF(NOT EXISTS "${CMAKE_SOURCE_DIR}/plugin/galera_replication/galera/GALERA_VERSION")
    MESSAGE(FATAL_ERROR "No MariaDB galera library code! Run
    ${GIT_EXECUTABLE} submodule update --init --recursive
Then restart the build.
")
  ENDIF()

  ADD_SUBDIRECTORY(galera)

  INSTALL(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/galera/libgalera_smm.so"
    DESTINATION ${INSTALL_PLUGINDIR}
    COMPONENT Server)
  INSTALL(PROGRAMS
    "${CMAKE_CURRENT_BINARY_DIR}/galera/garb/garbd"
    DESTINATION ${INSTALL_BINDIR}
    COMPONENT Server)
ENDIF()
