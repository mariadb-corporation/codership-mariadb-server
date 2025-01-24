#include "wsrep_key_check.h"
#include "my_global.h"
#include "sql_class.h"
#include "mdl.h"

static int check_key_for_ticket(MDL_ticket *mdl_ticket, void *arg,
                                bool granted)
{
  THD *thd= (THD *) arg;
  MDL_context *mdl_ctx= mdl_ticket->get_ctx();
  MDL_key *mdl_key= mdl_ticket->get_key();

  if (!granted)
    return 0;

  if (mdl_key->mdl_namespace() != MDL_key::TABLE)
    return 0;

  if (thd != mdl_ctx->get_thd())
    return 0;

  if (std::strncmp("performance_schema", mdl_key->db_name(),
                   mdl_key->db_name_length()) == 0)
    return 0;

  if (std::strncmp("gtid_slave_pos", mdl_key->name(),
                   mdl_key->name_length()) == 0)
    return 0;

  wsrep::key key(wsrep::key::shared);
  key.append_key_part(mdl_key->db_name(), mdl_key->db_name_length());
  key.append_key_part(mdl_key->name(), mdl_key->name_length());

  if (!thd->wsrep_trx().has_key(key))
  {
    WSREP_WARN("No certification key for MDL lock "
               "db: %s name: %s type: %s query: %s",
               mdl_key->db_name(), mdl_key->name(),
               mdl_ticket->get_type_name()->str, thd->query());
  }

  return 0;
}

void wsrep_check_keys(THD *thd)
{
  mdl_iterate(check_key_for_ticket, thd);
}
