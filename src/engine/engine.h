/**
 * This file is part of sally.
 * Copyright (C) 2015 SRI International.
 *
 * Sally is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sally is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sally.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "expr/term_manager.h"
#include "expr/gc_participant.h"
#include "system/context.h"
#include "ai/analyzer.h"

#include <string>
#include "../system/trace_helper.h"

namespace sally {

/**
 * Abstract engine class, and an entrypoint for creating new engines.
 */
class engine : public expr::gc_participant {

  /** The context */
  const system::context& d_ctx;

  /** The analyzer */
  analyzer* d_ai;

protected:

  /** Returns the context of the engine */
  const system::context& ctx() const;

  /** Returns the term manager of the engine */
  expr::term_manager& tm() const;

  /** Returns the analyzer */
  analyzer* ai() const;

public:

  enum result {
    /** The property is valid */
    VALID,
    /** The property is invalid */
    INVALID,
    /** The result is inconclusive */
    UNKNOWN,
    /** The query type is not supported by the engine */
    UNSUPPORTED,
    /** The engine was interrupted */
    INTERRUPTED,
    /** Silent result (e.g. for translator) */
    SILENT
  };

  /** Create the engine */
  engine(const system::context& ctx);

  virtual
  ~engine() {};

  /** Add an AI analyzer */
  void set_analyzer(analyzer* ai);

  /** Query the engine */
  virtual
  result query(const system::transition_system* ts, const system::state_formula* sf) = 0;

  /** Get the counter-example trace, if previous query allows it */
  virtual
  const system::trace_helper* get_trace() = 0;
};

std::ostream& operator << (std::ostream& out, engine::result result);

}
