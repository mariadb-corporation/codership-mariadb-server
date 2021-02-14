/* Copyright 2016 Codership Oy <http://www.codership.com>

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

#include "wsrep_trans_observer.h"
#include "wsrep_mysqld.h"

#include <mysql/plugin.h>

#include "wsrep/provider_options.hpp"
#include "wsrep_server_state.h"

std::vector<st_mysql_sys_var*> sysvars;
std::map<st_mysql_sys_var*, wsrep::provider_options::option*> var_to_opt;

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
  auto varopt= var_to_opt.find(var);
  if (varopt == var_to_opt.end())
  {
    WSREP_ERROR("Could not match var to option");
    return 1;
  }
  int len= 0;
  const char* new_value= value->val_str(value, 0, &len);
  if (options->set(varopt->second->name(), new_value)) return 1;
  *((const char**)save)= varopt->second->value();
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

  auto varopt= var_to_opt.find(var);
  if (varopt == var_to_opt.end())
  {
    WSREP_ERROR("Could not match var to option");
    my_error(ER_UNKNOWN_ERROR, MYF(0));
    return;
  }
  /*
    If the value is changed to default, save will contain pointer to
    default.
   */
  if (*(char**)save == varopt->second->default_value())
  {
    if (options->set(varopt->second->name(), varopt->second->default_value()))
    {
      WSREP_WARN("Could not set provider option to default value: %s",
                 varopt->second->default_value());
      my_error(ER_WRONG_VALUE_FOR_VAR,
               MYF(0),
               varopt->second->name(),
               varopt->second->default_value());
      return;
    }
  }
  *((char**)var_ptr) = *(char**)save;
}
char *wsrep_dummy= 0;
/* Prototype for system variables */
MYSQL_SYSVAR_STR(wsrep_provider_sysvar_proto,
                 wsrep_dummy,
                 0,
                 "Wsrep provider option",
                 wsrep_provider_sysvar_check,
                 wsrep_provider_sysvar_update, "");

static void wsrep_plugin_append_var(wsrep::provider_options::option* opt)
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
  sysvars.push_back(var);
  var_to_opt.insert(std::make_pair(var, opt));
}

static char* wsrep_plugin_get_var_value_ptr(st_mysql_sys_var* var)
{
  size_t val_ptr_off= (char*)&mysql_sysvar_wsrep_provider_sysvar_proto.value -
    (char*)&mysql_sysvar_wsrep_provider_sysvar_proto;
  char** ptr= (char**)((char*)var + val_ptr_off);
  if (ptr) return *ptr;
  return 0;
}

static void wsrep_plugin_free_var(st_mysql_sys_var* var)
{

  my_free(wsrep_plugin_get_var_value_ptr(var));
  my_free(var);
}

static int wsrep_plugin_init(void *p)
{
  WSREP_DEBUG("wsrep_plugin_init()");
  return 0;
}

static int wsrep_plugin_deinit(void *p)
{
  WSREP_DEBUG("wsrep_plugin_deinit()");
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
},
{
  MYSQL_REPLICATION_PLUGIN,
  &wsrep_plugin,
  "wsrep_provider",
  "Codership Oy",
  "Wsrep provider plugin",
  PLUGIN_LICENSE_GPL,
  wsrep_provider_plugin_init,
  wsrep_provider_plugin_deinit,
  0x0100,
  NULL, /* Status variables */
  /* System variables, this will be assigned after provider is loaded
   * first time. */
  NULL,
  "1.0", /* Version (string) */
  MariaDB_PLUGIN_MATURITY_ALPHA     /* Maturity */
}
maria_declare_plugin_end;

int wsrep_plugin_init_provider_options()
{
  auto options= Wsrep_server_state::get_options();
  if (!options)
  {
    WSREP_ERROR("Provider options not initialized before provider plugin init");
    return 1;
  }
  options->for_each([](wsrep::provider_options::option* opt)
                    {
                      wsrep_plugin_append_var(opt);
                    });
  sysvars.push_back(0);
  builtin_maria_wsrep_plugin[1].system_vars= &sysvars[0];
  return 0;
};

void wsrep_plugin_deinit_provider_options()
{
  for (auto i : sysvars)
  {
    if (i) wsrep_plugin_free_var(i);
  }
  sysvars.clear();
}
