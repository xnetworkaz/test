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
// Functions for directly invoking mmap() via syscall, avoiding the case where
// mmap() has been locally overridden.

#ifndef ABSL_BASE_INTERNAL_DIRECT_MMAP_H_
#define ABSL_BASE_INTERNAL_DIRECT_MMAP_H_

#include "absl/base/config.h"

#if ABSL_HAVE_MMAP

#include <sys/mman.h>

#ifdef __linux__

#include <sys/types.h>
#ifdef __BIONIC__
#include <sys/syscall.h>
#else
#include <syscall.h>
#endif

#include <linux/unistd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>

#ifdef __mips__
// Include definitions of the ABI currently in use.
#ifdef __BIONIC__
// Android doesn't have sgidefs.h, but does have asm/sgidefs.h, which has the
// definitions we need.
#include <asm/sgidefs.h>
#else
#include <sgidefs.h>
#endif  // __BIONIC__
#endif  // __mips__

// SYS_mmap and SYS_munmap are not defined in Android.
#ifdef __BIONIC__
extern "C" void* __mmap2(void*, size_t, int, int, int, long);
#if defined(__NR_mmap) && !defined(SYS_mmap)
#define SYS_mmap __NR_mmap
#endif
#ifndef SYS_munmap
#define SYS_munmap __NR_munmap
#endif
#endif  // __BIONIC__

namespace absl {
namespace base_internal {

// Platform specific logic extracted from
// https://chromium.googlesource.com/linux-syscall-support/+/master/linux_syscall_support.h
inline void* DirectMmap(void* start, size_t length, int prot, int flags, int fd,
                        off64_t offset) noexcept {
#if defined(__i386__) || defined(__ARM_ARCH_3__) || defined(__ARM_EABI__) || \
    (defined(__mips__) && _MIPS_SIM == _MIPS_SIM_ABI32) ||                   \
    (defined(__PPC__) && !defined(__PPC64__)) ||                             \
    (defined(__s390__) && !defined(__s390x__))
  // On these architectures, implement mmap with mmap2.
  static int pagesize = 0;
  if (pagesize == 0) {
    pagesize = getpagesize();
  }
  if (offset < 0 || offset % pagesize != 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }
#ifdef __BIONIC__
  // SYS_mmap2 has problems on Android API level <= 16.
  // Workaround by invoking __mmap2() instead.
  return __mmap2(start, length, prot, flags, fd, offset / pagesize);
#else
  return reinterpret_cast<void*>(
      syscall(SYS_mmap2, start, length, prot, flags, fd,
              static_cast<off_t>(offset / pagesize)));
#endif
#elif defined(__s390x__)
  // On s390x, mmap() arguments are passed in memory.
  uint32_t buf[6] = {
      reinterpret_cast<uint32_t>(start), static_cast<uint32_t>(length),
      static_cast<uint32_t>(prot),       static_cast<uint32_t>(flags),
      static_cast<uint32_t>(fd),         static_cast<uint32_t>(offset)};
  return reintrepret_cast<void*>(syscall(SYS_mmap, buf));
#elif defined(__x86_64__)
// The x32 ABI has 32 bit longs, but the syscall interface is 64 bit.
// We need to explicitly cast to an unsigned 64 bit type to avoid implicit
// sign extension.  We can't cast pointers directly because those are
// 32 bits, and gcc will dump ugly warnings about casting from a pointer
// to an integer of a different size. We also need to make sure __off64_t
// isn't truncated to 32-bits under x32.
#define MMAP_SYSCALL_ARG(x) ((uint64_t)(uintptr_t)(x))
  return reinterpret_cast<void*>(
      syscall(SYS_mmap, MMAP_SYSCALL_ARG(start), MMAP_SYSCALL_ARG(length),
              MMAP_SYSCALL_ARG(prot), MMAP_SYSCALL_ARG(flags),
              MMAP_SYSCALL_ARG(fd), static_cast<uint64_t>(offset)));
#undef MMAP_SYSCALL_ARG
#else  // Remaining 64-bit aritectures.
  static_assert(sizeof(unsigned long) == 8, "Platform is not 64-bit");
  return reinterpret_cast<void*>(
      syscall(SYS_mmap, start, length, prot, flags, fd, offset));
#endif
}

inline int DirectMunmap(void* start, size_t length) {
  return static_cast<int>(syscall(SYS_munmap, start, length));
}

}  // namespace base_internal
}  // namespace absl

#else  // !__linux__

// For non-linux platforms where we have mmap, just dispatch directly to the
// actual mmap()/munmap() methods.

namespace absl {
namespace base_internal {

inline void* DirectMmap(void* start, size_t length, int prot, int flags, int fd,
                        off_t offset) {
  return mmap(start, length, prot, flags, fd, offset);
}

inline int DirectMunmap(void* start, size_t length) {
  return munmap(start, length);
}

}  // namespace base_internal
}  // namespace absl

#endif  // __linux__

#endif  // ABSL_HAVE_MMAP

#endif  // ABSL_BASE_INTERNAL_DIRECT_MMAP_H_
