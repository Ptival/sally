#pragma once

#include "system/state_formula.h"
#include "system/transition_system.h"

#include <string>

namespace sally {
namespace cmd {
namespace transforms {

class transform {
public:
  
  virtual ~transform() {}

  virtual system::transition_system* apply (const system::transition_system *ts) = 0;
  
  virtual system::state_formula* apply(const system::state_formula *sf) = 0;

  virtual std::string get_name() const = 0;
};
}
}
}
