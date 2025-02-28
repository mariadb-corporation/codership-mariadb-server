/* Copyright 2018-2022 Codership Oy <info@codership.com>

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

#include "my_global.h"
#include "wsrep_api.h"
#include "wsrep_server_state.h"
#include "wsrep_allowlist_service.h"
#include "wsrep_event_service.h"
#include "wsrep_binlog.h" /* init/deinit group commit */
#include "wsrep_plugin.h" /* make/destroy sysvar helpers */
#include "my_getopt.h" /* handle_options() */
#include <string>

mysql_mutex_t LOCK_wsrep_server_state;
mysql_cond_t  COND_wsrep_server_state;

#ifdef HAVE_PSI_INTERFACE
PSI_mutex_key key_LOCK_wsrep_server_state;
PSI_cond_key  key_COND_wsrep_server_state;
#endif

wsrep::provider::services Wsrep_server_state::m_provider_services;

Wsrep_server_state* Wsrep_server_state::m_instance;
std::vector<st_mysql_sys_var*> Wsrep_server_state::m_sysvars;

static const char* provider_options_prefix="wsrep-provider-";
static int provider_options_prefix_length=15;

Wsrep_server_state::Wsrep_server_state(const std::string& name,
                                       const std::string& incoming_address,
                                       const std::string& address,
                                       const std::string& working_dir,
                                       const wsrep::gtid& initial_position,
                                       int max_protocol_version)
  : wsrep::server_state(m_mutex,
                        m_cond,
                        m_service,
                        NULL,
                        name,
                        incoming_address,
                        address,
                        working_dir,
                        initial_position,
                        max_protocol_version,
                        wsrep::server_state::rm_sync)
  , m_mutex(&LOCK_wsrep_server_state)
  , m_cond(&COND_wsrep_server_state)
  , m_service(*this)
{ }

Wsrep_server_state::~Wsrep_server_state() = default;

void Wsrep_server_state::init_once(const std::string& name,
                                   const std::string& incoming_address,
                                   const std::string& address,
                                   const std::string& working_dir,
                                   const wsrep::gtid& initial_position,
                                   int max_protocol_version)
{
  if (m_instance == 0)
  {
    mysql_mutex_init(key_LOCK_wsrep_server_state, &LOCK_wsrep_server_state,
                     MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_COND_wsrep_server_state, &COND_wsrep_server_state, 0);
    m_instance = new Wsrep_server_state(name,
                                        incoming_address,
                                        address,
                                        working_dir,
                                        initial_position,
                                        max_protocol_version);
  }
}

int Wsrep_server_state::init_provider(const std::string& provider,
                                      const std::string& options)
{
  DBUG_ASSERT(m_instance);
  int ret= m_instance->load_provider(provider, options);
  if (ret)
  {
    WSREP_ERROR("Failed to load provider %s with options %s",
                provider.c_str(), options.c_str());
    return ret;
  }
  return 0;
}

struct st_mysql_sys_var
{
  MYSQL_PLUGIN_VAR_HEADER;
};

static void my_option_init(struct my_option &my_opt,
                           void* app_type,
                           struct st_mysql_sys_var *var)
{
  std::string option_name(provider_options_prefix);
  option_name.append(var->name);
  for (size_t i= 0; i < option_name.size(); ++i)
    if (option_name[i] == '_')
      option_name[i]= '-';
  my_opt.name= my_strdup(PSI_INSTRUMENT_ME, option_name.c_str(), MYF(0));
  my_opt.id= 0;
  plugin_opt_set_limits(&my_opt, var);
  my_opt.value= my_opt.u_max_value= *(uchar ***) (var + 1);
  my_opt.deprecation_substitute= NULL;
  my_opt.block_size= 0;
  my_opt.app_type= app_type;
}

static void my_option_deinit(struct my_option &my_opt)
{
  if (my_opt.name)
    my_free((void*)my_opt.name);
}

