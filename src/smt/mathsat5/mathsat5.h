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

#ifdef WITH_MATHSAT5

#include "smt/solver.h"

namespace sally {
namespace smt {

class mathsat5_internal;

/**
 * Yices SMT solver.
 */
class mathsat5 : public solver {

  /** Internal yices data */
  mathsat5_internal* d_internal;

public:

  /** Constructor */
  mathsat5(expr::term_manager& tm, const options& opts, utils::statistics& stats);

  /** Destructor */
  ~mathsat5();

  /** Features */
  bool supports(feature f) const;

  /** Add an assertion f to the solver */
  void add(expr::term_ref f, formula_class f_class);

  /** Check the assertions for satisfiability */
  result check();

  /** Check the model (debug) */
  void check_model();

  /** Get the model */
  expr::model::ref get_model() const;

  /** Push the solving context */
  void push();

  /** Pop the solving context */
  void pop();

  /** Get the scope */
  int get_scope() const;

  /** Generalize the last sat result using quantifier elimination. */
  void generalize(generalization_type type, std::vector<expr::term_ref>& out);

  /** Interpolate the last UNSAT result */
  void interpolate(std::vector<expr::term_ref>& out);

  /** Unsat core of the last UNSAT result */
  void get_unsat_core(std::vector<expr::term_ref>& out);

  /** Get the current assertions */
  void get_assertions(std::vector<expr::term_ref>& out) const;

  /** Collect terms */
  void gc_collect(const expr::gc_relocator& gc_reloc);

  /** Collect garbage */
  void gc();
};

}
}

#endif
