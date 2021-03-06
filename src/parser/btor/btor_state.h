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

#include <map>
#include <vector>
#include <iosfwd>
#include <antlr3.h>

#include "system/context.h"
#include "command/command.h"

namespace sally {
namespace parser {

/** State attached to the parser */
class btor_state {

  /** The context */
  const system::context& d_context;

  // Map from indices to terms
  std::vector<expr::term_ref> d_terms;

  // List of variables indices
  std::vector<size_t> d_variables;

  typedef std::map<size_t, size_t> var_to_var_map;

  // Map from variables to their next versions
  var_to_var_map d_variables_next;

  // List of root nodes
  std::vector<expr::term_ref_strong> d_roots;

  /** Set the term */
  void set_term(size_t index, expr::term_ref term, size_t size);

  /** Bitvector 1 */
  expr::term_ref_strong d_one;

  /** Bitvector 0 */
  expr::term_ref_strong d_zero;

  /** Check if there is a next, or is this an input */
  bool is_register(size_t index) const;

  /** Get the next term for given index */
  expr::term_ref get_next(size_t index) const;

public:

  /** Construct the parser state */
  btor_state(const system::context& context);

  /** Returns the term manager for the parser */
  expr::term_manager& tm() const { return d_context.tm(); }

  /** Returns the context for the parser */
  const system::context& ctx() const { return d_context; }

  /** Get the string of a token begin parsed */
  static
  std::string token_text(pANTLR3_COMMON_TOKEN token);

  /** Get the int value of a token begin parsed */
  static
  int token_as_int(pANTLR3_COMMON_TOKEN token);

  /** Get the integer value of a token begin parsed */
  static
  expr::integer token_as_integer(pANTLR3_COMMON_TOKEN token, size_t base);

  /** Get the term at index (negated if negative) */
  expr::term_ref get_term(int index) const;

  /** Add a variable */
  void add_variable(size_t id, size_t size, std::string name);

  /** Add a next variable */
  void add_next_variable(size_t id, size_t size, size_t var_id, expr::term_ref value);

  /** Add a constant */
  void add_constant(size_t id, size_t size, const expr::bitvector& bv);

  /** Add a binary term */
  void add_term(size_t id, expr::term_op op, size_t size, expr::term_ref t1, expr::term_ref t2);

  /** Add a binary term */
  void add_ite(size_t id, size_t size, expr::term_ref c, expr::term_ref t_true, expr::term_ref t_false);

  /** Add an extractin/slice */
  void add_slice(size_t id, size_t size, expr::term_ref t, size_t high, size_t low);

  /** Add a root node */
  void add_root(size_t id, size_t size, expr::term_ref t1);

  /** Returns the final parse command */
  cmd::command* finalize() const;

  /** Collect terms */
  void gc_collect(const expr::gc_relocator& gc_reloc);
};

}
}
