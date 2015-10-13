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

induction_obligation::induction_obligation(expr::term_manager& tm, expr::term_ref P, size_t budget, bool analzye)
: d_P(P)
, d_budget(budget)
, d_analyze(analzye)
, d_score(0)
{
}

expr::term_ref induction_obligation::formula() const {
  return d_P;
}

bool induction_obligation::operator == (const induction_obligation& o) const {
  return d_P == o.d_P;
}

void induction_obligation::bump_score(double amount) {
  if (d_score + amount >= 0) {
    d_score += amount;
  } else {
    d_score = 0;
  }
}

bool induction_obligation::operator < (const induction_obligation& o) const {
  // Larger score wins
  if (d_score != o.d_score) {
    return d_score < o.d_score;
  }
  // Larger budget wins
  if (get_budget() != o.get_budget()) {
    return get_budget() < o.get_budget();
  }
  // Break ties
  return formula() > o.formula();
}

bool induction_obligation::analyze_cti() const {
  return d_analyze;
}

size_t induction_obligation::get_budget() const {
  return d_budget;
}

void induction_obligation::set_budget(size_t size) {
  d_budget = size;
}

ic3_engine::ic3_engine(const system::context& ctx)
: engine(ctx)
, d_transition_system(0)
, d_property(0)
, d_trace(0)
, d_smt(0)
, d_reachability(ctx)
, d_induction_frame_index(0)
, d_property_invalid(false)
, d_needed_invalid(false)
, d_learning_type(LEARN_UNDEFINED)
{
  d_stats.frame_index = new utils::stat_int("ic3::frame_index", 0);
  d_stats.frame_size = new utils::stat_int("ic3::frame_size", 0);
  d_stats.frame_pushed = new utils::stat_int("ic3::frame_pushed", 0);
  d_stats.frame_needed = new utils::stat_int("ic3::frame_needed", 0);
  d_stats.queue_size = new utils::stat_int("ic3::queue_size", 0);
  ctx.get_statistics().add(new utils::stat_delimiter());
  ctx.get_statistics().add(d_stats.frame_index);
  ctx.get_statistics().add(d_stats.frame_size);
  ctx.get_statistics().add(d_stats.frame_pushed);
  ctx.get_statistics().add(d_stats.frame_needed);
  ctx.get_statistics().add(d_stats.queue_size);
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
  d_induction_obligations.clear();
  d_induction_obligations_next.clear();
  d_induction_obligations_count.clear();
  delete d_smt;
  d_smt = 0;
  d_properties.clear();
  d_needed.clear();
  d_property_invalid = false;
  d_frame_formula_invalid_info.clear();
  d_frame_formula_parent_info.clear();
  d_reachability.clear();

  if (ai()) {
    ai()->clear();
  }
}

induction_obligation ic3_engine::pop_induction_obligation() {
  assert(d_induction_obligations.size() > 0);
  induction_obligation ind = d_induction_obligations.top();
  d_induction_obligations.pop();
  d_induction_obligations_handles.erase(ind.formula());
  d_stats.queue_size->get_value() = d_induction_obligations.size();
  return ind;
}

void ic3_engine::add_to_induction_frame(expr::term_ref F) {
  assert(d_induction_frame.find(F) == d_induction_frame.end());
  d_induction_frame.insert(F);
  d_smt->get_induction_solver()->add(F, smt::solver::CLASS_A);
  d_stats.frame_size->get_value() = d_induction_frame.size();
}

void ic3_engine::enqueue_induction_obligation(const induction_obligation& ind) {
  assert(d_induction_obligations_handles.find(ind.formula()) == d_induction_obligations_handles.end());
  assert(d_induction_frame.find(ind.formula()) != d_induction_frame.end());
  induction_obligation_queue::handle_type h = d_induction_obligations.push(ind);
  d_induction_obligations_handles[ind.formula()] = h;
  d_stats.queue_size->get_value() = d_induction_obligations.size();
}

