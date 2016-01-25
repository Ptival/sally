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

#include "engine/ic3/ic3_engine.h"
#include "engine/ic3/solvers.h"
#include "engine/factory.h"

#include "smt/factory.h"
#include "system/state_trace.h"
#include "utils/trace.h"
#include "expr/gc_relocator.h"

#include <stack>
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>

#define unused_var(x) { (void)x; }

namespace sally {
namespace ic3 {

// Fibonacci heap returns the top element in the order, so this should
bool induction_obligation_cmp::operator() (const induction_obligation& ind1, const induction_obligation& ind2) const {
  if (ind1.score != ind2.score) {
    return ind1.score < ind2.score;
  }
  if (ind1.d != ind2.d) {
    return ind1.d < ind2.d;
  }
  if (ind1.F_cex < ind2.F_cex) {
    return ind1.F_cex < ind2.F_cex;
  }
  return ind1.F_fwd < ind2.F_fwd;
}

induction_obligation::induction_obligation(expr::term_manager& tm, expr::term_ref F_fwd, expr::term_ref F_cex, size_t d, double score)
: F_fwd(F_fwd)
, F_cex(F_cex)
, d(d)
, score(score)
{}

bool induction_obligation::operator == (const induction_obligation& o) const {
  return F_cex == o.F_cex && F_fwd == o.F_fwd;
}

void induction_obligation::bump_score(double amount) {
  if (score + amount >= 0) {
    score += amount;
  } else {
    score = 0;
  }
}

bool induction_obligation::operator < (const induction_obligation& o) const {
  if (F_cex == o.F_cex) {
    return F_fwd < o.F_fwd;
  }
  return F_cex < o.F_cex;
}

std::ostream& operator << (std::ostream& out, const induction_obligation& ind) {
  out << ind.F_fwd;
  return out;
}

ic3_engine::ic3_engine(const system::context& ctx)
: engine(ctx)
, d_transition_system(0)
, d_property(0)
, d_trace(0)
, d_smt(0)
, d_reachability(ctx)
, d_induction_frame_index(0)
, d_induction_frame_depth(0)
, d_induction_cutoff(0)
, d_property_invalid(false)
, d_learning_type(LEARN_UNDEFINED)
{
  d_stats.frame_index = new utils::stat_int("ic3::frame_index", 0);
  d_stats.induction_depth = new utils::stat_int("ic3::induction_depth", 0);
  d_stats.frame_size = new utils::stat_int("ic3::frame_size", 0);
  d_stats.frame_pushed = new utils::stat_int("ic3::frame_pushed", 0);
  d_stats.queue_size = new utils::stat_int("ic3::queue_size", 0);
  d_stats.max_cex_depth = new utils::stat_int("ic3::max_cex_depth", 0);
  ctx.get_statistics().add(new utils::stat_delimiter());
  ctx.get_statistics().add(d_stats.frame_index);
  ctx.get_statistics().add(d_stats.induction_depth);
  ctx.get_statistics().add(d_stats.frame_size);
  ctx.get_statistics().add(d_stats.frame_pushed);
  ctx.get_statistics().add(d_stats.queue_size);
  ctx.get_statistics().add(d_stats.max_cex_depth);
}

ic3_engine::~ic3_engine() {
  delete d_trace;
  delete d_smt;
}

void ic3_engine::reset() {

  d_transition_system = 0;
  d_property = 0;
  delete d_trace;
  d_trace = 0;
  d_induction_frame.clear();
  d_induction_frame_index = 0;
  d_induction_frame_depth = 0;
  d_induction_cutoff = 1;
  d_induction_obligations.clear();
  d_induction_obligations_next.clear();
  d_induction_obligations_count.clear();
  delete d_smt;
  d_smt = 0;
  d_properties.clear();
  d_property_invalid = false;
  d_reachability.clear();

  if (ai()) {
    ai()->clear();
  }
}

induction_obligation ic3_engine::pop_induction_obligation() {
  assert(d_induction_obligations.size() > 0);
  induction_obligation ind = d_induction_obligations.top();
  d_induction_obligations.pop();
  d_induction_obligations_handles.erase(ind);
  d_stats.queue_size->get_value() = d_induction_obligations.size();
  return ind;
}

void ic3_engine::enqueue_induction_obligation(const induction_obligation& ind) {
  assert(d_induction_frame.find(ind) != d_induction_frame.end());
  induction_obligation_queue::handle_type h = d_induction_obligations.push(ind);
  d_induction_obligations_handles[ind] = h;
  d_stats.queue_size->get_value() = d_induction_obligations.size();
}

ic3_engine::induction_result ic3_engine::push_obligation(induction_obligation& ind) {

  TRACE("ic3") << "ic3: Trying F_fwd at " << d_induction_frame_index << ": " << ind.F_fwd << std::endl;

  // Check if F_cex is inductive. If not then, if it can be reached, we can find a counter-example.
  solvers::query_result result = d_smt->check_inductive(ind.F_fwd);

  // If UNSAT we can push it
  if (result.result == smt::solver::UNSAT) {    
    // It's pushed so add it to induction assumptions
    assert(d_induction_frame.find(ind) != d_induction_frame.end());
    TRACE("ic3") << "ic3: pushed " << ind.F_fwd << std::endl;
    // Add it to set of pushed facts
    d_induction_obligations_next.push_back(ind);
    // Update stats
    d_stats.frame_pushed->get_value() = d_induction_obligations_next.size();
    // We're done
    return INDUCTION_SUCCESS;
  }

  // We have a model for
  //
  //   F F ..... F !F
  //
  // If the model satisfies CEX in the last state, we try to extend the
  // counter-example, otherwise, we fail the push.
  result = d_smt->check_inductive_model(result.model, ind.F_cex);
  if (result.result == smt::solver::UNSAT) {
    return INDUCTION_FAIL;
  }

  // We can actually reach the counterexample of induction from G, so we check if
  // it's reachable.
  TRACE("ic3") << "ic3: F_cex generalization " << result.generalization << std::endl;

  // Check if G is reachable. We know that F_cex is not reachable up to induction frame index.
  // This means that G can be reached at index i, then F_cex is reachable at i + induction_depth.
  // Therefore i + induction_depth > frame_index, hence we check from i = frame_index - depth + 1 to frame_index
  size_t start = (d_induction_frame_index + 1) - d_induction_frame_depth;
  size_t end = d_induction_frame_index;
  reachability::status reachable = d_reachability.check_reachable(start, end, result.generalization, result.model);
  
  // If reachable, then G leads to F_cex, and F_cex leads to !P
  if (reachable == reachability::REACHABLE) {
    d_property_invalid = true;
    return INDUCTION_FAIL;
  }

  // G is not reachable, so !G holds up to current frame index
  expr::term_ref F_cex = result.generalization;
  TRACE("ic3") << "ic3: new F_cex: " << F_cex << std::endl;

  // Learn something forward that refutes G
  // We know that G_not is not satisfiable
  expr::term_ref F_fwd = d_smt->learn_forward(d_induction_frame_index, F_cex);
  TRACE("ic3") << "ic3: new F_fwd: " << F_fwd << std::endl;

  // Add to counter-example to induction frame
  induction_obligation new_ind(tm(), F_fwd, F_cex, d_induction_frame_depth + ind.d, /* initial score */ 0);
  // Add to induction assertion (but NOT to intermediate)
  assert(d_induction_frame.find(new_ind) == d_induction_frame.end());
  d_induction_frame.insert(new_ind);
  d_stats.frame_size->get_value() = d_induction_frame.size();
  d_smt->add_to_induction_solver(F_fwd, solvers::INDUCTION_FIRST);
  d_smt->add_to_induction_solver(F_fwd, solvers::INDUCTION_INTERMEDIATE);
  enqueue_induction_obligation(new_ind);

  if (d_learning_type != LEARN_FORWARD) {
    expr::term_ref F_cex_not = tm().mk_term(expr::TERM_NOT, F_cex);
    if (F_cex_not != F_fwd) {
      // Add to counter-example to induction frame
      induction_obligation new_ind(tm(), F_cex_not, F_cex, d_induction_frame_depth + ind.d, /* initial score */ 0);
      // Add to induction assertion (but NOT to intermediate)
      assert(d_induction_frame.find(new_ind) == d_induction_frame.end());
      d_induction_frame.insert(new_ind);
      d_stats.frame_size->get_value() = d_induction_frame.size();
      enqueue_induction_obligation(new_ind);
    }
  }

  // Decrease the score of the obligation
  if (ind.d > 0) {
    ind.bump_score(-1/ind.d);
  }

  // We try again with newly learnt facts that eliminate the counter-example we found
  return INDUCTION_RETRY;
}

void ic3_engine::push_current_frame() {

  // Search while we have something to do
  while (!d_induction_obligations.empty() && !d_property_invalid) {

    // Pick a formula to try and prove inductive, i.e. that F_k & P & T => P'
    induction_obligation ind = pop_induction_obligation();

    // If more than cutoff, just break
    if (ind.d >= d_induction_cutoff) {
      continue;
    }

    // Push the formula forward if it's inductive at the frame
    induction_result ind_result = push_obligation(ind);

    // See what happened
    switch (ind_result) {
    case INDUCTION_RETRY:
      // We'll retry the same formula (it's already added to the solver)
      enqueue_induction_obligation(ind);
      break;
    case INDUCTION_SUCCESS:
      // Boss, we're done with this one
      break;
    case INDUCTION_FAIL:
      // Failure, we didn't push, either counter-example found, or we couldn't push
      // and decided not to try again
      break;
    }
  }
}

engine::result ic3_engine::search() {

  // Push frame by frame */
  for(;;) {

    // Set the cutoff
    d_induction_cutoff = std::numeric_limits<size_t>::max();
    // d_induction_cutoff = 2*d_induction_frame_index + 1;

    MSG(1) << "ic3: working on induction frame " << d_induction_frame_index << " with induction depth " << d_induction_frame_depth << " and cutoff " << d_induction_cutoff << std::endl;

    // Push the current induction frame forward
    push_current_frame();

    // If we've disproved the property, we're done
    if (d_property_invalid) {
      return engine::INVALID;
    }

    MSG(1) << "ic3: pushed " << d_induction_obligations_next.size() << " of " << d_induction_frame.size() << std::endl;

    // If we pushed everything, we're done
    if (d_induction_frame.size() == d_induction_obligations_next.size()) {
      if (ctx().get_options().get_bool("ic3-show-invariant")) {
        d_smt->print_formulas(d_induction_frame, std::cout);
      }
      return engine::VALID;
    }

    // Clear induction obligations queue and the frame
    d_induction_obligations.clear();
    d_induction_frame.clear();
    d_stats.frame_size->get_value() = 0;

    // If exceeded number of frames
    if (ctx().get_options().get_unsigned("ic3-max") > 0 && d_induction_frame_index >= ctx().get_options().get_unsigned("ic3-max")) {
      return engine::INTERRUPTED;
    }

    // Next frame position
    d_induction_frame_index ++;

    // Set depth of induction
    d_induction_frame_depth ++;
    if (ctx().get_options().get_unsigned("ic3-induction-max") != 0 && d_induction_frame_depth > ctx().get_options().get_unsigned("ic3-induction-max")) {
      d_induction_frame_depth = ctx().get_options().get_unsigned("ic3-induction-max");
    }
    d_smt->reset_induction_solver(d_induction_frame_depth);

    // Add formulas to the new frame
    d_induction_frame.clear();
    std::vector<induction_obligation>::const_iterator next_it = d_induction_obligations_next.begin();
    for (; next_it != d_induction_obligations_next.end(); ++ next_it) {
      // The formula
      induction_obligation ind = *next_it;
      ind.bump_score(1); // We prefer deeper ones
      assert(d_induction_frame.find(ind) == d_induction_frame.end());
      d_smt->add_to_induction_solver(ind.F_fwd, solvers::INDUCTION_FIRST);
      d_smt->add_to_induction_solver(ind.F_fwd, solvers::INDUCTION_INTERMEDIATE);
      d_induction_frame.insert(ind);
      d_stats.frame_size->get_value() = d_induction_frame.size();
      enqueue_induction_obligation(ind);
    }

    // Clear next frame info
    d_induction_obligations_next.clear();
    d_stats.frame_pushed->get_value() = 0;

    // Update stats
    d_stats.frame_index->get_value() = d_induction_frame_index;
    d_stats.induction_depth->get_value() = d_induction_frame_depth;

    // Do garbage collection
    d_smt->gc();
  }

  // Didn't prove or disprove, so unknown
  return engine::UNKNOWN;
}

engine::result ic3_engine::query(const system::transition_system* ts, const system::state_formula* sf) {

  // Initialize
  result r = UNKNOWN;

  // Reset the engine
  reset();

  // Remember the input
  d_transition_system = ts;
  d_property = sf;

  // Make the trace
  if (d_trace) { delete d_trace; }
  d_trace = new system::state_trace(sf->get_state_type());

  // Initialize the solvers
  if (d_smt) { delete d_smt; }
  d_smt = new solvers(ctx(), ts, d_trace);

  // Initialize the reachability solver
  d_reachability.init(d_transition_system, d_smt);

  // Initialize the induction solver
  d_induction_frame_index = 0;
  d_induction_frame_depth = 1;
  d_induction_cutoff = 1;
  d_smt->reset_induction_solver(1);

  // Add the initial state
  if (!ctx().get_options().get_bool("ic3-no-initial-state")) {
    expr::term_ref I = d_transition_system->get_initial_states();
    add_initial_states(I, d_property->get_formula());
  }

  // Add the property we're trying to prove (if not already invalid at frame 0)
  bool ok = add_property(d_property->get_formula());
  if (!ok) {
    return engine::INVALID;
  }

  while (r == UNKNOWN) {

    MSG(1) << "ic3: starting search" << std::endl;

    // Search
    r = search();
  }

  MSG(1) << "ic3: search done: " << r << std::endl;

  return r;
}

void ic3_engine::add_initial_states(expr::term_ref I, expr::term_ref P) {
  if (tm().term_of(I).op() == expr::TERM_AND) {
    size_t size = tm().term_of(I).size();
    for (size_t i = 0; i < size; ++ i) {
      add_initial_states(tm().term_of(I)[i], P);
    }
  } else {
    induction_obligation ind(tm(), I, tm().mk_term(expr::TERM_NOT, P), /* cex depth */ 0, /* score */ 0);
    if (d_induction_frame.find(ind) == d_induction_frame.end()) {
      assert(d_induction_frame_depth == 1);
      d_induction_frame.insert(ind);
      d_stats.frame_size->get_value() = d_induction_frame.size();
      d_smt->add_to_induction_solver(I, solvers::INDUCTION_FIRST);
      enqueue_induction_obligation(ind);
    }
  }
}

bool ic3_engine::add_property(expr::term_ref P) {
  if (tm().term_of(P).op() == expr::TERM_AND) {
    size_t size = tm().term_of(P).size();
    for (size_t i = 0; i < size; ++ i) {
      bool ok = add_property(tm().term_of(P)[i]);
      if (!ok) return false;
    }
    return true;
  } else {
    smt::solver::result result = d_smt->query_at_init(tm().mk_term(expr::TERM_NOT, P));
    if (result == smt::solver::UNSAT) {
      induction_obligation ind(tm(), P, tm().mk_term(expr::TERM_NOT, P), /* cex depth */ 0, /* score */ 0);
      if (d_induction_frame.find(ind) == d_induction_frame.end()) {
        // Add to induction frame, we know it holds at 0
        assert(d_induction_frame_depth == 1);
        d_induction_frame.insert(ind);
        d_stats.frame_size->get_value() = d_induction_frame.size();
        d_smt->add_to_induction_solver(P, solvers::INDUCTION_FIRST);
        enqueue_induction_obligation(ind);
      }
      d_properties.insert(P);
      return true;
    } else {
      return false;
    }
  }
}

const system::state_trace* ic3_engine::get_trace() {
  return d_trace;
}

void ic3_engine::gc_collect(const expr::gc_relocator& gc_reloc) {
  assert(d_induction_obligations_next.size() == 0);
  d_smt->gc_collect(gc_reloc);
  d_reachability.gc_collect(gc_reloc);
}


}
}
