/* Copyright (C) 2013 Codership Oy <info@codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA. */

#include "mariadb.h"
#include "mysql/service_wsrep.h"
#include "wsrep_binlog.h"
#include "log.h"
#include "slave.h"
#include "log_event.h"
#include "wsrep_applier.h"
#include "wsrep_mysqld.h"
#include "transaction.h"

class wsrep_iocache_writer
{
public:
  wsrep_iocache_writer() {}
  virtual ~wsrep_iocache_writer() {}
  virtual int write(const unsigned char *data, size_t length)= 0;
  virtual void cleanup_after_error() {}
};

class wsrep_iocache_buffer_writer : public wsrep_iocache_writer
{
public:
  wsrep_iocache_buffer_writer(wsrep::mutable_buffer &buffer) : buffer(buffer)
  {
  }
  virtual int write(const unsigned char *data, size_t length)
  {
    buffer.push_back(reinterpret_cast<const char *>(data),
                     reinterpret_cast<const char *>(data + length));
    return 0;
  }
  virtual void cleanup_after_error() { buffer.clear(); }

private:
  wsrep::mutable_buffer &buffer;
};

class wsrep_iocache_provider_writer : public wsrep_iocache_writer
{
public:
  wsrep_iocache_provider_writer(THD *thd) : thd(thd) {}
  virtual int write(const unsigned char *data, size_t length)
  {
    return thd->wsrep_cs().append_data(wsrep::const_buffer(data, length));
  }

private:
  THD *thd;
};

/*
  Write the contents of a cache to wsrep provider.

  This function quite the same as MYSQL_BIN_LOG::write_cache(),
  with the exception that here we write to a wsrep_io_cache_writer
  instead of log file.

  This version uses incremental data appending as it reads it from cache.
*/
static int wsrep_write_cache_inc(IO_CACHE *const cache, size_t &offset,
                                 wsrep_iocache_writer &writer)
{
  DBUG_ENTER("wsrep_write_cache_inc");
  my_off_t const saved_pos(my_b_tell(cache));

  if (reinit_io_cache(cache, READ_CACHE, offset, 0, 0))
  {
    WSREP_ERROR("failed to initialize io-cache");
    DBUG_RETURN(1);
  }

  int ret= 0;
  size_t total_length(0);

  uint length(my_b_bytes_in_cache(cache));
  if (unlikely(0 == length)) length= my_b_fill(cache);

  if (likely(length > 0))
  {
    do
    {
      total_length += length;
      /* bail out if buffer grows too large
         not a real limit on a writeset size which includes other things
         like header and keys.
      */
      if (unlikely(total_length > wsrep_max_ws_size))
      {
        WSREP_WARN("transaction size limit (%lu) exceeded: %zu",
                   wsrep_max_ws_size, total_length);
        ret= 1;
        goto cleanup;
      }
      if (writer.write(cache->read_pos, length))
      {
        ret= 1;
        goto cleanup;
      }
      cache->read_pos= cache->read_end;
    } while ((cache->file >= 0) && (length= my_b_fill(cache)));
    if (ret == 0)
    {
      assert(total_length + offset == saved_pos);
    }
  }

  offset= saved_pos;
cleanup:
  if (ret)
  {
    writer.cleanup_after_error();
  }
  if (reinit_io_cache(cache, WRITE_CACHE, saved_pos, 0, 0))
  {
    WSREP_ERROR("failed to reinitialize io-cache");
  }
  DBUG_RETURN(ret);
}

int wsrep_write_cache_buf(IO_CACHE *cache, wsrep::mutable_buffer &buffer)
{
  size_t offset= 0;
  wsrep_iocache_buffer_writer writer(buffer);
  return wsrep_write_cache_inc(cache, offset, writer);
}

int wsrep_write_cache(THD *thd, IO_CACHE *const cache, size_t &offset)
{
  wsrep_iocache_provider_writer writer(thd);
  return wsrep_write_cache_inc(cache, offset, writer);
}

static int wsrep_write_cache_data(THD *thd, size_t &offset,
                                  wsrep_iocache_writer &writer,
                                  bool is_transactional)
{
  const bool stmt_end= true;
  thd->binlog_flush_pending_rows_event(stmt_end, is_transactional);
  IO_CACHE *cache= wsrep_get_cache(thd, is_transactional);
  if (cache && wsrep_write_cache_inc(cache, offset, writer))
  {
    return 1;
  }
  return 0;
}

