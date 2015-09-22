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

#include "ai/interval/interval_analyzer.h"
#include <boost/program_options/options_description.hpp>

namespace sally {
namespace interval {

struct interval_analyzer_info {

  static void setup_options(boost::program_options::options_description& options) {
  }

  static std::string get_id() {
    return "interval";
  }

  static analyzer* new_instance(const system::context& ctx) {
    return new interval_analyzer(ctx);
  }

};

}
}
