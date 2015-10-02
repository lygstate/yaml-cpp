#pragma once

#include "yaml-cpp/dll.h"

namespace YAML {
namespace detail {

template<typename T>
struct ref_holder {
    T *n;
    ~ref_holder<T>() {
        if (n) n->release();
    }
    ref_holder<T>(T* ptr) {
        n = ptr;
    }
    ref_holder<T>(const ref_holder<T>& ref) {
        if (ref.n) {
            ref.n->hold();
        }
        n = ref.n;
    }
    ref_holder<T>(ref_holder<T>&& ref) {
        n = ref.n;
        ref.n = nullptr;
    }
    ref_holder<T>& operator=(const ref_holder<T>& ref) {
        if (ref.n == n) { return *this; }

        if (ref.n) { ref.n->hold(); }
        if (n) { n->release(); }
        n = ref.n;
        return *this;
    }
    ref_holder<T>& operator=(ref_holder<T>&& ref) {
        if (ref.n == n) { return *this; }

        if (n) { n->release(); }
        n = ref.n;
        ref.n = nullptr;

        return *this;
    }
    bool operator<(const ref_holder<T>& ref) const { return n < ref.n; }
    bool operator==(const ref_holder<T>& ref) const { return n == ref.n; }

    const T* operator->() const { return n; }
    T* operator->() { return n; }

    const T& operator*() const { return *n; }
    T& operator*() { return *n; }

    const T* get() { return n; }

    void reset(T* _n) {
        if (_n == n) { return; }
        if (n) { n->release(); }
        n = _n;
    }

    operator bool() const { return n != nullptr; }
};

struct ref_counted {

  void hold() { refs++; }
  void release() {
      if (--refs == 0) {
          delete this;
      }
  }
  virtual ~ref_counted() {}

private:
    std::size_t refs = 1;

};

}
}
