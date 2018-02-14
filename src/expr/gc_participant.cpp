/**
 * This file is part of sally.
 * Copyright (C) 2015 SRI International.
 *
 * Sally is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sally is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sally.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "expr/gc_participant.h"

namespace sally {
namespace expr {

gc_participant::gc_participant(term_manager& tm, bool deregister)
: d_gc_participant_tm(tm)
, d_deregister(deregister)
{
  d_gc_participant_tm.gc_register(this);
}

gc_participant::~gc_participant()
{
  if (d_deregister) {
    d_gc_participant_tm.gc_deregister(this);
  }
}

term_manager& gc_participant::tm() const {
  return d_gc_participant_tm;
}

}
}

