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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA */

/*
  Wsrep plugin comes in two parts, wsrep_plugin and wsrep_provider_plugin.

  If plugin-wsrep-provider=ON, wsrep_provider_options variable is disabled,
  in favor of single options which are initialized from provider.
*/

#include "wsrep_plugin.h"
#include "sql_plugin.h"
#include "sql_priv.h"
#include "sql_class.h"
#include "set_var.h"

#include "my_global.h"
#include "mysqld_error.h"
#include <mysql/plugin.h>

#include "wsrep_mysqld.h"
#include "wsrep/provider_options.hpp"
#include "wsrep_server_state.h"
#include "wsrep_var.h" // wsrep_refresh_provider_options()

#ifdef WITH_WSREP
static bool provider_plugin_enabled= false;

/* Prototype for provider system variables */
static char *dummy_str= 0;
__attribute__((unused))
static MYSQL_SYSVAR_STR(proto_string, dummy_str, 0, 0, 0, 0, "");


bool wsrep_provider_plugin_enabled()
{
  return provider_plugin_enabled;
}

/* Returns the name of the variable without prefix */
static const char *sysvar_name(struct st_mysql_sys_var *var)
{
  const char *var_name= ((decltype(mysql_sysvar_proto_string) *) var)->name;
  long unsigned int prefix_len= sizeof("wsrep_provider_") - 1;
  return &var_name[prefix_len];
}

/* Returns option corresponding to the given sysvar */
static const wsrep::provider_options::option *
sysvar_to_option(struct st_mysql_sys_var *var)
{
  auto options= Wsrep_server_state::get_options();
  if (!options)
  {
    return nullptr;
  }
  return options->get_option(sysvar_name(var));
}

/* Make a boolean option value */
static std::unique_ptr<wsrep::provider_options::option_value>
make_option_value(my_bool value)
{
  return std::unique_ptr<wsrep::provider_options::option_value>(
      new wsrep::provider_options::option_value_bool(value));
}

/* Make a string option value */
static std::unique_ptr<wsrep::provider_options::option_value>
make_option_value(const char *value)
{
  return std::unique_ptr<wsrep::provider_options::option_value>(
      new wsrep::provider_options::option_value_string(value));
}

/* Make a integer option value */
static std::unique_ptr<wsrep::provider_options::option_value>
make_option_value(long long value)
{
  return std::unique_ptr<wsrep::provider_options::option_value>(
      new wsrep::provider_options::option_value_int(value));
}

/* Make a double option value */
static std::unique_ptr<wsrep::provider_options::option_value>
make_option_value(double value)
{
  return std::unique_ptr<wsrep::provider_options::option_value>(
      new wsrep::provider_options::option_value_double(value));
}

/* Helper to get the actual value out of option_value */
template <class T>
static T get_option_value(wsrep::provider_options::option_value *value)
{
  return *((T *) value->get_ptr());
}

/* Same as above, specialized for strings */
template <>
char *get_option_value(wsrep::provider_options::option_value *value)
{
  return (char *) value->get_ptr();
}

/* Update function for sysvars */
template <class T>
static void wsrep_provider_sysvar_update(THD *thd,
                                         struct st_mysql_sys_var *var,
                                         void *var_ptr, const void *save)
{
  auto opt= sysvar_to_option(var);
  if (!opt)
  {
    WSREP_ERROR("Could not match var to option");
    my_error(ER_UNKNOWN_ERROR, MYF(0));
    return;
  }

  T new_value= *((T *) save);

  auto options= Wsrep_server_state::get_options();
  if (options->set(Wsrep_server_state::get_provider(), opt->name(),
                   make_option_value(new_value)))
  {
    my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), opt->name(),
             make_option_value(new_value)->as_string());
    return;
  }

  *((T *) var_ptr)= get_option_value<T>(opt->value());

  wsrep_refresh_provider_options();
}

/* Convert option flags to corresponding sysvar flags */
static int map_option_flags_to_sysvar(wsrep::provider_options::option *opt)
{
  int flags= 0;
  if (opt->flags() & wsrep::provider_options::flag::readonly)
    flags|= PLUGIN_VAR_READONLY;
  if (opt->flags() & wsrep::provider_options::flag::deprecated)
    flags|= PLUGIN_VAR_DEPRECATED;
  return flags;
}

/* Helper to construct a sysvar of type string for the given option */
static struct st_mysql_sys_var *
make_sysvar_for_string_option(wsrep::provider_options::option *opt)
{
  char *dummy= 0;
  MYSQL_SYSVAR_STR(proto_string,
                   dummy,
                   map_option_flags_to_sysvar(opt),
                   "Wsrep provider option",
                   0,
                   wsrep_provider_sysvar_update<char *>,
                   get_option_value<char *>(opt->default_value()));
  mysql_sysvar_proto_string.name= opt->name();
  char **val= (char **) my_malloc(PSI_NOT_INSTRUMENTED, sizeof(char *), MYF(0));
  *val= get_option_value<char *>(opt->value());
  mysql_sysvar_proto_string.value= val;
  struct st_mysql_sys_var *var= (struct st_mysql_sys_var *) my_malloc(
    PSI_NOT_INSTRUMENTED, sizeof(mysql_sysvar_proto_string), MYF(0));
  memcpy(var, &mysql_sysvar_proto_string, sizeof(mysql_sysvar_proto_string));
  return var;
}