int wsrep_prepare_data_for_replication(THD *thd, size_t &offset,
                                       bool is_transactional)
{
  wsrep_iocache_provider_writer writer(thd);
  return wsrep_write_cache_data(thd, offset, writer, is_transactional);
}

int wsrep_prepare_fragment_for_replication(THD *thd,
                                           wsrep::mutable_buffer &buffer,
                                           size_t &offset,
                                           bool is_transactional)
{
  wsrep_iocache_buffer_writer writer(buffer);
  return wsrep_write_cache_data(thd, offset, writer, is_transactional);
}

size_t wsrep_get_binlog_cache_size(THD *thd, bool is_transactional)
{
  IO_CACHE *cache= wsrep_get_cache(thd, is_transactional);
  if (cache)
  {
    size_t pending_rows_event_length= 0;
    if (Rows_log_event *ev=
            thd->binlog_get_pending_rows_event(is_transactional))
    {
      pending_rows_event_length= ev->get_data_size();
    }
    return my_b_tell(cache) + pending_rows_event_length;
  }
  return 0;
}

void wsrep_dump_rbr_buf(THD *thd, const void* rbr_buf, size_t buf_len)
{
  int len= snprintf(NULL, 0, "%s/GRA_%lld_%lld.log",
                    wsrep_data_home_dir, (longlong) thd->thread_id,
                    (longlong) wsrep_thd_trx_seqno(thd));
  if (len < 0)
  {
    WSREP_ERROR("snprintf error: %d, skipping dump.", len);
    return;
  }
  /*
    len doesn't count the \0 end-of-string. Use len+1 below
    to alloc and pass as an argument to snprintf.
  */

  char *filename= (char *)malloc(len+1);
  int len1= snprintf(filename, len+1, "%s/GRA_%lld_%lld.log",
                    wsrep_data_home_dir, (longlong) thd->thread_id,
                    (long long)wsrep_thd_trx_seqno(thd));

  if (len > len1)
  {
    WSREP_ERROR("RBR dump path truncated: %d, skipping dump.", len);
    free(filename);
    return;
  }

  FILE *of= fopen(filename, "wb");

  if (of)
  {
    if (fwrite(rbr_buf, buf_len, 1, of) == 0)
       WSREP_ERROR("Failed to write buffer of length %llu to '%s'",
                   (unsigned long long)buf_len, filename);

    fclose(of);
  }
  else
  {
    WSREP_ERROR("Failed to open file '%s': %d (%s)",
                filename, errno, strerror(errno));
  }
  free(filename);
}

/* Dump replication buffer along with header to a file. */
void wsrep_dump_rbr_buf_with_header(THD *thd, const void *rbr_buf,
                                    size_t buf_len)
{
  DBUG_ENTER("wsrep_dump_rbr_buf_with_header");

  File file;
  IO_CACHE cache;
  Log_event_writer writer(&cache, 0);
  Format_description_log_event *ev= 0;

  longlong thd_trx_seqno= (long long)wsrep_thd_trx_seqno(thd);
  int len= snprintf(NULL, 0, "%s/GRA_%lld_%lld_v2.log",
                    wsrep_data_home_dir, (longlong)thd->thread_id,
                    thd_trx_seqno);
  /*
    len doesn't count the \0 end-of-string. Use len+1 below
    to alloc and pass as an argument to snprintf.
  */
  char *filename;
  if (len < 0 || !(filename= (char*)malloc(len+1)))
  {
    WSREP_ERROR("snprintf error: %d, skipping dump.", len);
    DBUG_VOID_RETURN;
  }

  int len1= snprintf(filename, len+1, "%s/GRA_%lld_%lld_v2.log",
                     wsrep_data_home_dir, (longlong) thd->thread_id,
                     thd_trx_seqno);

  if (len > len1)
  {
    WSREP_ERROR("RBR dump path truncated: %d, skipping dump.", len);
    free(filename);
    DBUG_VOID_RETURN;
  }

  if ((file= mysql_file_open(key_file_wsrep_gra_log, filename,
                             O_RDWR | O_CREAT | O_BINARY, MYF(MY_WME))) < 0)
  {
    WSREP_ERROR("Failed to open file '%s' : %d (%s)",
                filename, errno, strerror(errno));
    goto cleanup1;
  }

  if (init_io_cache(&cache, file, 0, WRITE_CACHE, 0, 0, MYF(MY_WME | MY_NABP)))
  {
    goto cleanup2;
  }

  if (my_b_safe_write(&cache, BINLOG_MAGIC, BIN_LOG_HEADER_SIZE))
  {
    goto cleanup2;
  }

  /*
    Instantiate an FDLE object for non-wsrep threads (to be written
    to the dump file).
  */
  ev= (thd->wsrep_applier) ? wsrep_get_apply_format(thd) :
    (new Format_description_log_event(4));

  if (writer.write(ev) || my_b_write(&cache, (uchar*)rbr_buf, buf_len) ||
      flush_io_cache(&cache))
  {
    WSREP_ERROR("Failed to write to '%s'.", filename);
    goto cleanup2;
  }

cleanup2:
  end_io_cache(&cache);

cleanup1:
  free(filename);
  mysql_file_close(file, MYF(MY_WME));

  if (!thd->wsrep_applier) delete ev;

  DBUG_VOID_RETURN;
}

