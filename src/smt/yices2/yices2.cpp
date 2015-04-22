/*
 * yices.cpp
 *
 *  Created on: Oct 24, 2014
 *      Author: dejan
 */

#ifdef WITH_YICES2

/*
 * BD: added this to work around issues with <stdint.h>. Without this,
 * the macro UINT32_MAX may not be defined in C++ even if you include
 * <stdint.h>.
 *
 * This should not be necessary for recent C++ compilers.
 */
#define __STDC_LIMIT_MACROS 1

#include <gmp.h>
#include <yices.h>
#include <boost/unordered_map.hpp>

#include <iostream>
#include <fstream>
#include <iomanip>

#include "expr/term.h"
#include "expr/term_manager.h"
#include "expr/rational.h"
#include "smt/yices2/yices2.h"
#include "smt/yices2/yices2_internal.h"
#include "utils/trace.h"

#define unused_var(x) { (void) x; }

namespace sally {
namespace smt {

yices2::yices2(expr::term_manager& tm, const options& opts)
: solver("yices2", tm, opts)
{
  d_internal = new yices2_internal(tm, opts);
}

yices2::~yices2() {
  delete d_internal;
}

void yices2::add(expr::term_ref f, formula_class f_class) {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: adding " << f << std::endl;
  d_internal->add(f, f_class);
}

solver::result yices2::check() {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: check()" << std::endl;
  return d_internal->check();
}

void yices2::get_model(expr::model& m) const {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: get_model()" << std::endl;
  d_internal->get_model(m, d_A_variables, d_B_variables);
}

void yices2::push() {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: push()" << std::endl;
  d_internal->push();
}

void yices2::pop() {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: pop()" << std::endl;
  d_internal->pop();
}


void yices2::generalize(generalization_type type, std::vector<expr::term_ref>& projection_out) {
  TRACE("yices2") << "yices2[" << d_internal->instance() << "]: generalizing" << std::endl;
  assert(!d_B_variables.empty());
  switch (type) {
  case GENERALIZE_FORWARD:
    d_internal->generalize(type, projection_out);
    break;
  case GENERALIZE_BACKWARD:
    d_internal->generalize(type, projection_out);
    break;
  }

}

void yices2::add_variable(expr::term_ref var, variable_class f_class) {
  solver::add_variable(var, f_class);
  d_internal->add_variable(var, f_class);
}

void yices2::gc() {
  d_internal->gc();
}

void yices2::gc_collect(const expr::gc_relocator& gc_reloc) {
  solver::gc_collect(gc_reloc);
  d_internal->gc_collect(gc_reloc);
}

}
}

#endif
