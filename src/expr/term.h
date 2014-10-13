/*
 * term.h
 *
 *  Created on: Oct 2, 2014
 *      Author: dejan
 */

#pragma once

#include <vector>
#include <cassert>
#include <iostream>

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

#include "expr/term_ops.h"
#include "utils/output.h"
#include "utils/allocator.h"
#include "utils/hash.h"

namespace sal2 {
namespace expr {

/** Term references */
class term_ref : public alloc::allocator_base::ref {
  typedef alloc::allocator_base::ref base_ref;

  friend class term_manager;

public:
  term_ref(): base_ref() {}
  term_ref(const base_ref& ref): base_ref(ref) {}
  term_ref(const alloc::empty_type& empty) {}
  void to_stream(std::ostream& out) const;
};

/** Output operator for term references */
inline
std::ostream& operator << (std::ostream& out, const term_ref& t_ref) {
  t_ref.to_stream(out);
  return out;
}

/** Terms */
class term {

  typedef alloc::allocator_base::ref payload_ref;

  /** The term kind */
  term_op d_op;

  /** Default constructor */
  term(): d_op(OP_LAST) {}

  /** Construct the term with all the attributes */
  term(term_op op)
  : d_op(op)
  {}

  friend class term_manager;

public:

  /** Output to the stream using the language set on the stream */
  void to_stream(std::ostream& out) const;
  /** Output to the stream using the SMT2 language */
  void to_stream_smt(std::ostream& out, const term_manager& tm) const;

  /** What kind of term is this */
  term_op op() const { return d_op; }

  /** Number of children, if any */
  size_t size() const {
    return alloc::allocator<term, term_ref>::object_size(*this);
  }

  /** Returns the first child */
  const term_ref* begin() const {
    return alloc::allocator<term, term_ref>::object_begin(*this);
  }

  /** The one past last child */
  const term_ref* end() const {
    return alloc::allocator<term, term_ref>::object_end(*this);
  }

  /** Return the k-th child */
  term_ref operator[] (size_t k) const {
    return begin()[k];
  }
};

/** Output operator for terms */
inline
std::ostream& operator << (std::ostream& out, const term& t) {
  t.to_stream(out);
  return out;
}

/**
 * Strong references.
 */
class term_ref_strong : public term_ref {

  /** Id of the term in the expression manager */
  size_t d_id;

public:

  term_ref_strong()
  : term_ref(), d_id(0) {}
  term_ref_strong(const term_ref& ref, size_t id)
  : term_ref(ref), d_id(id) {}
  term_ref_strong(const term_ref_strong& other)
  { *this = other.finalize(); }

  virtual ~term_ref_strong() {}

  /** Return the "real" version of self. Default, just self. */
  virtual term_ref_strong finalize() const {
    return term_ref_strong(*this, d_id);
  }

  /** The ID of the term. Doesn't change during lifetime */
  size_t id() const { return d_id; }

  /** By default we compare with references */
  virtual bool cmp(const term_ref_strong& other) const {
    return index() == other.index();
  }

  /** Comparison with cmp. If any is null, we use it for this->cmp */
  bool operator == (const term_ref_strong& ref) const;
};

}

namespace utils {

template<>
struct hash<expr::term_ref_strong> {
  size_t operator () (const expr::term_ref_strong& ref) const {
    return ref.id();
  }
};

}

namespace expr {

/**
 * Term manager controls the terms, allocation and garbage collection. All
 * terms are defined in term_ops.h.
 */
class term_manager {

public:

  /** Base references */
  typedef alloc::allocator_base::ref base_ref;

  /** Payload references */
  typedef base_ref payload_ref;

  /** Internal hasher */
  struct term_ref_hasher {
    size_t operator () (const term_ref& ref) const {
      return ref.index();
    }
  };

private:

  /** Whether to typecheck or not */
  bool d_typecheck;

  /** Memory where the terms are kept */
  alloc::allocator<term, term_ref> d_memory;

  /** Memory for the payloads, one for each kind of expression */
  alloc::allocator_base* d_payload_memory[OP_LAST];

  /** Generic term constructor */
  template <term_op op, typename iterator_type>
  term_ref_strong mk_term_internal(const typename term_op_traits<op>::payload_type& payload, iterator_type children_begin, iterator_type children_end);

  /**
   * A term with all the information in the package.
   */
  template <term_op op, typename iterator_type>
  class term_constructor : public term_ref_strong {

    typedef typename term_op_traits<op>::payload_type payload_type;

    /** The term manager */
    term_manager& d_tm;
    /** The payload */
    const payload_type& d_payload;
    /** The first child */
    iterator_type d_begin;
    /** One past last child */
    iterator_type d_end;

  public:

