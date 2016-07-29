#include "define_transition_system.h"

#include <iostream>

namespace sally {
namespace cmd {

define_transition_system::define_transition_system(std::string id, system::transition_system* system)
: command(DEFINE_TRANSITION_SYSTEM)
, d_id(id)
, d_system(system)
{}

void define_transition_system::run(system::context* ctx, engine* e) {
  ctx->add_transition_system(d_id, d_system);
  d_system = 0;
}

define_transition_system::~define_transition_system() {
  delete d_system;
}

void define_transition_system::to_stream(std::ostream& out) const  {
  out << "[" << get_command_type_string() << "(" << d_id << "): ";
  out << *d_system;
  out << "]";
}

}
}
