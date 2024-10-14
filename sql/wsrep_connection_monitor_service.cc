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
  bool connection_monitor_connect_cb(
      wsrep_connection_key_t id,
      const wsrep::const_buffer& scheme,
      const wsrep::const_buffer& local_addr,
      const wsrep::const_buffer& remote_uuid,
      const wsrep::const_buffer& remote_addr) WSREP_NOEXCEPT override;

  bool connection_monitor_disconnect_cb(
      wsrep_connection_key_t id) WSREP_NOEXCEPT override;

  bool connection_monitor_ssl_info_cb(
      wsrep_connection_key_t id,
      const wsrep::const_buffer& chipher,
      const wsrep::const_buffer& certificate_subject,
      const wsrep::const_buffer& certificate_issuer,
      const wsrep::const_buffer& version) WSREP_NOEXCEPT override;
};

bool Wsrep_connection_monitor_service::connection_monitor_connect_cb (
  wsrep_connection_key_t id,
  const wsrep::const_buffer& scheme,
  const wsrep::const_buffer& local_addr,
  const wsrep::const_buffer& remote_uuid,
  const wsrep::const_buffer& remote_addr)
  WSREP_NOEXCEPT
{
  std::string remote(remote_uuid.data());
  std::string lscheme(scheme.data());
  std::string raddr(remote_addr.data());
  std::string laddr(local_addr.data());

  return wsrep_connection_monitor_connect(id, lscheme, laddr, remote, raddr);
}

bool Wsrep_connection_monitor_service::connection_monitor_disconnect_cb (
  wsrep_connection_key_t id) WSREP_NOEXCEPT
{
  return wsrep_connection_monitor_disconnect(id);
}

bool Wsrep_connection_monitor_service::connection_monitor_ssl_info_cb (
  wsrep_connection_key_t id,
  const wsrep::const_buffer& chipher,
  const wsrep::const_buffer& certificate_subject,
  const wsrep::const_buffer& certificate_issuer,
  const wsrep::const_buffer& version)
  WSREP_NOEXCEPT
{
  std::string ch(chipher.data());
  std::string subject(certificate_subject.data());
  std::string issuer(certificate_issuer.data());
  std::string vers(version.data());

  return wsrep_connection_monitor_ssl_info(id, ch, subject, issuer, vers);
}

std::unique_ptr<wsrep::connection_monitor_service> monitor_entrypoint;

wsrep::connection_monitor_service* wsrep_connection_monitor_service_init()
{
  monitor_entrypoint = std::unique_ptr<wsrep::connection_monitor_service>(new Wsrep_connection_monitor_service);
  return monitor_entrypoint.get();
}

void wsrep_connection_monitor_service_deinit()
{
  monitor_entrypoint.reset();
}
