/*
 * command.cpp
 *
 *  Created on: Nov 12, 2014
 *      Author: dejan
 */

#include "parser/command.h"

#include <cassert>
#include <iostream>

using namespace sal2;
using namespace parser;

command::command(const system::context& ctx, command::type t)
: d_ctx(ctx)
, d_type(t)
{}

std::string command::get_command_type_string() const {

#define CASE_TO_STRING(TYPE) case TYPE: return #TYPE; break;
switch (d_type) {
  CASE_TO_STRING(DECLARE_STATE_TYPE)
  CASE_TO_STRING(DEFINE_STATES)
  CASE_TO_STRING(DEFINE_TRANSITION)
  CASE_TO_STRING(DEFINE_TRANSITION_SYSTEM)
  CASE_TO_STRING(QUERY)
default:
  assert(false);
}

return "unknown";

#undef CASE_TO_STRING
}

void declare_state_type_command::to_stream(std::ostream& out) const  {
  out << "[" << get_command_type_string() << "(" << d_state_id << "): ";
  out << *get_context().get_state_type(d_state_id);
  out << "]";
}

void define_states_command::to_stream(std::ostream& out) const  {
  out << "[" << get_command_type_string() << "(" << d_id << "): ";
  out << *get_context().get_state_formula(d_id);
  out << "]";
}

void define_transition_command::to_stream(std::ostream& out) const {
  out << "[" << get_command_type_string() << "(" << d_transition_id << "): ";
  out << *get_context().get_transition_formula(d_transition_id);
  out << "]";
}

void define_transition_system_command::to_stream(std::ostream& out) const  {
  out << "[" << get_command_type_string() << "(" << d_system_id << "): ";
  out << *get_context().get_transition_system(d_system_id);
  out << "]";
}

void query_command::to_stream(std::ostream& out) const  {
  out << "[" << get_command_type_string() << " " << d_system_id << " " << *d_query << "]";
}

void query_command::run(engine* e) const {
  // Check the formula
  const system::transition_system* T = get_context().get_transition_system(d_system_id);
  engine::result result = e->query(*T, d_query);
  std::cout << result << std::endl;
  // If invalid, show the trace
  if (result == engine::INVALID && get_context().get_options().has_option("show-trace")) {
    const system::state_trace* trace = e->get_trace();
    std::cout << *trace << std::endl;
  }
}
