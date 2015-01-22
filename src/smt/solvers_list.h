/*
 * solvers_list.h
 *
 *  Created on: Nov 26, 2014
 *      Author: dejan
 */

#pragma once

//
// ADD ALL THE ENGINES HERE
//

#include "smt/yices2/yices2_info.h"
#include "smt/generic/generic_solver_info.h"

sal2::smt::solver_data::solver_data() {
#ifdef WITH_YICES2
  add_module_info<yices2_info>();
#endif
  add_module_info<generic_solver_info>();
}

std::string sal2::smt::factory::get_default_solver_id() {
  if (s_default_solver.empty()) {
#ifdef WITH_YICES2
    s_default_solver = yices2_info::get_id();
#else
    s_default_solver = generic_solver_info::get_id();
#endif
  }
  return s_default_solver;
}


