// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
#define ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>

namespace absl {
namespace base_internal {

template <typename T>
class AtomicHook;

// AtomicHook is a helper class, templatized on a raw function pointer type, for
// implementing Abseil customization hooks.  It is a callable object that
// dispatches to the registered hook, or performs a no-op (and returns a default
// constructed object) if no hook has been registered.
//
// Reads and writes guarantee memory_order_acquire/memory_order_release
// semantics.
template <typename ReturnType, typename... Args>
class AtomicHook<ReturnType (*)(Args...)> {
 public:
  using FnPtr = ReturnType (*)(Args...);

  constexpr AtomicHook() : hook_(kInitialValue) {}

  // Stores the provided function pointer as the value for this hook.
  //
  // This is intended to be called once.  Multiple calls are legal only if the
  // same function pointer is provided for each call.  The store is implemented
  // as a memory_order_release operation, and read accesses are implemented as
  // memory_order_acquire.
  void Store(FnPtr fn) {
    bool success = DoStore(fn);
    static_cast<void>(success);
    assert(success);
  }

  // Invokes the registered callback.  If no callback has yet been registered, a
  // default-constructed object of the appropriate type is returned instead.
  template <typename... CallArgs>
  ReturnType operator()(CallArgs&&... args) const {
    return DoLoad()(std::forward<CallArgs>(args)...);
  }

  // Returns the registered callback, or nullptr if none has been registered.
  // Useful if client code needs to conditionalize behavior based on whether a
  // callback was registered.
  //
  // Note that atomic_hook.Load()() and atomic_hook() have different semantics:
  // operator()() will perform a no-op if no callback was registered, while
  // Load()() will dereference a null function pointer.  Prefer operator()() to
  // Load()() unless you must conditionalize behavior on whether a hook was
  // registered.
  FnPtr Load() const {
    FnPtr ptr = DoLoad();
    return (ptr == DummyFunction) ? nullptr : ptr;
  }

 private:
  static ReturnType DummyFunction(Args...) {
    return ReturnType();
  }

  // Current versions of MSVC (as of September 2017) have a broken
  // implementation of std::atomic<T*>:  Its constructor attempts to do the
  // equivalent of a reinterpret_cast in a constexpr context, which is not
  // allowed.
  //
  // This causes an issue when building with LLVM under Windows.  To avoid this,
  // we use a less-efficient, intptr_t-based implementation on Windows.

#ifdef _MSC_FULL_VER
#define ABSL_HAVE_WORKING_ATOMIC_POINTER 0
#else
#define ABSL_HAVE_WORKING_ATOMIC_POINTER 1
#endif

#if ABSL_HAVE_WORKING_ATOMIC_POINTER
  static constexpr FnPtr kInitialValue = &DummyFunction;

  // Return the stored value, or DummyFunction if no value has been stored.
  FnPtr DoLoad() const { return hook_.load(std::memory_order_acquire); }

  // Store the given value.  Returns false if a different value was already
  // stored to this object.
  bool DoStore(FnPtr fn) {
    assert(fn);
    FnPtr expected = DummyFunction;
    hook_.compare_exchange_strong(expected, fn, std::memory_order_acq_rel,
                                  std::memory_order_acquire);
    const bool store_succeeded = (expected == DummyFunction);
    const bool same_value_already_stored = (expected == fn);
    return store_succeeded || same_value_already_stored;
  }

  std::atomic<FnPtr> hook_;
#else  // !ABSL_HAVE_WORKING_ATOMIC_POINTER
  // Use a sentinel value unlikely to be the address of an actual function.
  static constexpr intptr_t kInitialValue = 0;

  static_assert(sizeof(intptr_t) >= sizeof(FnPtr),
                "intptr_t can't contain a function pointer");

  FnPtr DoLoad() const {
    const intptr_t value = hook_.load(std::memory_order_acquire);
    if (value == 0) {
      return DummyFunction;
    }
    return reinterpret_cast<FnPtr>(value);
  }

  bool DoStore(FnPtr fn) {
    assert(fn);
    const auto value = reinterpret_cast<intptr_t>(fn);
    intptr_t expected = 0;
    hook_.compare_exchange_strong(expected, value, std::memory_order_acq_rel,
                                  std::memory_order_acquire);
    const bool store_succeeded = (expected == 0);
    const bool same_value_already_stored = (expected == value);
    return store_succeeded || same_value_already_stored;
  }

  std::atomic<intptr_t> hook_;
#endif
};

#undef ABSL_HAVE_WORKING_ATOMIC_POINTER

}  // namespace base_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