void ic3_engine::bump_induction_obligation(expr::term_ref f, double amount) {
  expr::term_ref_hash_map<induction_obligation_queue::handle_type>::const_iterator find = d_induction_obligations_handles.find(f);
  if (find == d_induction_obligations_handles.end()) {
    return; // not in queue... already processed
  }
  induction_obligation_queue::handle_type h = find->second;
  induction_obligation& ind = *h;
  ind.bump_score(amount);
  d_induction_obligations.update(h);
}

ic3_engine::induction_result ic3_engine::push_if_inductive(induction_obligation& ind) {

  bool analyze_cti = ind.analyze_cti();
  expr::term_ref f = ind.formula();
  bool needed = is_needed(f);


  TRACE("ic3") << "ic3: pushing at " << d_induction_frame_index << ": " << f << std::endl;

  size_t default_budget = d_induction_frame_index + 1;

  // Check if inductive
  std::vector<expr::term_ref> core;
  solvers::query_result result = d_smt->check_inductive(f, core);

  // If inductive
  if (result.result == smt::solver::UNSAT) {
    TRACE("ic3") << "ic3: pushing at " << d_induction_frame_index << ": " << f << " is inductive" << std::endl;
    // Add to the next frame
    d_induction_obligations_next.push_back(induction_obligation(tm(), f, 0, analyze_cti));
    d_stats.frame_pushed->get_value() = d_induction_obligations_next.size();
    // Mark all as needed if core was obtained
    if (d_smt->check_inductive_returns_core()) {
      if (core.size() > 0) {
        // Add to assumption map
        assert(d_induction_assumptions.find(f) == d_induction_assumptions.end());
        for (size_t i = 0; i < core.size(); ++ i) {
          d_induction_assumptions.insert(std::make_pair(f, core[i]));
        }
        // Bump the
        double bump = 1.0 / (double) core.size();
        for (size_t i = 0; i < core.size(); ++ i) {
          if (needed) {
            set_needed(core[i]);
          }
          bump_induction_obligation(core[i], bump);
        }
      }
    }
    return INDUCTION_SUCCESS;
  }

  expr::term_ref G = result.generalization;
  TRACE("ic3::generalization") << "ic3: generalization " << G << std::endl;
  if (output::trace_tag_is_enabled("ic3::check-learnt")) {
    d_smt->output_efsmt(tm().mk_term(expr::TERM_NOT, f), G);
  }

  // If we're not analyzing the CTI, we're done
  if (!analyze_cti) {
    return INDUCTION_INCONCLUSIVE;
  }

  // Check if G is reachable (give a budget enough for frame length fails)
  size_t reachability_budget = ind.get_budget();
  if (reachability_budget == 0) { reachability_budget = default_budget; }
  reachability::status reachable = d_reachability.check_reachable(d_induction_frame_index, G, result.model, reachability_budget);
  ind.set_budget(reachability_budget);
  if (reachability_budget == 0) {
    bump_induction_obligation(ind.formula(), -1);
  }

  // If reachable, we're not inductive
  if (reachable == reachability::REACHABLE) {
    return INDUCTION_FAIL;
  }

  // If budget exceeded, we retry later
  if (reachable == reachability::BUDGET_EXCEEDED) {
    return INDUCTION_RETRY;
  }

  // Basic thing to learn is the generalization
  expr::term_ref G_not = tm().mk_term(expr::TERM_NOT, G);
  TRACE("ic3") << "ic3: backward learnt: " << G_not << std::endl;

  // Learn something to refute G
  expr::term_ref learnt = G_not;
  if (d_learning_type != LEARN_BACKWARD) {
    // Learn forward
    learnt = d_smt->learn_forward(d_induction_frame_index, G);
    TRACE("ic3") << "ic3: forward learnt: " << learnt << std::endl;
    // If forward learnt is already refuted in the future, use generalization, it's
    // more precise
    if (is_invalid(learnt)) {
      learnt = G_not;
    }
  }

  // Add to inductino frame
  assert(d_induction_frame.find(learnt) == d_induction_frame.end());
  add_to_induction_frame(learnt);
  // Try to push assumptions next time (unless, already invalid)
  if (!is_invalid(learnt)) {
    enqueue_induction_obligation(induction_obligation(tm(), learnt, default_budget, analyze_cti));
    set_refutes_info(f, G, learnt);
  }

  return INDUCTION_RETRY;
}