/* Helper to construct a sysvar of type boolean for the given option */
static struct st_mysql_sys_var *
make_sysvar_for_bool_option(wsrep::provider_options::option *opt)
{
  my_bool dummy= 0;
  MYSQL_SYSVAR_BOOL(proto_bool,
                    dummy,
                    map_option_flags_to_sysvar(opt),
                    "Wsrep provider option",
                    0,
                    wsrep_provider_sysvar_update<my_bool>,
                    get_option_value<my_bool>(opt->default_value()));
  mysql_sysvar_proto_bool.name= opt->name();
  char *val= (char *) my_malloc(PSI_NOT_INSTRUMENTED, sizeof(char), MYF(0));
  *val= get_option_value<bool>(opt->value());
  mysql_sysvar_proto_bool.value= val;
  struct st_mysql_sys_var *var= (struct st_mysql_sys_var *) my_malloc(
    PSI_NOT_INSTRUMENTED, sizeof(mysql_sysvar_proto_bool), MYF(0));
  memcpy(var, &mysql_sysvar_proto_bool, sizeof(mysql_sysvar_proto_bool));
  return var;
}

/* Helper to construct a integer sysvar for the given option */
static struct st_mysql_sys_var *
make_sysvar_for_integer_option(wsrep::provider_options::option *opt)
{
  long long dummy= 0;
  MYSQL_SYSVAR_LONGLONG(proto_longlong,
                        dummy,
                        map_option_flags_to_sysvar(opt),
                        "Wsrep provider option",
                        0,
                        wsrep_provider_sysvar_update<long long>,
                        get_option_value<long long>(opt->default_value()),
                        std::numeric_limits<long long>::min(),
                        std::numeric_limits<long long>::max(),
                        0);
  mysql_sysvar_proto_longlong.name= opt->name();
  long long *val= (long long *) my_malloc(PSI_NOT_INSTRUMENTED, sizeof(long long), MYF(0));
  *val= get_option_value<long long>(opt->value());
  mysql_sysvar_proto_longlong.value= val;
  struct st_mysql_sys_var *var= (struct st_mysql_sys_var *) my_malloc(
    PSI_NOT_INSTRUMENTED, sizeof(mysql_sysvar_proto_longlong), MYF(0));
  memcpy(var, &mysql_sysvar_proto_longlong, sizeof(mysql_sysvar_proto_longlong));
  return var;
}

/* Helper to construct a sysvar of type double for the given option */
static struct st_mysql_sys_var *
make_sysvar_for_double_option(wsrep::provider_options::option *opt)
{
  double dummy= 0;
  MYSQL_SYSVAR_DOUBLE(proto_double,
                      dummy,
                      map_option_flags_to_sysvar(opt),
                      "Wsrep provider option",
                      0,
                      wsrep_provider_sysvar_update<double>,
                      get_option_value<double>(opt->default_value()),
                      std::numeric_limits<double>::min(),
                      std::numeric_limits<double>::max(),
                      0);
  mysql_sysvar_proto_double.name= opt->name();
  double *val= (double *) my_malloc(PSI_NOT_INSTRUMENTED, sizeof(double), MYF(0));
  *val= get_option_value<double>(opt->value());
  mysql_sysvar_proto_double.value= val;
  struct st_mysql_sys_var *var= (struct st_mysql_sys_var *) my_malloc(
    PSI_NOT_INSTRUMENTED, sizeof(mysql_sysvar_proto_double), MYF(0));
  memcpy(var, &mysql_sysvar_proto_double, sizeof(mysql_sysvar_proto_double));
  return var;
}

/* Construct a sysvar corresponding to the given provider option */
struct st_mysql_sys_var *
wsrep_make_sysvar_for_option(wsrep::provider_options::option *opt)
{
  const int type_flag= opt->flags() & wsrep::provider_options::flag_type_mask;
  switch (type_flag)
  {
  case wsrep::provider_options::flag::type_bool:
    return make_sysvar_for_bool_option(opt);
  case wsrep::provider_options::flag::type_integer:
    return make_sysvar_for_integer_option(opt);
  case wsrep::provider_options::flag::type_double:
    return make_sysvar_for_double_option(opt);
  default:
    assert(type_flag == 0);
    return make_sysvar_for_string_option(opt);
  };
}

/* Free a sysvar */
void wsrep_destroy_sysvar(struct st_mysql_sys_var *var)
{
  char **var_value= ((decltype(mysql_sysvar_proto_string) *) var)->value;
  my_free(var_value);
  my_free(var);
}

