/*
 * parser.h
 *
 *  Created on: Nov 4, 2014
 *      Author: dejan
 */

#pragma once

#include "parser/command.h"

namespace sal2 {
namespace parser {

class parser_internal;

class parser {

  parser_internal* d_internal;

public:

  parser(const char* filename);
  ~parser();

  /** Parse the next command from the input */
  command* parse_command();
};

}
}
