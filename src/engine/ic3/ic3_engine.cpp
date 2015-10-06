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

#include "smt/factory.h"
#include "system/state_trace.h"
#include "utils/trace.h"
#include "expr/gc_relocator.h"

#include <stack>
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>

#define unused_var(x) { (void)x; }

namespace sally {
namespace ic3 {

induction_obligation::induction_obligation(expr::term_ref P, size_t used_budget, bool analzye)
: d_P(P)
, d_budget(used_budget)
, d_analyze(analzye)
{}

expr::term_ref induction_obligation::formula() const {
  return d_P;
}

bool induction_obligation::operator == (const induction_obligation& o) const {
  return d_P == o.d_P;
}

bool induction_obligation::operator < (const induction_obligation& o) const {
  // Smaller budget wins
  if (used_budget() != o.used_budget()) {
    return used_budget() > o.used_budget();
  }
  // Break ties
  return formula() > o.formula();
}

bool induction_obligation::analyze_cti() const {
  return d_analyze;
}

size_t induction_obligation::used_budget() const {
  return d_budget;
}

void induction_obligation::add_to_used_budget(size_t size) {
  d_budget += size;
}

/** A reachability obligation at frame k. */
class reachability_obligation {

  /** The frame of the obligation */
  size_t d_k;
  /** The formula in question */
  expr::term_ref d_P;
  /** Local model of the formula */
  expr::model::ref d_P_model;

public:

  /** Construct the obligation */
  reachability_obligation(size_t k, expr::term_ref P, expr::model::ref P_model)
  : d_k(k), d_P(P), d_P_model(P_model)
  {}

  /** Get the frame */
  size_t frame() const { return d_k; }

  /** Get the formula */
  expr::term_ref formula() const { return d_P; }

