/* Copyright (C) 2025 Codership Oy <info@codership.com>

   Idea by Zsolt Parragi <zsolt.parragi@percona.com>
   Implemented for MariaDB Jan Lindström <jan.lindstrom@galeracluster.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */
#ifndef WSREP_BUFFERED_ERROR_LOG_H
#define WSREP_BUFFERED_ERROR_LOG_H 1

#include <stdio.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdio>
#include <string>
#include <mysql/service_logger.h>

/**
   Interface for Galera buffered error logging using logger service
 */
class Buffered_error_logger
{
private:
  LOGGER_HANDLE* logfile;

 public:
  void init();
  void resize_buffer(const std::size_t buffer_size);
  void resize_file_size(const std::size_t file_size);
  void rotate(const uint n_rotations);

  ~Buffered_error_logger();

  void log(const char *msg, const std::size_t len);
  void write_to_disk();
  void close();
};

#endif /* WSREP_BUFFERED_ERROR_LOG */
