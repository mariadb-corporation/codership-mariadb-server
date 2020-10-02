/* Copyright (C) 2015 Codership Oy <info@codership.com>

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

#ifndef WSREP_XID_H
#define WSREP_XID_H

#include <my_config.h>

#ifdef WITH_WSREP

#include "wsrep_mysqld.h"
#include "wsrep/gtid.hpp"
#include "handler.h" // XID typedef

void wsrep_xid_init(xid_t*, const wsrep::gtid&, const wsrep_server_gtid_t&);
const wsrep::id& wsrep_xid_uuid(const XID&);
wsrep::seqno wsrep_xid_seqno(const XID&);

template<typename T> T wsrep_get_SE_checkpoint();
bool wsrep_set_SE_checkpoint(const wsrep::gtid& gtid, const wsrep_server_gtid_t&);
//void wsrep_get_SE_checkpoint(XID&);             /* uncomment if needed */
//void wsrep_set_SE_checkpoint(XID&);             /* uncomment if needed */

void wsrep_sort_xid_array(XID *array, int len);


// maximum size of xid string representation returned by
// Wsrep_xid::serialize(). See serialize_xid and ser_buf_size
// in log_event.h.
static const uint WSREP_XID_SERIALIZED_SIZE=
  8 + 2 * MYSQL_XIDDATASIZE + 4 * sizeof(long) + 1;

class Wsrep_xid : public wsrep::xid
{
public:
  Wsrep_xid(const XID* xid) :
    wsrep::xid(xid->formatID,
               xid->gtrid_length,
               xid->bqual_length,
               xid->data)
  { }
  Wsrep_xid(const Wsrep_xid& xid) :
    wsrep::xid(xid.format_id_,
               xid.gtrid_len_,
               xid.bqual_len_,
               xid.data_.data())
  { }
  operator XID() const
  {
    XID xid;
    xid.set(gtrid_len_, bqual_len_, data_.data());
    xid.formatID= format_id_;
    return xid;
  }
  char* serialize();
private:
  char m_serialized[WSREP_XID_SERIALIZED_SIZE];
};

#endif /* WITH_WSREP */
#endif /* WSREP_UTILS_H */