  /** Get the model */
  expr::model::ref model() const { return d_P_model; }

};

ic3_engine::ic3_engine(const system::context& ctx)
: engine(ctx)
, d_transition_system(0)
, d_property(0)
, d_trace(0)
, d_smt(0)
, d_induction_frame(0)
, d_property_invalid(false)

{
  for (size_t i = 0; i < 10; ++ i) {
    std::stringstream ss;
    ss << "sally::ic3::frame_size[" << i << "]";
    utils::stat_int* s = new utils::stat_int(ss.str(), 0);
    d_stat_frame_size.push_back(s);
    ctx.get_statistics().add(s);
  }
}

ic3_engine::~ic3_engine() {
  delete d_trace;
  delete d_smt;
}

void ic3_engine::reset() {

  d_transition_system = 0;
  d_property = 0;
  d_counterexample.clear();
  delete d_trace;
  d_trace = 0;
  d_induction_frame = 0;
  d_induction_obligations.clear();
  d_induction_obligations_next.clear();
  d_induction_obligations_count.clear();
  d_frame_content.clear();
  delete d_smt;
  d_smt = 0;
  for (size_t i = 0; i < d_stat_frame_size.size(); ++ i) {
    d_stat_frame_size[i]->get_value() = 0;
  }
  d_properties.clear();
  d_property_invalid = false;

  if (ai()) {
    ai()->clear();
  }
}

solvers::query_result ic3_engine::check_one_step_reachable(size_t k, expr::term_ref F) {
  assert(k > 0);

  // The state type
  const system::state_type* state_type = d_transition_system->get_state_type();

  // Move the formula variables current -> next
  expr::term_ref F_next = state_type->change_formula_vars(system::state_type::STATE_CURRENT, system::state_type::STATE_NEXT, F);

  // Query
  return d_smt->query_at(k-1, F_next, smt::solver::CLASS_B);
}

void ic3_engine::add_valid_up_to(size_t k, expr::term_ref F) {
  TRACE("ic3") << "ic3: adding at " << k << ": " << F << std::endl;
  assert(k > 0);
  assert(k < d_frame_content.size());

  // Add to all frames from 1..k (not adding to 0, initial states need no refinement)
  int i;
  for(i = k; i >= 1; -- i) {
    if (frame_contains(i, F)) {
      break;
    }
    add_to_frame(i, F);
  }
  assert(i < k);
}

induction_obligation ic3_engine::pop_induction_obligation() {
  assert(d_induction_obligations.size() > 0);
  induction_obligation ind = d_induction_obligations.top();
  d_induction_obligations.pop();
  return ind;
}

void ic3_engine::ensure_frame(size_t k) {

  // Upsize the frames if necessary
  while (d_frame_content.size() <= k) {
    // Add the empty frame content
    d_frame_content.push_back(formula_set());
    // Number of obligations per frame
    d_induction_obligations_count.push_back(0);
    // Solvers new frame
    d_smt->new_frame();
  }
}

bool ic3_engine::frame_contains(size_t k, expr::term_ref f) {
  assert(k < d_frame_content.size());
  return d_frame_content[k].find(f) != d_frame_content[k].end();
}

ic3_engine::reachability_status ic3_engine::check_reachable(size_t k, expr::term_ref f, expr::model::ref f_model, size_t& budget) {

  TRACE("ic3") << "ic3: checking reachability at " << k << std::endl;

  assert(budget > 0);

  // Queue of reachability obligations
  std::vector<reachability_obligation> reachability_obligations;
  reachability_obligations.push_back(reachability_obligation(k, f, f_model));

  // We're not reachable, if we hit 0 we set it to true
  bool reachable = false;

  // The induction not valid, try to extend to full counter-example
  for (size_t check = 0; !reachability_obligations.empty(); ++ check) {
    // Get the next reachability obligations
    reachability_obligation reach = reachability_obligations.back();
    // If we're at 0 frame, we're reachable: anything passed in is consistent
    // part of the abstraction
    if (reach.frame() == 0) {
      // We're reachable, mark it
      reachable = true;
      // Remember the counterexample and notify the analyzer
      d_counterexample.clear();
      for (size_t i = 0; i < reachability_obligations.size(); ++ i) {
        d_counterexample.push_front(reachability_obligations[i].formula());
      }
      break;
    }

    // Check if the obligation is reachable
    solvers::query_result result = check_one_step_reachable(reach.frame(), reach.formula());
    if (result.result == smt::solver::UNSAT) {
      // Proven, remove from obligations
      reachability_obligations.pop_back();
      // Notify the analyzer
      if (ai()) {
        ai()->notify_unreachable(k, reach.model());
      }
      // Learn
      if (reach.frame() < k) {
        // Learn something at k that refutes the formula
        expr::term_ref learnt = d_smt->learn_forward(reach.frame(), reach.formula());
        // Add any unreachability learnts, but not f itself
        add_valid_up_to(reach.frame(), learnt);
        // Use the budget
        if (--budget == 0) {
          return BUDGET_EXCEEDED;
        }
      }
    } else {
      if (output::trace_tag_is_enabled("ic3::check-learnt")) {
        output_efsmt(reach.formula(), result.generalization);
      }
      // New obligation to reach the counterexample
      reachability_obligations.push_back(reachability_obligation(reach.frame()-1, result.generalization, result.model));
    }
  }

  TRACE("ic3") << "ic3: " << (reachable ? "reachable" : "not reachable") << std::endl;

  // All discharged, so it's not reachable
  if (reachable) {
    return REACHABLE;
  } else {
    return UNREACHABLE;
  }
}

ic3_engine::induction_result ic3_engine::push_if_inductive(induction_obligation& ind) {

  bool analyze_cti = ind.analyze_cti();
  expr::term_ref f = ind.formula();

  assert(d_induction_frame < d_frame_content.size());
  assert(frame_contains(d_induction_frame, f));

  TRACE("ic3") << "ic3: pushing at " << d_induction_frame << ": " << f << std::endl;

  // Check if inductive
  solvers::query_result result = d_smt->check_inductive(f);

  // If inductive
  if (result.result == smt::solver::UNSAT) {
    // Add to the next frame
    d_induction_obligations_next.push_back(induction_obligation(f, ind.used_budget() / 2, analyze_cti));
    return INDUCTION_SUCCESS;
  }

  expr::term_ref G = result.generalization;
  TRACE("ic3::generalization") << "ic3: generalization " << G << std::endl;
  if (output::trace_tag_is_enabled("ic3::check-learnt")) {
    output_efsmt(tm().mk_term(expr::TERM_NOT, f), G);
  }

  // If we're not analyzing the CTI, we're done
  if (!analyze_cti) {
    return INDUCTION_INCONCLUSIVE;
  }

  // Check if G is reachable (give a budget enough for frame length fails)
  size_t reachability_budget = d_induction_frame;
  if (reachability_budget == 0) { reachability_budget = 1; }
  size_t budget_to_use = reachability_budget;
  reachability_status reachable = check_reachable(d_induction_frame, G, result.model, budget_to_use);
  ind.add_to_used_budget(reachability_budget - budget_to_use);

  // If reachable, we're not inductive
  if (reachable == REACHABLE) {
    return INDUCTION_FAIL;
  }

  // If budget exceeded, we retry later
  if (reachable == BUDGET_EXCEEDED) {
    return INDUCTION_RETRY;
  }

  // Basic thing to learn is the generalization
  expr::term_ref G_not = tm().mk_term(expr::TERM_NOT, G);
  TRACE("ic3") << "ic3: backward learnt: " << G_not << std::endl;

  // Learn something to refute G
  expr::term_ref learnt = d_smt->learn_forward(d_induction_frame, G);
  TRACE("ic3") << "ic3: forward learnt: " << learnt << std::endl;

  // If forward learnt is already refuted in the future, use generalization, it's
  // more precise
  if (is_invalid(learnt)) {
    learnt = G_not;
  }

  // Add to obligations if not already shown invalid
  add_valid_up_to(d_induction_frame, learnt);
  // Try to push assumptions next time
  d_induction_obligations.push(induction_obligation(learnt, 0, analyze_cti));
  set_refutes_info(f, G, learnt);

  // Learn with the analyzer
  if (ai()) {
    std::vector<expr::term_ref> analyzer_learnt;
    learn_from_analyzer(analyzer_learnt);
    for (size_t i = 0; i < analyzer_learnt.size(); ++ i) {
      learnt = analyzer_learnt[i];
      TRACE("ic3") << "ic3: analyzer learnt[" << i << "]" << learnt << std::endl;
      if (!is_invalid(learnt)) {
        // Try to push assumptions next time (but don't analyze the failures)
        d_induction_obligations.push(induction_obligation(learnt, 0, false));
      }
    }
  }

  return INDUCTION_RETRY;
}

void ic3_engine::add_to_frame(size_t k, expr::term_ref f) {
  assert(k < d_frame_content.size());
  assert(d_frame_content[k].count(f) == 0);

  // Add to solvers
  d_smt->add(k, f);

  // Remember
  d_frame_content[k].insert(f);
  if (k < d_stat_frame_size.size()) {
    d_stat_frame_size[k]->get_value() ++;
  }
}

void ic3_engine::extend_induction_failure(expr::term_ref f) {

  assert(d_counterexample.size() > 0);

  // We have a counter-example to inductiveness of f at at pushing frame
  // and d_counterexample has its generalization at the back.
  assert(d_counterexample.size() == d_induction_frame + 1);

  // Solver for checking
  smt::solver_scope solver_scope;
  d_smt->get_counterexample_solver(solver_scope);
  solver_scope.push();
  smt::solver* solver = solver_scope.get_solver();

  assert(d_smt->get_counterexample_solver_depth() == d_induction_frame);

  // Assert all the generalizations
  size_t k = 0;
  for (; k < d_counterexample.size(); ++ k) {
    // Add the generalization to frame k
    expr::term_ref G_k = d_trace->get_state_formula(d_counterexample[k], k);
    solver->add(G_k, smt::solver::CLASS_A);
  }

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
      // If this is a counter-example to the property itself, then we extend to
      // full counterexample
      if (d_properties.find(f) != d_properties.end()) {
        d_smt->ensure_counterexample_solver_depth(k);
        solver->add(d_trace->get_state_formula(tm().mk_term(expr::TERM_NOT, f), k), smt::solver::CLASS_A);
        r = solver->check();
        assert(r == smt::solver::SAT);
        model = solver->get_model();
        d_trace->set_model(model);

        MSG(1) << "ic3: CEX found at depth " << d_smt->get_counterexample_solver_depth() << " (with induction frame at " << d_induction_frame << ")" << std::endl;
      }
      break;
    }

