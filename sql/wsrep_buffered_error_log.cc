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

#include "wsrep_buffered_error_log.h"
#include "wsrep.h"
#include "wsrep_mysqld.h"
#include <my_stacktrace.h>

Buffered_error_logger wsrep_buffered_error_log;
const char *wsrep_buffered_error_log_filename= nullptr;
unsigned long long wsrep_buffered_error_log_buffer_size= 0;
unsigned long long wsrep_buffered_error_log_file_size= 0;
uint wsrep_buffered_error_log_rotations= 0;

void Buffered_error_logger::init()
{
  logfile= nullptr;

  if (wsrep_buffered_error_log_buffer_size > 0 &&
      wsrep_buffered_error_log_file_size > 0 &&
      wsrep_buffered_error_log_filename != nullptr &&
      strlen(wsrep_buffered_error_log_filename) > 0)
  {
    logfile= logger_open(wsrep_buffered_error_log_filename,
                         wsrep_buffered_error_log_file_size,
                         wsrep_buffered_error_log_rotations,
                         wsrep_buffered_error_log_buffer_size);

    if (!logfile)
    {
      WSREP_WARN("Could not open buffered error log %s error=%s (%d).",
                 wsrep_buffered_error_log_filename,
                 strerror(errno), errno);
      wsrep_disable_logging();
    }
    else
    {
      WSREP_INFO("Using buffered error logging into %s file_size %llu buffer_size %llu rotations %lu",
		 wsrep_buffered_error_log_filename,
                         wsrep_buffered_error_log_file_size,
                         wsrep_buffered_error_log_buffer_size,
		 wsrep_buffered_error_log_rotations);
      wsrep_debug_mode |= WSREP_DEBUG_MODE_BUFFERED;
    }
  }

  return;
}

void Buffered_error_logger::resize_buffer(const std::size_t buffer_size)
{
  if (!logfile)
    return;

  if (logger_resize_buffer(logfile, buffer_size))
  {
    wsrep_disable_logging();
    WSREP_WARN("Resize of buffered error log %s to size %zd failed error=%s (%ld).",
               wsrep_buffered_error_log_filename, buffer_size,
               strerror(errno), errno);
  }
}

void Buffered_error_logger::resize_file_size(const std::size_t file_size)
{
  if (!logfile)
    return;

  if (logger_set_filesize_limit(logfile, file_size))
  {
    wsrep_disable_logging();
    WSREP_WARN("Resize of buffered error log %s file size to %zd failed error=%s (%ld).",
               wsrep_buffered_error_log_filename, file_size,
               strerror(errno), errno);
  }
}


Buffered_error_logger::~Buffered_error_logger()
{
  if (logfile)
  {
    logger_close(logfile);
    logfile= nullptr;
  }
}

void Buffered_error_logger::log(const char *msg, const std::size_t len)
{
  if (logfile)
  {
    if (!logger_write(logfile, msg, len))
    {
      wsrep_disable_logging();
      my_safe_printf_stderr("Log write to buffered error log %s failed error=%s (%d).",
                            wsrep_buffered_error_log_filename,
                            strerror(errno), errno);
    }
  }
}

void Buffered_error_logger::write_to_disk()
{
  if (logfile)
  {
    if (logger_sync(logfile))
    {
      wsrep_disable_logging();
      WSREP_WARN("Log write to buffered error log %s failed error=%s (%ld).",
               wsrep_buffered_error_log_filename,
               strerror(errno), errno);
    }
  }
}

void Buffered_error_logger::close()
{
  if (logfile)
  {
    logger_sync(logfile);
    logger_close(logfile);
    logfile= nullptr;
  }
}

void Buffered_error_logger::rotate(const uint n_rotations)
{
  if (!logfile || n_rotations == 0)
    return;

  if (logger_rotate(logfile))
  {
    wsrep_disable_logging();
    WSREP_WARN("Rotation of buffered error log %s failed error=%s (%ld).",
               wsrep_buffered_error_log_filename,
               strerror(errno), errno);
  }
}
