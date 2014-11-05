/*
 * output.cpp
 *
 *  Created on: Oct 3, 2014
 *      Author: dejan
 */

#include <string>

#include "utils/output.h"

namespace sal2 {
namespace output {

static
void*& __get_term_manager(std::ostream& out) {
  static const int xindex = std::ios_base::xalloc();
  return out.pword(xindex);
}

static
long& __get_output_language(std::ostream& out) {
  static const int x_index = std::ios_base::xalloc();
  return out.iword(x_index);
}

static
long& __get_verbosity(std::ostream& out) {
  static const int x_index = std::ios_base::xalloc();
  return out.iword(x_index);
}

const expr::term_manager* get_term_manager(std::ostream& out) {
  return (expr::term_manager*) __get_term_manager(out);
}

void set_term_manager(std::ostream& out, const expr::term_manager* tm) {
  __get_term_manager(out) = (void*) tm;
}

language get_output_language(std::ostream& out) {
  return (language) __get_output_language(out);
}

void set_output_language(std::ostream& out, language lang) {
  __get_output_language(out) = lang;
}

size_t get_verbosity(std::ostream& out) {
  return __get_verbosity(out);
}

/** Set the verbosity */
void set_verbosity(std::ostream& out, size_t verbosity) {
  __get_verbosity(out) = verbosity;
}

}
}