static void make_my_options(std::vector<struct my_option> &my_options,
                            std::vector<struct st_mysql_sys_var*> sysvars,
                            void* app_type)
{
  for (struct st_mysql_sys_var* var : sysvars) {
    if (var) {
      struct my_option my_opt;
      my_option_init(my_opt, app_type, var);
      my_options.push_back(my_opt);
    }
  }
  struct my_option null_opt;
  null_opt.name= NULL;
  my_options.push_back(null_opt);
}

// Convert plugin option name to provider option name.
// For example, given 'wsrep-provider-repl-max-ws-size'
// return 'repl.max_ws_size'
static std::string option_name_to_provider_name(const char *name)
{
  if (strncmp(name, provider_options_prefix,
              provider_options_prefix_length) != 0)
  {
    assert(0);
    return "";
  }

  std::string option(name + provider_options_prefix_length);
  size_t pos= option.find('-');
  if (pos == std::string::npos)
  {
    assert(0);
    return "";
  }

  // Replace the first '-' with a '.'
  option[pos]= '.';

  // Replace the remaining '-' with '_'
  for (size_t i= pos + 1; i < option.length(); ++i)
    if (option[i] == '-')
      option[i]= '_';

  return option;
}

std::string make_provider_option_string(const char* name, const char* value)
{
  std::string option_string(option_name_to_provider_name(name));
  option_string.append("=");
  option_string.append(value);
  return option_string;
}

my_bool option_changed(const struct my_option *opt, const char *value,
                       const char *filename)
{
  std::string* extra_options= (std::string*)opt->app_type;
  std::string option_string(make_provider_option_string(opt->name, value));
  extra_options->append(";");
  extra_options->append(option_string);
  return 0;
}

static void parse_config_params(std::vector<struct st_mysql_sys_var *> sysvars,
                                std::string &extra_options)
{
  int argc=orig_argc;
  char **argv=orig_argv;

  if (load_defaults(MYSQL_CONFIG_NAME, load_default_groups, &argc, &argv))
  {
    assert(0);
    return;
  }
  char** defaults_argv= argv;

  std::vector<struct my_option> my_options;
  make_my_options(my_options, sysvars, &extra_options);
  my_getopt_skip_unknown= 1; // reset to original value
  int error= handle_options(&argc, &argv, &my_options[0], option_changed);
  if (error)
  {
    WSREP_ERROR("handle_options failed");
    assert(0);
  }

  for (struct my_option &opt : my_options)
    my_option_deinit(opt);
  if (argv)
    free_defaults(defaults_argv);
}

int Wsrep_server_state::init_options(std::string& extra_options)
{
  if (!m_instance)
    return 1;
  auto options= m_instance->provider_options();
  options->for_each([](wsrep::provider_options::option *opt) {
    struct st_mysql_sys_var *var= wsrep_make_sysvar_for_option(opt);
    m_sysvars.push_back(var);
  });
  m_sysvars.push_back(nullptr);
  wsrep_provider_plugin_set_sysvars(&m_sysvars[0]);
  parse_config_params(m_sysvars, extra_options);
  return 0;
}

void Wsrep_server_state::deinit_provider()
{
  m_instance->unload_provider();
}

void Wsrep_server_state::destroy()
{
  if (m_instance)
  {
    delete m_instance;
    m_instance= 0;
    mysql_mutex_destroy(&LOCK_wsrep_server_state);
    mysql_cond_destroy(&COND_wsrep_server_state);
    for (auto var : m_sysvars)
    {
      if (var)
      {
        wsrep_destroy_sysvar(var);
      }
    }
    m_sysvars.clear();
  }
}

void Wsrep_server_state::init_provider_services()
{
  m_provider_services.allowlist_service= wsrep_allowlist_service_init();
  m_provider_services.event_service= Wsrep_event_service::instance();
}

void Wsrep_server_state::deinit_provider_services()
{
  if (m_provider_services.allowlist_service)
    wsrep_allowlist_service_deinit();
  m_provider_services= wsrep::provider::services();
}