struct st_mysql_sys_var
{
  MYSQL_PLUGIN_VAR_HEADER;
};

struct my_option_arg
{
  wsrep::provider_options::option *option;
  struct st_mysql_sys_var *sysvar;
  std::string *defaults;
};

static void my_option_init(struct my_option &my_opt,
                           struct my_option_arg &my_arg)
{
  std::string option_name("wsrep-provider-");
  option_name.append(my_arg.sysvar->name);
  for (size_t i= 0; i < option_name.size(); ++i)
    if (option_name[i] == '_')
      option_name[i]= '-';
  my_opt.name= my_strdup(PSI_INSTRUMENT_ME, option_name.c_str(), MYF(0));
  my_opt.id= 0;
  plugin_opt_set_limits(&my_opt, my_arg.sysvar);
  my_opt.value= my_opt.u_max_value= *(uchar ***) (my_arg.sysvar + 1);
  my_opt.block_size= 0;
  my_opt.app_type= &my_arg;
}

static void my_option_deinit(struct my_option &my_opt)
{
  if (my_opt.name)
    my_free((void*)my_opt.name);
}

static void make_my_options(std::vector<struct my_option_arg> &my_args,
                            std::vector<struct my_option> &my_options)
{
  for (auto& arg : my_args)
  {
    struct my_option my_opt;
    my_option_init(my_opt, arg);
    my_options.push_back(my_opt);
  }
  struct my_option null_opt;
  null_opt.name= NULL;
  my_options.push_back(null_opt);
}

static void make_my_option_args(const wsrep::provider_options &options,
                                std::string &defaults,
                                std::vector<struct my_option_arg> &my_args)
{
  options.for_each([&](wsrep::provider_options::option *opt) {
    my_args.push_back({opt, wsrep_make_sysvar_for_option(opt), &defaults});
  });
}

static my_bool option_changed(const struct my_option *opt, const char *value,
                              const char *filename)
{
  my_option_arg *my_arg= (struct my_option_arg *) opt->app_type;
  if (my_arg->defaults->size())
    my_arg->defaults->append(";");
  my_arg->defaults->append(my_arg->option->real_name());
  my_arg->defaults->append("=");
  my_arg->defaults->append(value);
  return 0;
}

int wsrep_load_provider_plugin_defaults(const wsrep::provider_options &options,
                                        std::string &extra_options)
{
  int argc= orig_argc;
  char **argv= orig_argv;

  if (load_defaults(MYSQL_CONFIG_NAME, load_default_groups, &argc, &argv))
  {
    return 1;
  }
  char **defaults_argv= argv;

  std::vector<struct my_option> my_options;
  std::vector<struct my_option_arg> my_option_args;
  make_my_option_args(options, extra_options, my_option_args);
  make_my_options(my_option_args, my_options);

  my_bool skip_unknown_orig= my_getopt_skip_unknown;
  my_getopt_skip_unknown= TRUE;
  int error= handle_options(&argc, &argv, &my_options[0], option_changed);
  my_getopt_skip_unknown= skip_unknown_orig;

  for (struct my_option &opt : my_options)
    my_option_deinit(opt);
  for (struct my_option_arg &arg : my_option_args)
    wsrep_destroy_sysvar(arg.sysvar);
  if (argv)
    free_defaults(defaults_argv);

  return error;
}

static int wsrep_provider_plugin_init(void *p)
{
  WSREP_DEBUG("wsrep_provider_plugin_init()");

  if (!WSREP_ON)
  {
    sql_print_information("Plugin '%s' is disabled.", "wsrep-provider");
    return 0;
  }

  provider_plugin_enabled= true;

  // When plugin-wsrep-provider is enabled we set
  // wsrep_provider_options parameter as READ_ONLY
  sys_var *my_var= find_sys_var(current_thd, "wsrep_provider_options");
  int flags= my_var->get_flags();
  my_var->update_flags(flags |= (int)sys_var::READONLY);
  return 0;
}

static int wsrep_provider_plugin_deinit(void *p)
{
  WSREP_DEBUG("wsrep_provider_plugin_deinit()");
  sys_var *my_var= find_sys_var(current_thd, "wsrep_provider_options");
  int flags= my_var->get_flags();
  my_var->update_flags(flags &= (int)~sys_var::READONLY);
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

void wsrep_provider_plugin_set_sysvars(st_mysql_sys_var** vars)
{
  builtin_maria_wsrep_provider_plugin->system_vars= vars;
}

/*
  Wsrep plugin
*/

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
  NULL,
  NULL,
  0x0100,
  NULL, /* Status variables */
  NULL, /* System variables */
  "1.0", /* Version (string) */
  MariaDB_PLUGIN_MATURITY_STABLE     /* Maturity */
}
maria_declare_plugin_end;

#endif /* WITH_WSREP */