    term_constructor(term_manager& tm, const payload_type& payload, iterator_type begin, iterator_type end)
    : term_ref_strong(term_ref(), tm.term_hash<op, iterator_type>(payload, begin, end))
    , d_tm(tm)
    , d_payload(payload)
    , d_begin(begin)
    , d_end(end)
    {}

    /** Compare to a term op without using the hash. */
    bool cmp(const term_ref_strong& other_ref) const;

    /**
     * Actually construct the reference. This is remembered and guarded from
     * calling more than once.
     */
    virtual term_ref_strong finalize() const {
      return d_tm.mk_term_internal<op, iterator_type>(d_payload, d_begin, d_end);
    }

  };

  /** The underlying hash set */
  typedef boost::unordered_set<term_ref_strong, utils::hash<term_ref_strong> > term_ref_hash_set;

  /** The pool of existing terms */
  term_ref_hash_set d_pool;

  /** Boolean type */
  term_ref d_booleanType;

  /** Integer type */
  term_ref d_integerType;

  /** Real type */
  term_ref d_realType;

  typedef boost::unordered_map<term_ref, term_ref, term_ref_hasher > tcc_map;

  /**
   * Map from terms to their type-checking conditions. If the entry is empty
   * then TCC = true.
   */
  tcc_map d_tcc_map;

  /** Typecheck the term (adds to TCC if needed) */
  bool typecheck(term_ref t);

  /** Compute the has of the term parts */
  template <term_op op, typename iterator_type>
  size_t term_hash(const typename term_op_traits<op>::payload_type& payload, iterator_type begin, iterator_type end);

  /** Max id of any term */
  size_t d_term_ids_max;

  typedef boost::unordered_map<term_ref, size_t, term_ref_hasher > tref_id_map;

  /** Map from term references to their ids */
  tref_id_map d_term_ids;

public:

  /** Construct them manager */
  term_manager(bool typecheck);

  /** Destruct the manager, and destruct all payloads that the manager owns */
  ~term_manager();

  /** Print the term manager information and all the terms to out */
  void to_stream(std::ostream& out) const;

  /** Get the Boolean type */
  term_ref booleanType() const { return d_booleanType; }

  /** Get the Integer type */
  term_ref integerType() const { return d_integerType; }

  /** Get the Real type */
  term_ref realType() const { return d_realType; }

  /** Generic term constructor */
  template <term_op op, typename iterator_type>
  term_ref_strong mk_term(const typename term_op_traits<op>::payload_type& payload, iterator_type children_begin, iterator_type children_end);

  /** Make a term from just payload (for constants) */
  template<term_op op>
  term_ref_strong mk_term(const typename term_op_traits<op>::payload_type& payload) {
    return mk_term<op, term_ref*>(payload, 0, 0);
  }

  /** Make a term from one child and no payload */
  template<term_op op>
  term_ref_strong mk_term(term_ref child) {
    term_ref children[1] = { child };
    return mk_term<op, term_ref*>(alloc::empty_type(), children, children + 1);
  }

  /** Make a term from two children and no payload */
  template<term_op op>
  term_ref_strong mk_term(term_ref child1, term_ref child2) {
    term_ref children[2] = { child1, child2 };
    return mk_term<op, term_ref*>(alloc::empty_type(), children, children + 2);
  }

  /** Make a term with a list of children and no payload */
  template <term_op op, typename iterator_type>
  term_ref_strong mk_term(iterator_type children_begin, iterator_type children_end) {
    return mk_term<op, iterator_type>(alloc::empty_type(), children_begin, children_end);
  }

  /** Make a term from one child and payload */
  template<term_op op>
  term_ref_strong mk_term(const typename term_op_traits<op>::payload_type& payload, term_ref child) {
    term_ref children[1] = { child };
    return mk_term<op, term_ref*>(payload, children, children + 1);
  }

  /** Make a term from two children and payload */
  template<term_op op>
  term_ref_strong mk_term(const typename term_op_traits<op>::payload_type& payload, term_ref child1, term_ref child2) {
    term_ref children[2] = { child1, child2 };
    return mk_term<op, term_ref*>(payload, children, children + 2);
  }

  /** Get a reference for the term */
  term_ref ref_of(const term& term) const {
    return d_memory.ref_of(term);
  }

  /** Get a term of the reference */
  const term& term_of(const term_ref& ref) const {
    return d_memory.object_of(ref);
  }

  /** Get a term payload */
  template<typename payload_type>
  const payload_type& payload_of(const term& t) const {
    assert(d_payload_memory[t.d_op] != 0);
    return d_payload_memory[t.d_op]->object_of<payload_type>(*t.end());
  }

  /**
   * Get the number of children this term has.
   */
  size_t term_size(const term& t) const {
    return d_memory.object_size(t);
  }

  /** First child */
  const term_ref* term_begin(const term& t) const {
    return d_memory.object_begin(t);
  }

  /** End of children (one past) */
  const term_ref* term_end(const term& t) const {
    return d_memory.object_end(t);
  }

