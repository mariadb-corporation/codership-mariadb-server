/* Copyright 2024 Codership Oy <http://www.codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA */

/*
*/

#include "my_global.h"
#include "mysqld_error.h"
#include <mysql/plugin.h>
#include "sql_plugin.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "set_var.h"
#include "sql_acl.h"
#include "sql_i_s.h"
#include "wsrep_mysqld.h"
#include "wsrep_schema.h"

#include <map>
#include <mutex>

#include "wsrep_connection_monitor_service.h"

#ifdef WITH_WSREP
static bool connection_monitor_plugin_enabled= false;

/** Mutex protecting in-memory cache of the wsrep connections */
std::mutex wsrep_connections_mutex;

/** Wsrep connections in-memory cache */
std::map<uintptr_t, wsrep_connection_t> wsrep_connections;

bool wsrep_connection_monitor_plugin_enabled()
{
  return connection_monitor_plugin_enabled;
}

static int wsrep_connection_monitor_plugin_deinit(void *p)
{
  return 0;
}

namespace Show {
static ST_FIELD_INFO wsrep_connections_fields_info[]=
{
#define CONNECTION_ID 0
  Column ("connection_id", ULonglong(), NOT_NULL),
#define CONNECTION_SCHEME 1
  Column ("connection_scheme", Varchar(3), NOT_NULL),
#define LOCAL_ADDRESS 2
  Column ("local_address", Varchar(256), NOT_NULL),
#define REMOTE_UUID 3
  Column ("remote_uuid", Varchar(256), NOT_NULL),
#define REMOTE_ADDRESS 4
  Column ("remote_address", Varchar(256), NOT_NULL),
#define CHIPHER 5
  Column ("chipher", Varchar(256), NOT_NULL),
#define CERTIFICATE_SUBJECT 6
  Column ("certificate_subject", Varchar(256), NOT_NULL),
#define CERTIFICATE_ISSUER 7
  Column ("certificate_issuer", Varchar(256), NOT_NULL),
#define CERTIFICATE_VERSION 8
  Column ("certificate_version", Varchar(256), NOT_NULL),

  CEnd()
};
} // namespace Show


#define OK(expr)     \
  if ((expr) != 0)   \
  {                  \
    return (1);      \
  }

static int store_string(Field* field, const std::string &str)
{
  if (str.empty())
  {
    field->set_null();
    return 0;
  }

  field->set_notnull();
  return field->store(str.c_str(), uint(str.size()), system_charset_info);
}

static int fill_wsrep_connections(THD *thd, TABLE_LIST *tables, Item *)
{
  // Require wsrep enabled and deny access to non-superusers
  if (!WSREP(thd) || check_global_access(thd, PROCESS_ACL))
    return 0;

  // Wsrep should be inited
  if (!wsrep_inited)
  {
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_CANT_FIND_SYSTEM_REC,
                        "Galera: SELECTing from "
                        "INFORMATION_SCHEMA.wsrep_connections but "
                        "the wsrep is not inited");
    return 0;
  }

  Field** fields= tables->table->field;
  std::lock_guard<std::mutex> wsrep_connections_lock(wsrep_connections_mutex);
  std::map<uintptr_t, wsrep_connection_t>::iterator c = wsrep_connections.begin();

  WSREP_DEBUG(":::JAN:::FILL_WSREP_CONNECTIONS");
  while( c != wsrep_connections.end())
  {
    wsrep_connection_t &conn = c->second;
    OK(fields[CONNECTION_ID]->store(conn.connection_id));
    OK(store_string(fields[CONNECTION_SCHEME], conn.scheme));
    OK(store_string(fields[LOCAL_ADDRESS], conn.local_address));
    OK(store_string(fields[REMOTE_UUID], conn.remote_uuid));
    OK(store_string(fields[REMOTE_ADDRESS], conn.remote_address));
    OK(store_string(fields[CHIPHER], conn.chipher));
    OK(store_string(fields[CERTIFICATE_SUBJECT], conn.certificate_subject));
    OK(store_string(fields[CERTIFICATE_ISSUER], conn.certificate_issuer));
    OK(store_string(fields[CERTIFICATE_VERSION], conn.version));
    OK(schema_table_store_record(thd, tables->table));
    c++;
  }

  return 0;
}

/** Bind the dynamic table INFORMATION_SCHEMA.wsrep_connections
@return 0 on success */
static int wsrep_connections_init(void*	p)
{
  ST_SCHEMA_TABLE* schema;

  connection_monitor_plugin_enabled= true;

  schema = (ST_SCHEMA_TABLE*) p;
  schema->fields_info = Show::wsrep_connections_fields_info;
  schema->fill_table = fill_wsrep_connections;

  return 0;
}

static struct st_mysql_information_schema plugin_descriptor =
{
  MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION
};

/** version number reported by SHOW PLUGINS */
constexpr unsigned i_s_version= MYSQL_VERSION_MAJOR << 8 | MYSQL_VERSION_MINOR;

