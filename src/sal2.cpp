/*
 * sal2.cpp
 *
 *  Created on: Oct 2, 2014
 *      Author: dejan
 */

#include <iostream>
#include <boost/program_options.hpp>

#include "expr/term_manager.h"
#include "utils/output.h"
#include "system/context.h"
#include "parser/parser.h"
#include "engine/factory.h"
#include "smt/factory.h"

using namespace std;
using namespace boost::program_options;

using namespace sal2;
using namespace sal2::expr;

/** Parses the program arguments. */
void parseOptions(int argc, char* argv[], variables_map& variables);

int main(int argc, char* argv[]) {

  // Get the options from command line and config files
  variables_map boost_opts;
  parseOptions(argc, argv, boost_opts);
  options opts(boost_opts);

  // Get the files to run
  vector<string>& files = boost_opts.at("input").as< vector<string> >();

  // Set the verbosity
  output::set_verbosity(cout, opts.get_unsigned("verbosity"));
  output::set_verbosity(cerr, opts.get_unsigned("verbosity"));

  // Typecheck by default
  bool type_check = true;

  // Create the term manager
  expr::term_manager tm(type_check);
  cout << expr::set_tm(tm);
  cerr << expr::set_tm(tm);

  // Create the context
  system::context ctx(tm, opts);

  // Set the default solver for the solver factory
  if (opts.has_option("solver")) {
    smt::factory::set_default_solver(opts.get_string("solver"));
  }

  // Create the engine
  engine* engine_to_use = 0;
  if (opts.has_option("engine") > 0) {
    try {
      engine_to_use = factory::mk_engine(boost_opts.at("engine").as<string>(), ctx);
    } catch (const sal2::exception& e) {
      cerr << e << endl;
      exit(1);
    }
  }

  // Go through all the files and run them
  for (size_t i = 0; i < files.size(); ++ i) {

    try {
      if (output::get_verbosity(cout) > 0) {
        cout << "Processing " << files[i] << endl;
      }

      // Create the parser
      parser::parser mcmt_parser(ctx, files[i].c_str());

      // Parse an process each command
      for (parser::command* cmd = mcmt_parser.parse_command(); cmd != 0; delete cmd, cmd = mcmt_parser.parse_command()) {

        if (output::get_verbosity(cout) > 0) {
          cout << "Got command " << *cmd << endl;
        }

        // If only parsing, just ignore the command
        if (opts.has_option("parse-only")) {
          continue;
        }

        // Run the command
        if (engine_to_use) {
          cmd->run(engine_to_use);
        }
      }

    } catch (sal2::exception& e) {
      cerr << e << std::endl;
      exit(1);
    }
  }

  // Delete the engine
  if (engine_to_use != 0) {
    delete engine_to_use;
  }
}

std::string get_engines_list() {
  std::vector<string> engines;
  factory::get_engines(engines);
  std::stringstream out;
  out << "The engine to use: ";
  for (size_t i = 0; i < engines.size(); ++ i) {
    if (i) { out << ", "; }
    out << engines[i];
  }
  return out.str();
}

std::string get_solvers_list() {
  std::vector<string> solvers;
  smt::factory::get_solvers(solvers);
  std::stringstream out;
  out << "The SMT solver to use: ";
  for (size_t i = 0; i < solvers.size(); ++ i) {
    if (i) { out << ", "; }
    out << solvers[i];
  }
  return out.str();
}


void parseOptions(int argc, char* argv[], variables_map& variables)
{
  // Define the main options
  options_description description("General options");
  description.add_options()
      ("help,h", "Prints this help message.")
      ("verbosity,v", value<unsigned>()->default_value(0), "Set the verbosity of the output.")
      ("input,i", value<vector<string> >()->required(), "A problem to solve.")
      ("parse-only", "Just parse, don't solve.")
      ("engine", value<string>(), get_engines_list().c_str())
      ("solver", value<string>(), get_solvers_list().c_str())
      ;

  // Get the individual engine options
  factory::setup_options(description);

  // Get the individual solver options
  smt::factory::setup_options(description);

  // The input files can be positional
  positional_options_description positional;
  positional.add("input", -1);

  // Parse the options
  bool parseError = false;
  try {
    store(command_line_parser(argc, argv).options(description).positional(positional).run(), variables);
  } catch (...) {
    parseError = true;
  }

  // If help needed, print it out
  if (parseError || variables.count("help") > 0 || variables.count("input") == 0) {
    if (parseError) {
      cout << "Error parsing command line!" << endl;
    }
    cout << "Usage: " << argv[0] << " [options] input ..." << endl;
    cout << description << endl;
    if (parseError) {
      exit(1);
    } else {
      exit(0);
    }
  }
}
