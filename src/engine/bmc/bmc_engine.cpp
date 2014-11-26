/*
 * bmc_engine.cpp
 *
 *  Created on: Nov 23, 2014
 *      Author: dejan
 */

#include "engine/bmc/bmc_engine.h"

#include "smt/solver.h"

#include <sstream>

namespace sal2 {
namespace bmc {

bmc_engine::bmc_engine(const system::context& ctx)
: engine(ctx)
{
  // Make the solver
  d_solver = smt::factory::mk_solver("generic", ctx.tm());
}

bmc_engine::~bmc_engine() {
  delete d_solver;
}

expr::term_ref bmc_engine::state_variables(unsigned k, expr::term_ref type) {
  // Make sure we're using the right variables
  if (d_state_type != type) {
    d_state_variables.clear();
    d_state_type = type;
  }
  // Ensure we have enough
  while (d_state_variables.size() <= k) {
    std::stringstream ss;
    ss << "s" << d_state_variables.size();
    expr::term_ref var = tm().mk_variable(ss.str(), type);
    d_state_variables.push_back(expr::term_ref_strong(tm(), var));
  }
  // Return the variable
  return d_state_variables[k];
}

expr::term_ref bmc_engine::replace_vars(expr::term_ref f, expr::term_ref from, expr::term_ref to) {
  // Setup the substitution map
  expr::term_manager::substitution_map subst;
  std::vector<expr::term_ref> from_vars;
  std::vector<expr::term_ref> to_vars;
  tm().get_variables(from, from_vars);
  tm().get_variables(to, to_vars);
  for (size_t i = 0; i < from_vars.size(); ++ i) {
    subst[from_vars[i]] = to_vars[i];
  }
  return tm().substitute(f, subst);
}

bmc_engine::result bmc_engine::query(const system::transition_system& ts, const system::state_formula* sf) {

  smt::solver_scope scope(d_solver);
  scope.push();

  // The state type
  expr::term_ref type = ts.get_state_type()->get_type();

  // The state vars
  expr::term_ref current_state_var = ts.get_state_type()->get_state(system::state_type::STATE_CURRENT);
  expr::term_ref next_state_var = ts.get_state_type()->get_state(system::state_type::STATE_NEXT);

  // Initial states
  expr::term_ref initial_states = ts.get_initial_states();
  d_solver->add(replace_vars(initial_states, current_state_var, state_variables(0, type)));

  // Transition formula
  expr::term_ref transition_fromula = ts.get_transition_relation();

  // The property
  expr::term_ref property = sf->get_formula();

  // BMC loop
  unsigned k = 0;
  while (true) {

    if (output::get_verbosity(std::cout) > 0) {
      std::cout << "BMC: checking " << k << std::endl;
    }

    // Check the current unrolling
    scope.push();
    expr::term_ref property_not = tm().mk_term(expr::TERM_NOT, property);
    d_solver->add(replace_vars(property_not, current_state_var, state_variables(k, type)));
    smt::solver::result r = d_solver->check();

    if (output::get_verbosity(std::cout) > 0) {
      std::cout << "BMC: got " << r << std::endl;
    }

    // See what happened
    switch(r) {
    case smt::solver::SAT:
      return INVALID;
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

    // Did we go overboard
    if (ctx().get_options().has_option("bmc_max") > 0) {
      unsigned max = ctx().get_options().get_unsigned("bmc_max");
      if (k >= max) {
        return UNKNOWN;
      }
    }

    // Unroll once more
    expr::term_ref transition_k = replace_vars(transition_fromula, current_state_var, state_variables(k, type));
    transition_k = replace_vars(transition_k, next_state_var, state_variables(k+1, type));
    d_solver->add(transition_k);
    k = k + 1;
}

  return UNKNOWN;
}

}
}
