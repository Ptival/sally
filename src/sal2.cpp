/*
 * sal2.cpp
 *
 *  Created on: Oct 2, 2014
 *      Author: dejan
 */

#include <iostream>
#include <iomanip>

#include "expr/term.h"
#include "parser/parser.h"

using namespace std;

using namespace sal2;
using namespace sal2::expr;

int main(int argc, char* argv[]) {
  parser::parser mcmt_parser(argv[1]);
  mcmt_parser.parse_command();
}
