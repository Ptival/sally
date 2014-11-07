/*
 * smt_solver.h
 *
 *  Created on: Oct 23, 2014
 *      Author: dejan
 */

#pragma once

#include "expr/term.h"

namespace sal2 {
namespace smt {

/**
 * SMT solver interface for solving queries of the form
 *
 *  F(x, y) = A(x) & T(x, y) & B(y)
 *
 * If F is sat, we can generalize in terms of x.
 * If F is unsat, we can get an interpolant in terms of y.
 */
class solver {

  expr::term_manager& d_tm;

public:

  enum result {
    SAT,
    UNSAT,
    UNKNOWN
  };

  /** Construct with the given term manager */
  solver(expr::term_manager& tm)
  : d_tm(tm)
  {}

  virtual
  ~solver() {};

  /** Assert the formula */
  virtual
  void add(expr::term_ref f) = 0;

  /** Check for satisfiability */
  virtual
  result check() = 0;

  /** Generalize a satisfiable answer */
  virtual
  expr::term_ref generalize() = 0;

  /** Interpolate an unsatisfiable answer */
  virtual
  void interpolate(std::vector<expr::term_ref>& ) = 0;
};

inline
std::ostream& operator << (std::ostream& out, solver::result result) {
  switch (result) {
  case solver::SAT:
    out << "sat";
    break;
  case solver::UNSAT:
    out << "unsat";
    break;
  case solver::UNKNOWN:
    out << "unknown";
    break;
  }
  return out;
}

/**
 * Factory for creating SMT solvers.
 */
class factory {
public:
  /**
   * Create a solver.
   * @param id id of the solver
   * @param tm the term manager
   */
  static solver* mk_solver(std::string id, expr::term_manager& tm);
};



}
}
