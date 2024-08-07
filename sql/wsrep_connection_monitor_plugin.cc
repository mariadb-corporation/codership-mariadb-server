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

#ifdef WITH_WSREP

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

#include <list>
#include <mutex>

static bool connection_monitor_plugin_enabled= false;

typedef struct
{
  std::string node_uuid;
  std::string address_scheme;
  std::string address;
} wsrep_connection_t;

/** Mutex protecting in-memory cache of the wsrep connections */
static std::mutex wsrep_connections_mutex;
/** Wsrep connections in-memory cache */
static std::list<wsrep_connection_t> wsrep_connections;

bool wsrep_connection_monitor_plugin_enabled()
{
  return connection_monitor_plugin_enabled;
}

static int wsrep_connection_monitor_plugin_init(void *p)
{
  if (!WSREP_ON)
  {
    sql_print_information("Plugin '%s' is disabled.", "wsrep-connection-monitor");
    return 0;
  }

  connection_monitor_plugin_enabled= true;
  return 0;
}

static int wsrep_connection_monitor_plugin_deinit(void *p)
{
  return 0;
}

namespace Show {
static ST_FIELD_INFO wsrep_connenections_fields_info[]=
{
#define NODE_UUID 0
  Column ("node_uuid", Varchar(13), NOT_NULL),
#define CONNECTION_SCHEME
  Column ("connection_scheme", Varchar(3), NOT_NULL),
#define CONNECTION_ADDRESS
  Column ("connection_address", Varchar(13), NOT_NULL),

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

  std::lock_guard<std::mutex> wsrep_connections_lock(wsrep_connections_mutex);
  Field** fields= tables->table->field;

  // Read current cluster members
  std::vector<Wsrep_view::member> members;
  Wsrep_schema::read_members(thd, members);

  for (auto const& conn : wsrep_connections)
  {
    OK(store_string(fields[NODE_UUID], conn.node_uuid));
    OK(store_string(fields[CONNECTION_SCHEME], conn.address_scheme));
    OK(store_string(fields[CONNECTION_ADDRESS], conn.address));
    OK(schema_table_store_record(thd, tables->table));
  }

  return 0;
}

/** Bind the dynamic table INFORMATION_SCHEMA.wsrep_connections
@return 0 on success */
static int wsrep_connections_init(void*	p)
{
  ST_SCHEMA_TABLE* schema;

  schema = (ST_SCHEMA_TABLE*) p;

  schema->fields_info = Show::wsrep_connections_fields_info;
  schema->fill_table = fill_wsrep_connections;

  return 0;
}

static struct st_mysql_information_schema i_s_info =
{
  MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION
};

/** version number reported by SHOW PLUGINS */
constexpr unsigned i_s_version= MYSQL_VERSION_MAJOR << 8 | MYSQL_VERSION_MINOR;

static int i_s_common_deinit(void*)
{
  /* Do nothing */
  return 0;
}

struct st_maria_plugin	i_s_wsrep_connections=
{
  /* the plugin type (a MYSQL_XXX_PLUGIN value) */
  /* int */
  MYSQL_INFORMATION_SCHEMA_PLUGIN,

  /* pointer to type-specific plugin descriptor */
  /* void* */
  &i_s_info,

  /* plugin name */
  /* const char* */
  "wsrep_connections",

  /* plugin author (for SHOW PLUGINS) */
  /* const char* */
  "Codership Oy",

  /* general descriptive text (for SHOW PLUGINS) */
  /* const char* */
  "Galera connections",

  /* the plugin license (PLUGIN_LICENSE_XXX) */
  /* int */
  PLUGIN_LICENSE_GPL,

  /* the function to invoke when plugin is loaded */
  /* int (*)(void*); */
  wsrep_connections_init,

  /* the function to invoke when plugin is unloaded */
  /* int (*)(void*); */
  i_s_common_deinit,

  i_s_version, nullptr, nullptr, PACKAGE_VERSION,
  MariaDB_PLUGIN_MATURITY_STABLE
};

static void wsrep_connection_remove(const std::string &scheme, const std::string addr)
{
  std::lock_guard<std::mutex> wsrep_lock(wsrep_connection_mutex);
  WSREP_DEBUG("wsrep_connection_remove: %s//%s", scheme.c_str(), addr.c_str());
  std::list<wsrep_connection_t>::iterator i = wsrep_connections.begin();
  while(i != wsrep_connections.end())
  {
    wsrep_connection_t &conn= *i;
    WSREP_DEBUG("wsrep_connection_remove: found for %s : %s//%s",
                conn.node_uuid.c_str(), conn.address_scheme.c_str(), conn.address.c_str());
    if(conn.address.compare(addr) == 0)
    {
      wsrep_connections.erase(i);
      break;
    }
    i++;
  }
}

static void wsrep_connection_add(const std::string &scheme, const std::string addr)
{
  int error=0;
  my_thread_init();
  THD thd(0);
  thd.thread_stack= (char *) &thd;
  wsrep_init_thd_for_schema(&thd);

  WSREP_DEBUG("wsrep_connection_add: %s%%%s", scheme.c_str(), addr.c_str());

  std::lock_guard<std::mutex> wsrep_lock(wsrep_connection_mutex);
  std::list<wsrep_connection_t>::iterator i = wsrep_connections.begin();
  while(i != wsrep_connections.end())
  {
    wsrep_connection_t &conn= *i;
    WSREP_DEBUG("wsrep_connection_add: found for %s : %s//%s",
                conn.node_uuid.c_str(), conn.address_scheme.c_str(), conn.address.c_str());
    if(conn.address.compare(addr) == 0)
    {
      break; // found
    }
    i++;
  }

  // Not found : add
  if (i == wsrep_connections.end())
  {
    wsrep_connection_t new_connection;
    new_connection.address_scheme= scheme;
    new_connection.address= addr;
    wsrep_connections.push_back(new_connection);
  }
  my_thread_end();
  return 0;
}

}


void wsrep_connection_monitor_update(
				     wsrep::connection_monitor_service::connection_monitor_key key,
				     const std::string &scheme,
				     const std::string &addr)
{
  switch(key)
  {
  case wsrep::connection_monitor_service::connection_monitor_key::connection_disconnected:
  {
    /* Galera library has disconnected one of the connections. Remove it from in-memory
       cache if found. If not found we ignore request. */
    wsrep_connection_remove(scheme, addr);
    break
  }
  case wsrep::connection_monitor_service::connection_monitor_key::connection_disconnected:
  {
    /* Galera library has created new connection. We identify node UUID and
       add connection information to in-memory cache. If this connection
       is already there we ignore request. */
    wsrep_connection_add(scheme, addr);
  }
  default:
  {
    WSREP_ERROR("wsrep_connection_monitor_update: incorrect key %ld", key);
    assert(0);
    break;
  }
  }
}

#endif /* WITH_WSREP */