void ic3_engine::extend_induction_failure(expr::term_ref f) {

  const std::deque<expr::term_ref>& cex = d_reachability.get_cex();

  assert(cex.size() > 0);

  // We have a counter-example to inductiveness of f at at pushing frame
  // and d_counterexample has its generalization at the back.
  assert(cex.size() == d_induction_frame_index + 1);

  // Solver for checking
  smt::solver_scope solver_scope;
  d_smt->get_counterexample_solver(solver_scope);
  solver_scope.push();
  smt::solver* solver = solver_scope.get_solver();

  // Sync the counterexample solver
  d_smt->ensure_counterexample_solver_depth(d_induction_frame_index);

  // Assert all the generalizations
  size_t k = 0;
  for (; k < cex.size(); ++ k) {
    // Add the generalization to frame k
    expr::term_ref G_k = d_trace->get_state_formula(cex[k], k);
    solver->add(G_k, smt::solver::CLASS_A);
  }

  // Assert f at next frame
  assert(k == d_induction_frame_index + 1);
  d_smt->ensure_counterexample_solver_depth(k);
  solver->add(d_trace->get_state_formula(tm().mk_term(expr::TERM_NOT, f), k), smt::solver::CLASS_A);

  // Should be SAT
  smt::solver::result r = solver->check();
  assert(r == smt::solver::SAT);
  expr::model::ref model = solver->get_model();
  d_trace->set_model(model);

  if (ai()) {
    ai()->notify_reachable(d_trace);
  }

  // Try to extend it
  for (;; ++ k) {

    // We know there is a counterexample to induction of f: 0, ..., k-1, with f
    // being false at k. We try to extend it to falsify the reason we
    // introduced f. We introduced f to refute the counterexample to induction
    // of parent(f), which is witnessed by generalization refutes(f). We are
    // therefore looking to satisfy refutes(f) at k.
    assert(is_invalid(f));

    // If no more parents, we're done
    if (!has_parent(f)) {
      // If this is a counter-example to the property itself => full counterexample
      if (d_properties.find(f) != d_properties.end()) {
        MSG(1) << "ic3: CEX found at depth " << d_smt->get_counterexample_solver_depth() << " (with induction frame at " << d_induction_frame_index << ")" << std::endl;
      }
      break;
    }

    // Try to extend
    f = get_parent(f);

    // If invalid already, done
    if (is_invalid(f)) {
      break;
    }

    // Check at frame k + 1
    d_smt->ensure_counterexample_solver_depth(k+1);
    solver->add(d_trace->get_state_formula(tm().mk_term(expr::TERM_NOT, f), k+1), smt::solver::CLASS_A);

    // If not a generalization we need to check
    r = solver->check();

    // If not sat, we can't extend any more
    if (r != smt::solver::SAT) {
      break;
    }

    // We're sat (either by knowing, or by checking), so we extend further
    set_invalid(f, k+1);
    model = solver->get_model();
    d_trace->set_model(model);
    if (ai()) {
      ai()->notify_reachable(d_trace);
    }
  }
}

void ic3_engine::push_current_frame() {

  // Search while we have something to do
  while (!d_induction_obligations.empty() && !d_property_invalid) {

    // Pick a formula to try and prove inductive, i.e. that F_k & P & T => P'
    induction_obligation ind = pop_induction_obligation();

    // If formula is marked as invalid, skip it
    if (is_invalid(ind.formula())) {
      continue;
    }

    // Push the formula forward if it's inductive at the frame
    induction_result ind_result = push_if_inductive(ind);

    // See what happened
    switch (ind_result) {
    case INDUCTION_RETRY:
      // We'll retry the same formula (it's already added to the solver)
      enqueue_induction_obligation(ind);
      break;
    case INDUCTION_SUCCESS:
      // Boss
      break;
    case INDUCTION_FAIL:
      // Not inductive, mark it
      set_invalid(ind.formula(), d_induction_frame_index + 1);
      // Try to extend the counter-example further
      extend_induction_failure(ind.formula());
      break;
    case INDUCTION_INCONCLUSIVE:
      break;
    }
  }

  // Dump dependency graph if asked
  if (ctx().get_options().get_bool("ic3-dump-dependencies")) {
    std::stringstream ss;
    ss << "dependency." << d_induction_frame_index << ".dot";
    std::ofstream output(ss.str().c_str());

    output << "digraph G {" << std::endl;

    // Output relationships
    expr::term_ref_map<frame_formula_parent_info>::const_iterator it = d_frame_formula_parent_info.begin();
    expr::term_ref_map<frame_formula_parent_info>::const_iterator it_end = d_frame_formula_parent_info.end();
    for (; it != it_end; ++ it) {
      expr::term_ref learnt = it->first;
      expr::term_ref parent = it->second.parent;
      if (is_invalid(learnt)) {
        output << tm().id_of(learnt) << " [color = red];" << std::endl;
      }
      output << tm().id_of(learnt) << "->" << tm().id_of(parent) << ";" << std::endl;
    }

    output << "}" << std::endl;
  }
}