maria_declare_plugin(wsrep_connection_monitor)
{
  /* the plugin type (a MYSQL_XXX_PLUGIN value) */
  /* int */
  MYSQL_INFORMATION_SCHEMA_PLUGIN,

  /* pointer to type-specific plugin descriptor */
  /* void* */
  &plugin_descriptor,

  /* plugin name */
  /* const char* */
  "wsrep_connections",

  /* plugin author (for SHOW PLUGINS) */
  /* const char* */
  "Codership Oy",

  /* general descriptive text (for SHOW PLUGINS) */
  /* const char* */
  "Provides information about Galera connections",

  /* the plugin license (PLUGIN_LICENSE_XXX) */
  /* int */
  PLUGIN_LICENSE_GPL,

  /* the function to invoke when plugin is loaded */
  /* int (*)(void*); */
  wsrep_connections_init,

  /* the function to invoke when plugin is unloaded */
  /* int (*)(void*); */
  wsrep_connection_monitor_plugin_deinit,

  i_s_version, nullptr, nullptr, PACKAGE_VERSION,
  MariaDB_PLUGIN_MATURITY_STABLE
}
maria_declare_plugin_end;

bool wsrep_connection_monitor_connect(wsrep_connection_key_t id,
                                      const std::string &scheme,
                                      const std::string &local_addr,
                                      const std::string &remote_uuid,
                                      const std::string &remote_addr)
{
  uintptr_t key = (uintptr_t)(id);

  std::lock_guard<std::mutex> wsrep_lock(wsrep_connections_mutex);
  std::map<uintptr_t, wsrep_connection_t>::iterator i = wsrep_connections.find(key);

  WSREP_DEBUG("wsrep_connection_add: %llu : %s %s %s %s",
              key, scheme.c_str(), local_addr.c_str(),
              remote_uuid.c_str(), remote_addr.c_str());

  // Not found : add
  if (i == wsrep_connections.end())
  {
    WSREP_DEBUG("wsrep_connection_add: key %lld not found add %s %s %s %s", key,
                scheme.c_str(), local_addr.c_str(), remote_uuid.c_str(), remote_addr.c_str());
    wsrep_connection_t new_connection {key, scheme, local_addr, remote_uuid, remote_addr,
                                       "","","",""};
    wsrep_connections.insert(std::pair<uintptr_t, wsrep_connection_t>(key, new_connection));
  }
  else
  {
    // found, update
    wsrep_connection_t &old_conn= i->second;
    WSREP_DEBUG("wsrep_connection_add: key %lld found %s %s %s %s new %s %s %s",
                key, old_conn.scheme.c_str(), old_conn.local_address.c_str(),
                old_conn.remote_uuid.c_str(), old_conn.remote_address.c_str(),
                remote_uuid.c_str(), remote_addr.c_str(), local_addr.c_str());

    old_conn.remote_uuid= remote_uuid;
    old_conn.scheme= scheme;
    old_conn.remote_address= remote_addr;
    old_conn.local_address= local_addr;
  }
  return true;
}

bool wsrep_connection_monitor_disconnect(wsrep_connection_key_t id)
{
  uintptr_t key = (uintptr_t)id;
  std::lock_guard<std::mutex> wsrep_lock(wsrep_connections_mutex);
  WSREP_DEBUG("wsrep_connection_remove: %lld", key);

  std::map<uintptr_t, wsrep_connection_t>::iterator c = wsrep_connections.find(key);

  if (c != wsrep_connections.end())
  {
    wsrep_connection_t conn= c->second;
    WSREP_DEBUG("wsrep_connection_remove: found for %llu : %s %s %s %s",
                key,
                conn.scheme.c_str(), conn.local_address.c_str(),
                conn.remote_uuid.c_str(), conn.remote_address.c_str());
    wsrep_connections.erase(c);
  }
  return true;
}

bool wsrep_connection_monitor_ssl_info(wsrep_connection_key_t id,
                                       const std::string &chipher,
                                       const std::string &certificate_subject,
                                       const std::string &certificate_issuer,
                                       const std::string &version)
{
  uintptr_t key = (uintptr_t)(id);

  std::lock_guard<std::mutex> wsrep_lock(wsrep_connections_mutex);
  std::map<uintptr_t, wsrep_connection_t>::iterator i = wsrep_connections.find(key);

  // Not found : add
  if (i != wsrep_connections.end())
  {
    // found, update
    wsrep_connection_t &old_conn= i->second;
    WSREP_DEBUG("wsrep_connection_ssl_info: key %lld %s %s %s %s : %s %s %s %s",
                key, old_conn.scheme.c_str(), old_conn.local_address.c_str(),
                old_conn.remote_uuid.c_str(), old_conn.remote_address.c_str(),
                chipher.c_str(), certificate_subject.c_str(),
                certificate_issuer.c_str(), version.c_str());
    old_conn.chipher= chipher;
    old_conn.certificate_subject= certificate_subject;
    old_conn.certificate_issuer= certificate_issuer;
    old_conn.version= version;
  }
  else
    WSREP_DEBUG("::JAN::not found %lld", key);
  return true;
}

#endif /* WITH_WSREP */
