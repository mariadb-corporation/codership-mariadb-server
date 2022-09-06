/* Copyright (C) 2022  Codership Oy <info@galeracluster.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
   USA.
*/

#ifndef MYSQL_SERVER
#define MYSQL_SERVER
#endif

#include <my_global.h>
#include <mysql/plugin.h>

#include "field.h"
#include "table.h"
// #include "mysqld_thd_manager.h"
#include "sql_class.h"
#include "sql_show.h"
#include "mysql/service_wsrep.h"

namespace {
static struct st_mysql_information_schema wsrep_thd_info_view=
{MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION};

enum columns
{
  COLUMN_ID = 0,
  COLUMN_THD_PTR,
  COLUMN_OS_THREAD_ID,
  COLUMN_CLIENT_MODE,
  COLUMN_CLIENT_STATE,
  COLUMN_TRANSACTION_STATE,
  COLUMN_SEQNO,
  COLUMN_DEPENDS_ON
};
using namespace Show;
static ST_FIELD_INFO wsrep_thd_info_view_fields[]=
{
  Column{"ID", ULong(), NOT_NULL, "Id"},
  Column{"THD_PTR", Varchar(18), NOT_NULL, "Thd_Ptr"},
  Column{"OS_THREAD_ID", ULonglong(), NOT_NULL, "Os_Thread_Id"},
  Column{"CLIENT_MODE", Varchar(16), NOT_NULL, "Client_Mode"},
  Column{"CLIENT_STATE", Varchar(16), NOT_NULL, "Client_State"},
  Column{"TRANSACTION_STATE", Varchar(16), NOT_NULL, "Transaction_State"},
  Column{"SEQNO", SLonglong(), NOT_NULL, "Seqno"},
  Column{"DEPENDS_ON", SLonglong(), NOT_NULL, "Depends_On"},
  CEnd{}
};
}

struct Wsrep_thd_info_fill_arg
{
  THD *caller_thd;
  TABLE *table;
};

static my_bool wsrep_thd_info_fill_action(THD *thd,
                                          Wsrep_thd_info_fill_arg *fill_arg)
{
  if (!WSREP(thd))
  {
    return FALSE;
  }
  THD *caller_thd= fill_arg->caller_thd;
  TABLE *table= fill_arg->table;

  table->field[COLUMN_ID]->store(thd->thread_id, true);
  char thd_ptr_str[19]= {
      0,
  };
  snprintf(thd_ptr_str, sizeof(thd_ptr_str), "%p", thd);
  table->field[COLUMN_THD_PTR]->store(thd_ptr_str, strlen(thd_ptr_str),
                                      system_charset_info);
  table->field[COLUMN_OS_THREAD_ID]->store((ulong) thd->real_id, true);
  const char *client_mode= wsrep_thd_client_mode_str(thd);
  table->field[COLUMN_CLIENT_MODE]->store(client_mode, strlen(client_mode),
                                          system_charset_info);
  const char *client_state= wsrep_thd_client_state_str(thd);
  table->field[COLUMN_CLIENT_STATE]->store(client_state, strlen(client_state),
                                           system_charset_info);
  const char *transaction_state= wsrep_thd_transaction_id(thd) > 0
                                     ? wsrep_thd_transaction_state_str(thd)
                                     : "none";
  table->field[COLUMN_TRANSACTION_STATE]->store(
      transaction_state, strlen(transaction_state), system_charset_info);
  table->field[COLUMN_SEQNO]->store(wsrep_thd_trx_seqno(thd), 0);
  table->field[COLUMN_DEPENDS_ON]->store(wsrep_thd_depends_on(thd), 0);
  if (schema_table_store_record(caller_thd, table))
  {
    return TRUE;
  }

  return FALSE;
}

/**
   Function to fill information_schema.wsrep_thd_info.

   @param thd THD handle
   @param tables Handle to information_schema.wsrep_thd_info
   @param cond Ignored
 */
static int wsrep_thd_info_fill_view(THD *thd,
                                    TABLE_LIST *tables,
                                    Item * /* cond */)
{
  TABLE *table= tables->table;
  Wsrep_thd_info_fill_arg arg{thd, table};
  server_threads.iterate(wsrep_thd_info_fill_action, &arg);
  return 0;
}

static int wsrep_thd_info_init(void *ptr)
{
  ST_SCHEMA_TABLE *schema_table= static_cast<ST_SCHEMA_TABLE *>(ptr);
  schema_table->fields_info= wsrep_thd_info_view_fields;
  schema_table->fill_table= wsrep_thd_info_fill_view;
  return 0;
}

mysql_declare_plugin(wsrep_thd_info)
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,     /* plugin type */
    &wsrep_thd_info_view,              /* descriptor */
    "WSREP_THD_INFO",                  /* plugin name */
    "Codership Oy",                    /* author       */
    "Wsrep thread state information",  /* description */
    PLUGIN_LICENSE_GPL,                /* license */
    wsrep_thd_info_init,               /* plugin initializer */
    NULL,                              /* plugin deinitializer */
    0x0001,                            /* version */
    NULL,                              /* status variables */
    NULL,                              /* system variables */
    NULL,                              /* reserved */
    0                                  /* flags */
}
mysql_declare_plugin_end;
