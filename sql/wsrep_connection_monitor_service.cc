/* Copyright 2024 Codership Oy <info@codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "wsrep_connection_monitor_service.h"

#include "my_global.h"
#include "wsrep_mysqld.h"
#include "wsrep_priv.h"
#include "wsrep_schema.h"

#include <algorithm>
#include <memory>
#include <vector>

class Wsrep_connection_monitor_service : public wsrep::connection_monitor_service
{
public:
    bool connection_monitor_cb(
        wsrep::connection_monitor_service::connection_monitor_key key,
        const wsrep::const_buffer& connection_scheme,
	const wsrep::const_buffer& connection_addr) WSREP_NOEXCEPT override;
};

bool Wsrep_connection_monitor_service::connection_monitor_cb (
  wsrep::connection_monitor_service::connection_monitor_key key,
  const wsrep::const_buffer& connection_scheme,
  const wsrep::const_buffer& connection_addr)
  WSREP_NOEXCEPT
{
  std::string scheme(connection_scheme.data());
  std::string addr(connection_addr.data());
  wsrep_connection_monitor_update(key, scheme, addr);
  return true;
}

std::unique_ptr<wsrep::connection_monitor_service> entrypoint;

wsrep::connection_monitor_service* wsrep_connection_monitor_service_init()
{
  entrypoint = std::unique_ptr<wsrep::connection_monitor_service>(new Wsrep_connection_monitor_service);
  return entrypoint.get();
}

void wsrep_connection_monitor_service_deinit()
{
  entrypoint.reset();
}
