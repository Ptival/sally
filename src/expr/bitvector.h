/*
 * bitvector.h
 *
 *  Created on: Dec 1, 2014
 *      Author: dejan
 */

#pragma once

#include <iosfwd>
#include "expr/integer.h"
#include "utils/hash.h"

namespace sal2 {
namespace expr {

class bitvector : public integer {

  /** The size in bits */
  size_t d_size;

public:

  /** Construct 0 */
  bitvector(size_t size);

  /** Construct from integer */
  bitvector(size_t size, const integer& z);

  /** Get the size of the bitvector */
  size_t get_size() const { return d_size; }

  /** Hash */
  size_t hash() const;

  /** Compare */
  bool operator == (const bitvector& other) const {
    return d_size == other.d_size && cmp(other) == 0;
  }

  /** Output ot stream */
  void to_stream(std::ostream& out) const;
};

std::ostream& operator << (std::ostream& out, const bitvector& bv);

}

namespace utils {

template<>
struct hash<expr::bitvector> {
  size_t operator()(const expr::bitvector& bv) const {
    return bv.hash();
  }
};

}
}
