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

#pragma once

#include "expr/term_manager.h"
#include "expr/gc_participant.h"
#include "system/state_type.h"

#include <iosfwd>

namespace sally {
namespace system {

/**
 * A formula where over a state type.
 */
class state_formula : expr::gc_participant {

  /** The state variables */
  const state_type* d_state_type;

  /** The formula itself */
  expr::term_ref_strong d_state_formula;

public:

  /**
   * Construct a state formula over the state type with the explicit formula.
   * It also registers itself with the state type.
   */
  state_formula(expr::term_manager& tm, const state_type* st, expr::term_ref formula);

  /**
   * Construct a copy of the given state formula.
   */
  state_formula(const state_formula* f);

  /** Destruct and unregister. */
  ~state_formula();

  /** Get the state formula */
  expr::term_ref get_formula() const {
    return d_state_formula;
  }

  /** Get the state type */
  const state_type* get_state_type() const {
    return d_state_type;
  }

  /** Print it to the stream */
  void to_stream(std::ostream& out) const;

  /** GC */
  void gc_collect(const expr::gc_relocator& gc_reloc);

};

std::ostream& operator << (std::ostream& out, const state_formula& sf);

}
}