    // Try to extend
    expr::term_ref G = get_refutes(f);
    expr::term_ref parent = get_parent(f);

    // Check at frame k
    d_smt->ensure_counterexample_solver_depth(k);
    solver->add(d_trace->get_state_formula(G, k), smt::solver::CLASS_A);

    // If not a generalization we need to check
    r = solver->check();

    // If not sat, we can't extend any more
    if (r != smt::solver::SAT) {
      break;
    }

    // We're sat (either by knowing, or by checking), so we extend further
    f = parent;
    set_invalid(f);
    model = solver->get_model();
    d_trace->set_model(model);
    if (ai()) {
      ai()->notify_reachable(d_trace);
    }
    d_counterexample.push_back(G);
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
      d_induction_obligations.push(ind);
      break;
    case INDUCTION_SUCCESS:
      // Boss
      break;
    case INDUCTION_FAIL:
      // Not inductive, mark it
      set_invalid(ind.formula());
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
    ss << "dependency." << d_induction_frame << ".dot";
    std::ofstream output(ss.str().c_str());

    output << "digraph G {" << std::endl;

    // Output relationships
    expr::term_ref_map<frame_formula_info>::const_iterator it = d_frame_formula_info.begin();
    expr::term_ref_map<frame_formula_info>::const_iterator it_end = d_frame_formula_info.end();
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

