/*
 * allocator.h
 *
 *  Created on: Oct 3, 2014
 *      Author: dejan
 */

#pragma once

#include <vector>
#include <cstdlib>
#include <typeinfo>
#include <iostream>
#include <cassert>

#include "utils/hash.h"
#include "utils/allocator_types.h"

namespace sal2 {
namespace alloc {

/**
 * Base allocator does the basic allocation stuff.
 */
class allocator_base {

  /** The memory */
  char* d_memory;

  /** Used memory */
  size_t d_size;

  /** Available memory */
  size_t d_capacity;

public:

  /** Constructor */
  allocator_base(size_t initial_size = 10000)
  : d_memory(static_cast<char*>(std::malloc(initial_size)))
  , d_size(0)
  , d_capacity(initial_size)
  {}

  /** Destructor just frees the memory, stuff inside needs to be destructed by hand */
  virtual ~allocator_base() {
    std::free(d_memory);
  }

  /** Allocate at least size bytes and return the pointer */
  template<typename T>
  T* allocate(size_t size);

  /** Returns the index in memory of the given object */
  template<typename T>
  size_t index_of(const T& o) const {
    return (const char*)&o - d_memory;
  }

  /** Returns the reference of the given object */
  template<typename T>
  ref ref_of(const T& o) { return ref(index_of(o)); }

  /** Returns the object pointed to by the given reference */
  template<typename T>
  const T& object_of(ref o_ref) const { return *((const T*)(d_memory + o_ref.d_ref)); }

  /** Returns the object pointed to by the given reference */
  template<typename T>
  T& object_of(ref o_ref) { return *((T*)(d_memory + o_ref.d_ref)); }

  /** Print out some info */
  virtual void to_stream(std::ostream& out) const {
    out << "(size = " << d_size << ", capacity = " << d_capacity << ")";
  }
};

inline
std::ostream& operator << (std::ostream& out, const allocator_base& alloc) {
  alloc.to_stream(out);
  return out;
}

template<typename T>
T* allocator_base::allocate(size_t size) {

  // Align the size
  size = (size + 7) & ~((size_t)7);

  // Make sure there is enough memory
  size_t requested = d_size + size;
  if (requested > d_capacity) {
    while (requested > d_capacity) {
      d_capacity += d_capacity / 2;
    }
    d_memory = (char*) std::realloc(d_memory, d_capacity);
  }

  // Actually allocate
  T* o = (T*)(d_memory + d_size);
  // Increase the d_size
  d_size += size;
  // Return the clause memory
  return o;
}

/**
 * Allocator of objects that have a structure as
 *
 *  - T data
 *  - size_t size
 *  - E elements
 *
 * In other words, the object consist of initial data, and then an inlined
 * array of elements with the size fixed. To ommit the trailing array, just
 * use empty_type for E.
 */
template<typename T, typename E = empty_type>
class allocator : public allocator_base {
public:

  /**
   * The reference class for the <T, E> type of objects.
   */
  class ref : public alloc::ref {
    friend class allocator<T, E>;
    ref(size_t ref): alloc::ref(ref) {}
  public:
    ref(): alloc::ref(-1) {}
    ref(const alloc::ref& ref): alloc::ref(ref) {}
    static const ref null;
  };

private:

  struct data {
    T t_data;
    size_t e_size;
    E e_data[0];

    template <typename iterator>
    void construct(const T& data, iterator begin, iterator end, size_t extras) {
      new (&t_data) T(data);
      if (!type_traits<E>::is_empty) {
        E* e = e_data;
        for (; begin != end; ++ begin, ++ e) {
          new (e) E(*begin);
        }
        for (size_t i = 0; i < extras; ++ i, ++ e) {
          new (e) E();
        }
      }
    }
  };

  /** All the allocated objects, so that we can destruct it later */
  std::vector<alloc::ref> d_allocated;

public:

  /**
   * Allocate T with children from begin .. end, with potentially extra
   * children. The extras are not destructed automatically so use only for
   * simple types E.
   */
  template<typename iterator>
  ref allocate(const T& t, iterator begin, iterator end, size_t extras) {
    data* full;
    if (type_traits<E>::is_empty) {
      assert(extras == 0);
      full = allocator_base::allocate<data>(sizeof(T));
    } else {
      size_t size = std::distance(begin, end);
      full = allocator_base::allocate<data>(sizeof(data) + (size + extras)*sizeof(E));
      full->e_size = size;
    }
    full->construct(t, begin, end, extras);
    ref t_ref(allocator_base::index_of(*full));
    d_allocated.push_back(t_ref);
    return t_ref;
  }

  /** Get the reference of the object */
  ref ref_of(const T& o) const { return ref(allocator_base::index_of(o)); }

  /** Get the object given the reference */
  const T& object_of(ref o_ref) const {
    const data& d = allocator_base::object_of<data>(o_ref);
    return d.t_data;
  }

  /** Get the object given the reference */
  T& object_of(ref o_ref) {
    data& d = allocator_base::object_of<data>(o_ref);
    return d.t_data;
  }

  /** Get the number of children */
  static size_t object_size(const T& o) {
    const data& d = (const data&) o;
    return d.e_size;
  }

  /** Get the first child */
  static const E* object_begin(const T& o) {
    const data& d = (const data&) o;
    return d.e_data;
  }

  /** Get the last child */
  static const E* object_end(const T& o) {
    const data& d = (const data&) o;
    return d.e_data + d.e_size;
  }

  /** Get the first child */
  static E* object_begin(T& o) {
    data& d = (data&) o;
    return d.e_data;
  }

  /** Get the last child */
  static E* object_end(T& o) {
    data& d = (data&) o;
    return d.e_data + d.e_size;
  }

  /** Destructor, destructs all Ts and Es */
  ~allocator() {
    for (unsigned i = 0; i < d_allocated.size(); ++ i) {
      alloc::ref o_ref = d_allocated[i];
      data& d = allocator_base::object_of<data>(o_ref);
      // Destruct Es
      if (!type_traits<E>::is_empty) {
        for (unsigned i = 0; i < d.e_size; ++ i) {
         d.e_data[i].~E();
        }
      }
      // Destruct T
      d.t_data.~T();
    }
  }

  void to_stream(std::ostream& out) const {
    out << "allocator<" << typeid(T).name() << ">";
    allocator_base::to_stream(out);
  }
};

}
}
