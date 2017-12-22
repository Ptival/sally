#pragma once

#include "system/context.h"
#include "system/state_type.h"
#include "system/state_formula.h"
#include "system/transition_formula.h"

#include "transform.h"

#include <string>

namespace sally {
namespace cmd {
namespace transforms {
  
/** 
    Inline functions.
**/
  
class inliner: public transform {
public:

  // Id is a fresh identifier managed by the context ctx so that new
  // state type, transition system, and state formula are associated
  // to Id. The constructor also creates the new state type from st
  // and it will be managed by the context.
  inliner(system::context *ctx, std::string id, const system::state_type *st);
  
  ~inliner();

  /* Create a new transition system without enum types with
     the given id in the constructor (to be managed by the context) */  
  system::transition_system* apply (const system::transition_system *ts);
  
  /* Create a new state formula semantically equivalent to sf without
     enum types with the given id in the constructor (to be managed by
     the context) */
  system::state_formula* apply(const system::state_formula *sf);

  std::string get_name() const {
    return "Function inliner";
  }
  
private:

  // forward declaration
  class inliner_impl;
  inliner_impl *m_pImpl;
  
};
  
}
}
}