    // Push the current induction frame forward
    push_current_frame();

    // If we've disproved the property, we're done
    if (d_property_invalid) {
      return engine::INVALID;
    }

    // If we pushed all of them, we're done
    if (d_frame_content[d_induction_frame].size() == d_induction_obligations_next.size()) {
      if (ctx().get_options().get_bool("ic3-show-invariant")) {
        print_frame(d_induction_frame, std::cout);
      }
      return engine::VALID;
    }

    // Move to the next frame (will also clear induction solver)
    d_induction_frame ++;
    d_induction_obligations.clear();
    ensure_frame(d_induction_frame);

    // If exceeded number of frames
    if (d_induction_frame == ctx().get_options().get_unsigned("ic3-max")) {
      return engine::INTERRUPTED;
    }

    MSG(1) << "ic3: Extending trace to " << d_induction_frame << " (" << d_induction_obligations_next.size() << "/" << d_frame_content[d_induction_frame-1].size() << "facts)" << std::endl;

    // Add formulas to the new frame
    std::vector<induction_obligation>::const_iterator it = d_induction_obligations_next.begin();
    for (; it != d_induction_obligations_next.end(); ++ it) {
      // Push if not shown invalid
      if (!is_invalid(it->formula())) {
        add_to_frame(d_induction_frame, it->formula());
        d_induction_obligations.push(*it);
      }
    }
    d_induction_obligations_next.clear();

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
  d_induction_frame = 0;

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

  // Initializer the analyzer
  if (ai()) {
    d_smt->generate_models_for_queries(true);
    ai()->start(d_transition_system, d_property);
  }

  // Start with at least one frame
  ensure_frame(0);

  // Add the initial state
  expr::term_ref I = d_transition_system->get_initial_states();
  add_to_frame(0, I);
  d_induction_obligations.push(induction_obligation(I, 0, true));

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
  MSG(1) << "ic3: final frame: " << d_frame_content.back().size() << " facts" << std::endl;
  MSG(1) << "ic3: total facts: " << total_facts() << std::endl;