  /** Get the type of the term */
  term_ref type_of(const term& t) const;

  /** Get the type of the term */
  term_ref type_of(term_ref t) const {
    return type_of(term_of(t));
  }

  /** Get the TCC of the term */
  term_ref tcc_of(const term& t) const;

  /** Get the TCC of the term */
  term_ref tcc_of(const term_ref& t) const {
    return tcc_of(term_of(t));
  }

  /** Get the id of the term */
  size_t id_of(const term_ref& t) const {
    if (t.is_null()) return 0;
    assert(d_term_ids.find(t) != d_term_ids.end());
    return d_term_ids.find(t)->second;
  }

};

inline
std::ostream& operator << (std::ostream& out, const term_manager& tm) {
  tm.to_stream(out);
  return out;
}

template<>
inline const alloc::empty_type& term_manager::payload_of<alloc::empty_type>(const term& t) const {
  static alloc::empty_type empty;
  return empty;
}

/** IO modifier to set the term manager */
struct set_tm {
  const term_manager* tm;
  set_tm(const term_manager& tm): tm(&tm) {}
};

/** IO manipulator to set the term manager on the stream */
std::ostream& operator << (std::ostream& out, const set_tm& stm);

template <term_op op, typename iterator_type>
size_t term_manager::term_hash(const typename term_op_traits<op>::payload_type& payload, iterator_type begin, iterator_type end) {

  typedef typename term_op_traits<op>::payload_type payload_type;

  // Compute the hash of the term
  utils::sequence_hash hasher;
  hasher.add(op);

  // If there are children, add to the hash
  for (iterator_type it = begin; it != end; ++ it) {
    hasher.add(id_of(*it));
  }

  // If there is a payload, add it to the hash
  if (!alloc::type_traits<payload_type>::is_empty) {
    hasher.add(payload);
  }

  return hasher.get();
}

template <term_op op, typename iterator_type>
term_ref_strong term_manager::mk_term_internal(const typename term_op_traits<op>::payload_type& payload, iterator_type begin, iterator_type end) {

  typedef typename term_op_traits<op>::payload_type payload_type;
  typedef alloc::allocator<payload_type, alloc::empty_type> payload_allocator;

  // Construct the payload if any
  payload_ref p_ref;
  if (!alloc::type_traits<payload_type>::is_empty) {
    // If no payload allocator, construct it
    if (d_payload_memory[op] == 0) {
      d_payload_memory[op] = new payload_allocator();
    }
    // Allocate the payload and copy construct it
    payload_allocator* palloc = ((payload_allocator*) d_payload_memory[op]);
    p_ref = palloc->template allocate<alloc::empty_type*>(payload, 0, 0, 0);
  }

  // Construct the term
  term_ref t_ref;
  if (alloc::type_traits<payload_type>::is_empty) {
    // No payload, 0 for extras
    t_ref = d_memory.allocate(term(op), begin, end, 0);
  } else {
    // Pyaload active, add a child
    t_ref = d_memory.allocate(term(op), begin, end, 1);
    *alloc::allocator<term, term_ref>::object_end(d_memory.object_of(t_ref)) = p_ref;
  }

  // Type-check the term
  if (d_typecheck) {
    if (!typecheck(t_ref)) {
      return term_ref_strong();
    }
  }

  // Set the id of the term and add to terms
  size_t id = d_term_ids_max ++;
  d_term_ids[t_ref] = id;

  // Get the reference
  return term_ref_strong(t_ref, id);
}

template <term_op op, typename iterator_type>
term_ref_strong term_manager::mk_term(const typename term_op_traits<op>::payload_type& payload, iterator_type begin, iterator_type end) {
  // Insert and return the actual term_ref
  return *d_pool.insert(term_constructor<op, iterator_type>(*this, payload, begin, end)).first;
}

/** Compare to a term op without using the hash. */
template <term_op op, typename iterator_type>
bool term_manager::term_constructor<op, iterator_type>::cmp(const term_ref_strong& other_ref) const {

  // If the other reference is null, we're comparing to default => not equal
  if (other_ref.is_null()) {
    return false;
  }

  // The actual term we are comparing with
  const term& other = d_tm.term_of(other_ref);

  // Different ops => not equal
  if (op != d_tm.term_of(other_ref).op()) {
    return false;
  }

  // Different sizes => not equal
  size_t size = std::distance(d_begin, d_end);
  if (size != other.size()) {
    return false;
  }

  // Compare the payloads
  if (!(d_payload == d_tm.payload_of<payload_type>(other))) {
    return false;
  }

  // Compare the children
  iterator_type it_this = d_begin;
  const term_ref* it_other = other.begin();
  for (; it_this != d_end; ++ it_this, ++ it_other) {
    if (it_this->index() != it_other->index()) {
      return false;
    }
  }

  return true;
}

}
}