engine::result ic3_engine::search() {

  // Push frame by frame */
  for(;;) {

    // Clear the frame-specific info
    d_frame_formula_parent_info.clear();
    d_induction_assumptions.clear();

    // If we have unsat core, mark all properties as needed
    if (d_smt->check_inductive_returns_core()) {
      d_needed.clear();
      d_needed_invalid = false;
      std::set<expr::term_ref>::const_iterator prop_it = d_properties.begin();
      for (; prop_it != d_properties.end(); ++ prop_it) {
        set_needed(*prop_it);
      }
    }

    // Push the current induction frame forward
    push_current_frame();

    // If we've disproved the property, we're done
    if (d_property_invalid) {
      return engine::INVALID;
    }

    // If we pushed all that's needed, we're done
    if (d_smt->check_inductive_returns_core() && !d_needed_invalid) {
      if (ctx().get_options().get_bool("ic3-show-invariant")) {
        d_smt->print_formulas(d_induction_frame, std::cout);
      }
      return engine::VALID;
    } else if (d_induction_frame.size() == d_induction_obligations_next.size()) {
      if (ctx().get_options().get_bool("ic3-show-invariant")) {
        d_smt->print_formulas(d_induction_frame, std::cout);
      }
      return engine::VALID;
    }

    // Move to the next frame (will also clear induction solver)
    d_induction_frame_index ++;
    d_induction_obligations.clear();

    MSG(1) << "ic3: Extending trace to " << d_induction_frame_index << std::endl;

    d_stats.frame_index->get_value() = d_induction_frame_index;
    d_stats.frame_size->get_value() = 0;

    // If exceeded number of frames
    if (d_induction_frame_index == ctx().get_options().get_unsigned("ic3-max")) {
      return engine::INTERRUPTED;
    }

    // Add formulas to the new frame
    d_induction_frame.clear();
    d_smt->reset_induction_solver();
    std::vector<induction_obligation>::const_iterator next_it = d_induction_obligations_next.begin();
    for (; next_it != d_induction_obligations_next.end(); ++ next_it) {
      // Push if not shown invalid
      if (!is_invalid(next_it->formula())) {
        add_to_induction_frame(next_it->formula());
        enqueue_induction_obligation(*next_it);
      }
    }
    d_induction_obligations_next.clear();
    d_stats.frame_pushed->get_value() = 0;

    // Do garbage collection
    d_smt->gc();

    // Restart if asked
    if (ctx().get_options().get_bool("ic3-enable-restarts")) {
      return engine::UNKNOWN;
    }
  }

  // Didn't prove or disprove, so unknown
  return engine::UNKNOWN;
}