  return r;
}

bool ic3_engine::add_property(expr::term_ref P) {
  const expr::term& P_term = tm().term_of(P);
  if (P_term.op() == expr::TERM_AND) {
    for (size_t i = 0; i < P_term.size(); ++ i) {
      bool ok = add_property(P_term[i]);
      if (!ok) return false;
    }
    return true;
  } else {
    solvers::query_result result = d_smt->query_at(0, tm().mk_term(expr::TERM_NOT, P), smt::solver::CLASS_A);
    if (result.result == smt::solver::UNSAT) {
      add_to_frame(0, P);
      d_induction_obligations.push(induction_obligation(P, 0, true));
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

void ic3_engine::set_invalid(expr::term_ref f) {
  d_frame_formula_info[f].invalid = true;
  if (d_properties.find(f) != d_properties.end()) {
    d_property_invalid = true;
  }
}

bool ic3_engine::is_invalid(expr::term_ref f) const {
  expr::term_ref_map<frame_formula_info>::const_iterator find = d_frame_formula_info.find(f);
  if (find == d_frame_formula_info.end()) {
    return false;
  } else {
    return find->second.invalid;
  }
}

bool ic3_engine::is_invalid_or_parent_invalid(expr::term_ref f) const {
  expr::term_ref_map<frame_formula_info>::const_iterator find = d_frame_formula_info.find(f);
  if (find == d_frame_formula_info.end()) {
    return false;
  } else {
    if (find->second.invalid) {
      return true;
    } else {
      return is_invalid_or_parent_invalid(find->second.parent);
    }
  }
}

void ic3_engine::learn_from_analyzer(std::vector<expr::term_ref>& potential_invariants) {
  if (ai()) {

    // The state type
    const system::state_type* state_type = d_transition_system->get_state_type();

    // Potential invariants from the analyzer
    ai()->infer(potential_invariants);

    // Next varsion of the potential invariants
    std::vector<expr::term_ref> potential_invariants_next;
    for (size_t i = 0; i < potential_invariants.size(); ++ i) {
      potential_invariants_next.push_back(state_type->change_formula_vars(system::state_type::STATE_CURRENT, system::state_type::STATE_NEXT, potential_invariants[i]));
    }

    // Check the invariants at frame 0
    size_t to_keep = 0;
    for (size_t i = 0; i < potential_invariants.size(); ++ i) {
      expr::term_ref invariant = potential_invariants[i];
      solvers::query_result result = d_smt->query_at(0, tm().mk_term(expr::TERM_NOT, invariant), smt::solver::CLASS_A);
      if (result.result == smt::solver::UNSAT) {
        // holds at 0
        potential_invariants[to_keep++] = invariant;
        potential_invariants_next[to_keep] = potential_invariants_next[i];
      }
    }
    potential_invariants.resize(to_keep);
    potential_invariants_next.resize(to_keep);


    for (size_t k = 1; k <= d_induction_frame; ++ k) {
      to_keep = 0;
      for (size_t i = 0; i < potential_invariants.size(); ++ i) {
        // The invariant
        expr::term_ref invariant = potential_invariants[i];
        expr::term_ref invariant_next = potential_invariants_next[i];
        // Check if we've done it already
        if (frame_contains(1, invariant)) {
          continue;
        }
        // Check if R_{k-1} && T => Inv
        solvers::query_result result = d_smt->query_at(k-1, tm().mk_term(expr::TERM_NOT, invariant_next), smt::solver::CLASS_B);
        if (result.result == smt::solver::UNSAT) {
          // holds at 0
          potential_invariants[to_keep++] = invariant;
          potential_invariants_next[to_keep] = invariant_next;
          add_valid_up_to(k, invariant);
        }
      }
      potential_invariants.resize(to_keep);
      potential_invariants_next.resize(to_keep);
    }
  }
}


void ic3_engine::gc_collect(const expr::gc_relocator& gc_reloc) {
  gc_reloc.reloc(d_counterexample);
  d_frame_formula_info.reloc(gc_reloc);
  assert(d_induction_obligations_next.size() == 0);
  for (size_t i = 0; i < d_frame_content.size(); ++ i) {
    gc_reloc.reloc(d_frame_content[i]);
  }
  d_smt->gc_collect(gc_reloc);
}

void ic3_engine::to_stream(std::ostream& out) const  {
  for (size_t k = 0; k < d_frame_content.size(); ++ k) {
    out << "Frame " << k << ":" << std::endl;
    formula_set::const_iterator it = d_frame_content[k].begin();
    for (; it != d_frame_content[k].end(); ++ it) {
      out << *it << std::endl;
    }
  }
}

void ic3_engine::print_frames(std::ostream& out) const {
  for (size_t k = 0; k < d_frame_content.size(); ++ k) {
    print_frame(k, out);
  }
}

void ic3_engine::print_frame(size_t k, std::ostream& out) const {
  out << tm().mk_and(d_frame_content[k]) << std::endl;
}

size_t ic3_engine::total_facts() const {
  // Frames are smaller and smaller, so we return the first one. But, since we
  // never add to frame 0, we return frame 0 + frame 1
  assert (d_frame_content.size() > 0);
  if (d_frame_content.size() > 1) {
    return d_frame_content[0].size() + d_frame_content[1].size();
  } else {
    return d_frame_content[0].size();
  }
}

std::ostream& operator << (std::ostream& out, const ic3_engine& ic3) {
  ic3.to_stream(out);
  return out;
}

void ic3_engine::set_refutes_info(expr::term_ref f, expr::term_ref g, expr::term_ref l) {
  assert(d_frame_formula_info.find(l) == d_frame_formula_info.end());
  d_frame_formula_info[l] = frame_formula_info(f, g);
}

expr::term_ref ic3_engine::get_refutes(expr::term_ref l) const {
  assert(has_parent(l));
  expr::term_ref_map<frame_formula_info>::const_iterator find = d_frame_formula_info.find(l);
  return find->second.refutes;
}

expr::term_ref ic3_engine::get_parent(expr::term_ref l) const {
  assert(has_parent(l));
  expr::term_ref_map<frame_formula_info>::const_iterator find = d_frame_formula_info.find(l);
  return find->second.parent;
}

bool ic3_engine::has_parent(expr::term_ref l) const {
  expr::term_ref_map<frame_formula_info>::const_iterator find = d_frame_formula_info.find(l);
  if (find == d_frame_formula_info.end()) { return false; }
  return !find->second.parent.is_null();
}

void ic3_engine::output_efsmt(expr::term_ref f, expr::term_ref g) const {

  assert(!f.is_null());
  assert(!g.is_null());

  static size_t i = 0;

  // we have
  //  \forall x G(x) => \exists x' T(x, x') and f is valid
  //  G(x) and \forall y not (T(x, x') and f') is unsat

  std::stringstream ss;
  ss << "ic3_gen_check_" << i++ << ".smt2";
  std::ofstream out(ss.str().c_str());

  out << expr::set_tm(tm());

  const utils::name_transformer* old_transformer = tm().get_name_transformer();
  smt::smt2_name_transformer name_transformer;
  tm().set_name_transformer(&name_transformer);

  out << "(set-logic LRA)" << std::endl;
  out << "(set-info :smt-lib-version 2.0)" << std::endl;
  out << "(set-info :status unsat)" << std::endl;
  out << std::endl;

  const system::state_type* state_type = d_transition_system->get_state_type();

  const std::vector<expr::term_ref>& state_vars = state_type->get_variables(system::state_type::STATE_CURRENT);
  const std::vector<expr::term_ref>& input_vars = state_type->get_variables(system::state_type::STATE_INPUT);
  const std::vector<expr::term_ref>& next_vars = state_type->get_variables(system::state_type::STATE_NEXT);

  for (size_t i = 0; i < state_vars.size(); ++ i) {
    expr::term_ref variable = state_vars[i];
    out << "(declare-fun " << variable << " () " << tm().type_of(variable) << ")" << std::endl;
  }

  out << std::endl;
  out << ";; generalization" << std::endl;
  out << "(assert " << g << ")" << std::endl;

  out << std::endl;
  out << "(assert (forall (";
  for (size_t i = 0; i < input_vars.size(); ++ i) {
    expr::term_ref variable = input_vars[i];
    if (i) out << " ";
    out << "(";
    out << variable << " " << tm().type_of(variable);
    out << ")";
  }
  for (size_t i = 0; i < next_vars.size(); ++ i) {
    expr::term_ref variable = next_vars[i];
    out << " (";
    out << variable << " " << tm().type_of(variable);
    out << ")";
  }
  out << ")" << std::endl; // end forall variables
  out << "(not (and" << std::endl;
  out << "  " << d_transition_system->get_transition_relation() << std::endl;
  out << "  " << state_type->change_formula_vars(system::state_type::STATE_CURRENT, system::state_type::STATE_NEXT, f) << std::endl;
  out << "))" << std::endl; // end negation and and

  out << "))" << std::endl; // end forall

  out << std::endl;
  out << "(check-sat)" << std::endl;

  tm().set_name_transformer(old_transformer);
}


}
}
