/* Copyright 2010 Codership Oy <http://www.codership.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA
 */

//! @file declares symbols private to wsrep integration layer

#ifndef WSREP_PRIV_H
#define WSREP_PRIV_H

#include "wsrep/server_state.hpp"

// a helper function
void wsrep_notify_status(enum wsrep::server_state::state status,
                         const wsrep::view* view= 0);

#endif /* WSREP_PRIV_H */