int wsrep_write_skip_event(THD* thd)
{
  DBUG_ENTER("wsrep_write_skip_event");
  Ignorable_log_event skip_event(thd);
  int ret= mysql_bin_log.write_event(&skip_event);
  if (ret)
  {
    WSREP_WARN("wsrep_write_skip_event: write to binlog failed: %d", ret);
  }
  if (!ret && (ret= trans_commit_stmt(thd)))
  {
    WSREP_WARN("wsrep_write_skip_event: statt commit failed");
  }
  DBUG_RETURN(ret);
}

int wsrep_write_dummy_event_low(THD *thd, const char *msg)
{
  ::abort();
  return 0;
}

int wsrep_write_dummy_event(THD *orig_thd, const char *msg)
{
  return 0;
}

bool wsrep_commit_will_write_binlog(THD *thd)
{
  return (!wsrep_emulate_bin_log && /* binlog enabled*/
          (wsrep_thd_is_local(thd) || /* local thd*/
           (thd->wsrep_applier_service && /* applier and log-slave-updates */
            opt_log_slave_updates)));
}

/*
  The last THD/commit_for_wait registered for group commit.
*/
static wait_for_commit *commit_order_tail= NULL;

void wsrep_register_for_group_commit(THD *thd)
{
  DBUG_ENTER("wsrep_register_for_group_commit");
  if (wsrep_emulate_bin_log)
  {
    /* Binlog is off, no need to maintain group commit queue */
    DBUG_VOID_RETURN;
  }

  DBUG_ASSERT(thd->wsrep_trx().ordered());

  wait_for_commit *wfc= thd->wait_for_commit_ptr= &thd->wsrep_wfc;

  mysql_mutex_lock(&LOCK_wsrep_group_commit);
  if (commit_order_tail)
  {
    wfc->register_wait_for_prior_commit(commit_order_tail);
  }
  commit_order_tail= thd->wait_for_commit_ptr;
  mysql_mutex_unlock(&LOCK_wsrep_group_commit);

  /*
    Now we have queued for group commit. If the commit will go
    through TC log_and_order(), the commit ordering is done
    by TC group commit. Otherwise the wait for prior
    commits to complete is done in ha_commit_one_phase().
  */
  DBUG_VOID_RETURN;
}

void wsrep_unregister_from_group_commit(THD *thd)
{
  DBUG_ASSERT(thd->wsrep_trx().ordered());
  wait_for_commit *wfc= thd->wait_for_commit_ptr;

  if (wfc)
  {
    mysql_mutex_lock(&LOCK_wsrep_group_commit);
    wfc->unregister_wait_for_prior_commit();
    thd->wakeup_subsequent_commits(0);

    /* The last one queued for group commit has completed commit, it is
       safe to set tail to NULL. */
    if (wfc == commit_order_tail)
      commit_order_tail= NULL;
    mysql_mutex_unlock(&LOCK_wsrep_group_commit);
    thd->wait_for_commit_ptr= NULL;
  }
}
