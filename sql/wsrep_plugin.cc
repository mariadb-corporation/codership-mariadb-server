/* Copyright 2016-2021 Codership Oy <http://www.codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
  Wsrep plugin comes in two parts, wsrep_plugin and wsrep_provider_plugin.

  Wsrepl_plugin is used to initialize facilities which are needed before
  wsrep_provider_plugin can be initialized, such as provider options.
*/

#include "wsrep_trans_observer.h"
#include "wsrep_mysqld.h"

#include <mysql/plugin.h>

#include "wsrep/provider_options.hpp"
#include "wsrep_server_state.h"

static int wsrep_provider_sysvar_check(THD *, struct st_mysql_sys_var *var,
                                       void *save, struct st_mysql_value *value)
{
  WSREP_INFO("Sysvar check");
  auto options= Wsrep_server_state::get_options();
  if (!options)
  {
    WSREP_ERROR("Provider options not initialized in plugin sysvar check");
    return 1;
  }
  auto opt= Wsrep_server_state::sysvar_to_option(var);
  if (!opt)
  {
    WSREP_ERROR("Could not match var to option");
    return 1;
  }
  int len= 0;
  const char* new_value= value->val_str(value, 0, &len);
  if (options->set(opt->name(), new_value)) return 1;
  *((const char**)save)= opt->value();
  return 0;
}

static void wsrep_provider_sysvar_update(THD *thd, struct st_mysql_sys_var *var,
                                         void *var_ptr, const void *save)
{
  auto options= Wsrep_server_state::get_options();
  if (!options)
  {
    WSREP_ERROR("Provider options not initialized in plugin sysvar update");
    my_error(ER_UNKNOWN_ERROR, MYF(0));
    return;
  }

  auto opt= Wsrep_server_state::sysvar_to_option(var);
  if (!opt)
  {
    WSREP_ERROR("Could not match var to option");
    my_error(ER_UNKNOWN_ERROR, MYF(0));
    return;
  }
  /*
    If the value is changed to default, save will contain pointer to
    default.
   */
  if (*(char**)save == opt->default_value())
  {
    if (options->set(opt->name(), opt->default_value()))
    {
      WSREP_WARN("Could not set provider option to default value: %s",
                 opt->default_value());
      my_error(ER_WRONG_VALUE_FOR_VAR,
               MYF(0),
               opt->name(),
               opt->default_value());
      return;
    }
  }
  *((char**)var_ptr) = *(char**)save;
}

/* Prototype for provider system variables */
static char *wsrep_dummy= 0;
MYSQL_SYSVAR_STR(wsrep_provider_sysvar_proto,
                 wsrep_dummy,
                 0,
                 "Wsrep provider option",
                 wsrep_provider_sysvar_check,
                 wsrep_provider_sysvar_update, "");

static void wsrep_plugin_append_sys_var(wsrep::provider_options::option* opt)
{
  mysql_sysvar_wsrep_provider_sysvar_proto.name = opt->name();
  char** val= (char**)my_malloc(PSI_NOT_INSTRUMENTED, sizeof(char*), MYF(0));
  *val = (char*)opt->value();
  mysql_sysvar_wsrep_provider_sysvar_proto.value = val;
  mysql_sysvar_wsrep_provider_sysvar_proto.def_val = opt->default_value();
  st_mysql_sys_var* var= (st_mysql_sys_var*)
    my_malloc(PSI_NOT_INSTRUMENTED,
              sizeof(mysql_sysvar_wsrep_provider_sysvar_proto), MYF(0));
  memcpy(var, &mysql_sysvar_wsrep_provider_sysvar_proto,
         sizeof(mysql_sysvar_wsrep_provider_sysvar_proto));

  Wsrep_server_state::append_sysvar(var, opt);
}

static char* wsrep_plugin_get_var_value_ptr(st_mysql_sys_var* var)
{
  size_t val_ptr_off= (char*)&mysql_sysvar_wsrep_provider_sysvar_proto.value -
    (char*)&mysql_sysvar_wsrep_provider_sysvar_proto;
  char** ptr= (char**)((char*)var + val_ptr_off);
  if (ptr) return *ptr;
  return 0;
}

static int wsrep_provider_plugin_init(void *p)
{
  WSREP_DEBUG("wsrep_provider_plugin_init()");
  return 0;
}

static int wsrep_provider_plugin_deinit(void *p)
{
  WSREP_DEBUG("wsrep_provider_plugin_deinit()");
  auto sysvars= Wsrep_server_state::sysvars();
  if (sysvars)
  {
    for (st_mysql_sys_var** var= sysvars; *var; ++var)
    {
      my_free(wsrep_plugin_get_var_value_ptr(*var));
    }
  }
  return 0;
}

struct Mysql_replication wsrep_provider_plugin = {
  MYSQL_REPLICATION_INTERFACE_VERSION
};

maria_declare_plugin(wsrep_provider)
{
  MYSQL_REPLICATION_PLUGIN,
  &wsrep_provider_plugin,
  "wsrep_provider",
  "Codership Oy",
  "Wsrep provider plugin",
  PLUGIN_LICENSE_GPL,
  wsrep_provider_plugin_init,
  wsrep_provider_plugin_deinit,
  0x0100,
  NULL, /* Status variables */
  /* System variables, this will be assigned by wsrep plugin below. */
  NULL,
  "1.0", /* Version (string) */
  MariaDB_PLUGIN_MATURITY_ALPHA     /* Maturity */
}
maria_declare_plugin_end;

static int wsrep_plugin_init_provider_options()
{
  WSREP_DEBUG("wsrep_plugin_init_provider_options()");
  auto options= Wsrep_server_state::get_options();
  if (!options)
  {
    WSREP_ERROR("Provider options not initialized before provider plugin init");
    return 1;
  }
  options->for_each([](wsrep::provider_options::option* opt)
                    {
                      wsrep_plugin_append_sys_var(opt);
                    });
  Wsrep_server_state::append_sysvar(nullptr, nullptr);
  builtin_maria_wsrep_provider_plugin->system_vars= Wsrep_server_state::sysvars();
  return 0;
};

/*
   Wsrep plugin
 */

static int wsrep_plugin_init(void *p)
{
  WSREP_DEBUG("wsrep_plugin_init()");
  if (Wsrep_server_state::get_options())
  {
    return wsrep_plugin_init_provider_options();
  }
  return 0;
}

static int wsrep_plugin_deinit(void *p)
{
  WSREP_DEBUG("wsrep_plugin_deinit()");
  return 0;
}

struct Mysql_replication wsrep_plugin= {
  MYSQL_REPLICATION_INTERFACE_VERSION
};

maria_declare_plugin(wsrep)
{
  MYSQL_REPLICATION_PLUGIN,
  &wsrep_plugin,
  "wsrep",
  "Codership Oy",
  "Wsrep replication plugin",
  PLUGIN_LICENSE_GPL,
  wsrep_plugin_init,
  wsrep_plugin_deinit,
  0x0100,
  NULL, /* Status variables */
  NULL, /* System variables */
  "1.0", /* Version (string) */
  MariaDB_PLUGIN_MATURITY_STABLE     /* Maturity */
}
maria_declare_plugin_end;