engine::result ic3_engine::query(const system::transition_system* ts, const system::state_formula* sf) {

  // Initialize
  result r = UNKNOWN;
  d_induction_frame_index = 0;

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

  // Add the initial state
  expr::term_ref I = d_transition_system->get_initial_states();
  add_initial_states(I);

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

void ic3_engine::add_initial_states(expr::term_ref I) {
  if (tm().term_of(I).op() == expr::TERM_AND) {
    size_t size = tm().term_of(I).size();
    for (size_t i = 0; i < size; ++ i) {
      add_initial_states(tm().term_of(I)[i]);
    }
  } else {
    if (d_induction_frame.find(I) == d_induction_frame.end()) {
      d_reachability.add_to_frame(0, I);
      add_to_induction_frame(I);
      enqueue_induction_obligation(induction_obligation(tm(), I, 0, true));
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
    solvers::query_result result = d_smt->query_at(0, tm().mk_term(expr::TERM_NOT, P), smt::solver::CLASS_A);
    if (result.result == smt::solver::UNSAT) {
      if (d_induction_frame.find(P) == d_induction_frame.end()) {
        d_reachability.add_to_frame(0, P);
        add_to_induction_frame(P);
        enqueue_induction_obligation(induction_obligation(tm(), P, 0, true));
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

void ic3_engine::set_invalid(expr::term_ref f, size_t frame) {
  assert(frame >= d_induction_frame_index);
  assert(d_frame_formula_invalid_info.find(f) == d_frame_formula_invalid_info.end());
  d_frame_formula_invalid_info[f] = frame;
  if (d_properties.find(f) != d_properties.end()) {
    d_property_invalid = true;
  }
  if (is_needed(f)) {
    d_needed_invalid = true;
  }
}

bool ic3_engine::is_invalid(expr::term_ref f) const {
  expr::term_ref_map<size_t>::const_iterator find = d_frame_formula_invalid_info.find(f);
  return (find != d_frame_formula_invalid_info.end());
}

void ic3_engine::set_needed(expr::term_ref f) {
  std::set<expr::term_ref>::iterator find = d_needed.find(f);
  if (find == d_needed.end()) {
    // Add to needed set
    d_needed.insert(f);
    d_stats.frame_needed->get_value() = d_needed.size();
    // Set any induction assumptions as needed too
    std::pair<induction_assumptions_map::const_iterator, induction_assumptions_map::const_iterator> ia = d_induction_assumptions.equal_range(f);
    for (induction_assumptions_map::const_iterator it = ia.first; it != ia.second; ++ it) {
      set_needed(it->second);
    }
  }
  if (is_invalid(f)) {
    d_needed_invalid = true;
  }
}

bool ic3_engine::is_needed(expr::term_ref f) const {
  return d_needed.find(f) != d_needed.end();
}

void ic3_engine::gc_collect(const expr::gc_relocator& gc_reloc) {
  d_frame_formula_parent_info.reloc(gc_reloc);
  assert(d_induction_obligations_next.size() == 0);
  d_smt->gc_collect(gc_reloc);
  d_reachability.gc_collect(gc_reloc);
}

void ic3_engine::set_refutes_info(expr::term_ref f, expr::term_ref g, expr::term_ref l) {
  if (d_frame_formula_parent_info.find(l) != d_frame_formula_parent_info.end()) {
    std::cerr << "l = " << l << std::endl;
    std::cerr << "f = " << f << std::endl;
    std::cerr << "g = " << g << std::endl;

    expr::term_ref_map<frame_formula_parent_info>::const_iterator find = d_frame_formula_parent_info.find(l);
    std::cerr << "f = " << find->second.parent << std::endl;
    std::cerr << "g = " << find->second.refutes << std::endl;

    std::cerr << "f_invalid = " << is_invalid(f) << std::endl;
    std::cerr << "l_invalid = " << is_invalid(l) << std::endl;

    assert(false);
  }
  d_frame_formula_parent_info[l] = frame_formula_parent_info(f, g);
}

expr::term_ref ic3_engine::get_refutes(expr::term_ref l) const {
  assert(has_parent(l));
  expr::term_ref_map<frame_formula_parent_info>::const_iterator find = d_frame_formula_parent_info.find(l);
  return find->second.refutes;
}

expr::term_ref ic3_engine::get_parent(expr::term_ref l) const {
  assert(has_parent(l));
  expr::term_ref_map<frame_formula_parent_info>::const_iterator find = d_frame_formula_parent_info.find(l);
  return find->second.parent;
}

bool ic3_engine::has_parent(expr::term_ref l) const {
  expr::term_ref_map<frame_formula_parent_info>::const_iterator find = d_frame_formula_parent_info.find(l);
  if (find == d_frame_formula_parent_info.end()) { return false; }
  return !find->second.parent.is_null();
}


}
}
