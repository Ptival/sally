/*
 * bmc_engine.cpp
 *
 *  Created on: Nov 23, 2014
 *      Author: dejan
 */

#include "engine/bmc/bmc_engine.h"

#include "smt/factory.h"
#include "utils/trace.h"

#include <sstream>
#include <iostream>

namespace sally {
namespace bmc {

bmc_engine::bmc_engine(const system::context& ctx)
: engine(ctx)
, d_trace(0)
{
  // Make the solver
  d_solver = smt::factory::mk_default_solver(ctx.tm(), ctx.get_options());
}

bmc_engine::~bmc_engine() {
  delete d_solver;
  delete d_trace;
}

engine::result bmc_engine::query(const system::transition_system* ts, const system::state_formula* sf) {

  // Scope for push/pop on the solver
  smt::solver_scope scope(d_solver);
  scope.push();

  // The trace we are building
  if (d_trace) { delete d_trace; }
  d_trace = new system::state_trace(ts->get_state_type());

  // Initial states
  expr::term_ref initial_states = ts->get_initial_states();
  d_solver->add(d_trace->get_state_formula(initial_states, 0), smt::solver::CLASS_A);

  // Transition formula
  expr::term_ref transition_formula = ts->get_transition_relation();

  // The property
  expr::term_ref property = sf->get_formula();

  // The loop
  size_t bmc_min = ctx().get_options().get_unsigned("bmc-min");
  size_t bmc_max = ctx().get_options().get_unsigned("bmc-max");

  // BMC loop
  for (size_t k = 0; k <= bmc_max; ++ k) {

    // Add the variables to the solver
    std::vector<expr::term_ref> vars;
    d_trace->get_state_variables(k, vars);
    d_solver->add_x_variables(vars.begin(), vars.end());

    // Check the current unrolling
    if (k >= bmc_min) {

      MSG(1) << "BMC: checking " << k << std::endl;

      scope.push();
      expr::term_ref property_not = tm().mk_term(expr::TERM_NOT, property);
      d_solver->add(d_trace->get_state_formula(property_not, k), smt::solver::CLASS_A);
      smt::solver::result r = d_solver->check();

      MSG(1) << "BMC: got " << r << std::endl;

      // See what happened
      switch (r) {
      case smt::solver::SAT: {
        expr::model m(tm(), false);
        d_solver->get_model(m);
        d_trace->add_model(m);
        return INVALID;
      }
      case smt::solver::UNKNOWN:
        return UNKNOWN;
      case smt::solver::UNSAT:
        // No counterexample found, continue
        break;
      default:
        assert(false);
      }

      // Pop the solver
      scope.pop();
    }

    // Unroll once more
    d_solver->add(d_trace->get_transition_formula(transition_formula, k, k + 1), smt::solver::CLASS_A);
  }

  return UNKNOWN;
}

const system::state_trace* bmc_engine::get_trace() {
  return d_trace;
}

}
}
