#include <boost/test/unit_test.hpp>

#include "smt/solver.h"

#include <iostream>

using namespace std;
using namespace sal2;
using namespace expr;
using namespace smt;

struct term_manager_test_fixture {

  term_manager tm;
  solver* yices2;

public:
  term_manager_test_fixture()
  : tm(true)
  {
    yices2 = factory::mk_solver("yices2", tm);
    cout << set_tm(tm);
  }

  ~term_manager_test_fixture() {
    delete yices2;
  }
};

BOOST_FIXTURE_TEST_SUITE(term_manager_construction, term_manager_test_fixture)

BOOST_AUTO_TEST_CASE(basic_asserts) {

  // Assert something to yices
  term_ref x = tm.mk_term<VARIABLE>("x", tm.realType());
  term_ref y = tm.mk_term<VARIABLE>("y", tm.realType());
  term_ref b = tm.mk_term<VARIABLE>("b", tm.booleanType());
  term_ref zero = tm.mk_term<CONST_RATIONAL>(rational());

  term_ref sum = tm.mk_term<TERM_ADD>(x, y);

  // x + y <= 0
  term_ref leq = tm.mk_term<TERM_LEQ>(sum, zero);

  term_ref f = tm.mk_term<TERM_AND>(b, leq);

  cout << "Adding: " << f << endl;
  yices2->add(f);

  solver::result result = yices2->check();
  cout << "Check result: " << result << endl;

  BOOST_CHECK_EQUAL(result, solver::SAT);

  term_ref g = tm.mk_term<TERM_NOT>(b);

  cout << "Adding: " << g << endl;
  yices2->add(g);

  result = yices2->check();
  cout << "Check result: " << result << endl;

  BOOST_CHECK_EQUAL(result, solver::UNSAT);

}

BOOST_AUTO_TEST_SUITE_END()
