/*
 * term.cpp
 *
 *  Created on: Oct 2, 2014
 *      Author: dejan
 */

#include "expr/term.h"
#include "utils/output.h"

#include <iomanip>
#include <cassert>

namespace sal2 {
namespace expr {

term_manager::term_manager(bool typecheck)
: d_typecheck(typecheck)
{
  // Initalize all payload memory to 0
  for (unsigned i = 0; i < OP_LAST; ++ i) {
    d_payload_memory[i] = 0;
  }

  // Create the types
  d_booleanType = mk_term<OP_TYPE_BOOL>(alloc::empty);
  d_integerType = mk_term<OP_TYPE_INTEGER>(alloc::empty);
  d_realType = mk_term<OP_TYPE_REAL>(alloc::empty);
}

void term_manager::term_ref::to_stream(std::ostream& out) const {
  if (is_null()) {
    out << "null";
  } else {
    const term_manager* tm = output::get_term_manager(out);
    if (tm == 0) {
      out << d_ref;
    } else {
      tm->term_of(*this).to_stream(out);
    }
  }
}

void term_manager::term::to_stream(std::ostream& out) const {
  output::language lang = output::get_output_language(out);
  const term_manager* tm = output::get_term_manager(out);
  switch (lang) {
  case output::SMTLIB:
    to_stream_smt(out, *tm);
    break;
  default:
    assert(false);
  }
}

static inline
std::string get_smt_keyword(term_op op) {
  switch (op) {
  case OP_AND:
    return "and";
  case OP_OR:
    return "or";
  case OP_NOT:
    return "not";
  case OP_IMPLIES:
    return "implies";
  case OP_XOR:
    return "xor";
  case OP_ADD:
    return "+";
  case OP_SUB:
    return "-";
  case OP_MUL:
    return "*";
  case OP_DIV:
    return "/";
  case OP_TYPE_BOOL:
    return "Bool";
  case OP_TYPE_INTEGER:
    return "Integer";
  case OP_TYPE_REAL:
    return "Real";
  default:
    assert(false);
    return "unknown";
  }
}

void term_manager::term::to_stream_smt(std::ostream& out, const term_manager& tm) const {
  switch (d_op) {
  case OP_VARIABLE:
    out << tm.payload_of<std::string>(*this);
    break;
  case OP_BOOL_CONSTANT:
    out << (tm.payload_of<bool>(*this) ? "true" : "false");
    break;
  case OP_AND:
  case OP_OR:
  case OP_NOT:
  case OP_IMPLIES:
  case OP_XOR:
  case OP_ADD:
  case OP_SUB:
  case OP_MUL:
  case OP_DIV: {
    if (size() > 0) {
      out << "(";
    }
    out << get_smt_keyword(d_op);
    for (const term_ref* it = begin(); it != end(); ++ it) {
      out << " " << *it;
    }
    if (size() > 0) {
      out << ")";
    }
    break;
  }
  case OP_REAL_CONSTANT:
    // Stream is already in SMT mode
    out << tm.payload_of<rational>(*this);
    break;
  default:
    assert(false);
  }
}

term_manager::~term_manager() {
  for (unsigned i = 0; i < OP_LAST; ++ i) {
    delete d_payload_memory[i];
  }
}

std::ostream& operator << (std::ostream& out, const set_tm& stm) {
  output::set_term_manager(out, stm.tm);
  return out;
}

bool term_manager::typecheck(term_ref t_ref) {
  const term& t = term_of(t_ref);

  switch (t.op()) {
  case OP_TYPE_BOOL:
  case OP_TYPE_INTEGER:
  case OP_TYPE_REAL:
  case OP_VARIABLE:
    return true;
  // Equality
  case OP_EQ:

    break;

  // Boolean terms
  case OP_BOOL_CONSTANT:
    return true;
  case OP_AND:
  case OP_OR:
  case OP_XOR:
    // Check types of arguments
  case OP_NOT:
    // One argument
  case OP_IMPLIES:
    // Only binary
    break;
  // Arithmetic terms
  case OP_REAL_CONSTANT:
    return true;
  case OP_ADD:
  case OP_MUL:
    // Check arguments
  case OP_SUB:
    // Only binary
  case OP_DIV:
    // Binary with TCC: den != 0
  default:
    assert(false);
  }

  return true;
}


}
}
