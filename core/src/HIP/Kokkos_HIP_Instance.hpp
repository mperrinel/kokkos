/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

/*--------------------------------------------------------------------------*/

#ifndef KOKKOS_HIP_INSTANCE_HPP
#define KOKKOS_HIP_INSTANCE_HPP

#include <HIP/Kokkos_HIP_Space.hpp>
#include <HIP/Kokkos_HIP_Error.hpp>

#include <mutex>

namespace Kokkos {
namespace Impl {

struct HIPTraits {
#if defined(KOKKOS_ARCH_VEGA)
  static int constexpr WarpSize       = 64;
  static int constexpr WarpIndexMask  = 0x003f; /* hexadecimal for 63 */
  static int constexpr WarpIndexShift = 6;      /* WarpSize == 1 << WarpShift*/
#elif defined(KOKKOS_ARCH_NAVI)
  static int constexpr WarpSize       = 32;
  static int constexpr WarpIndexMask  = 0x001f; /* hexadecimal for 31 */
  static int constexpr WarpIndexShift = 5;      /* WarpSize == 1 << WarpShift*/
#endif
  static int constexpr ConservativeThreadsPerBlock =
      256;  // conservative fallback blocksize in case of spills
  static int constexpr MaxThreadsPerBlock =
      1024;  // the maximum we can fit in a block
  static int constexpr ConstantMemoryUsage        = 0x008000; /* 32k bytes */
  static int constexpr KernelArgumentLimit        = 0x001000; /*  4k bytes */
  static int constexpr ConstantMemoryUseThreshold = 0x000200; /* 512 bytes */
};

//----------------------------------------------------------------------------

HIP::size_type hip_internal_maximum_warp_count();
std::array<HIP::size_type, 3> hip_internal_maximum_grid_count();
HIP::size_type hip_internal_multiprocessor_count();

HIP::size_type *hip_internal_scratch_space(const HIP &instance,
                                           const std::size_t size);
HIP::size_type *hip_internal_scratch_flags(const HIP &instance,
                                           const std::size_t size);

//----------------------------------------------------------------------------

class HIPInternal {
 private:
  HIPInternal(const HIPInternal &);
  HIPInternal &operator=(const HIPInternal &);

 public:
  using size_type = ::Kokkos::HIP::size_type;

  inline static int m_hipDev                        = -1;
  inline static int m_hipArch                       = -1;
  inline static unsigned m_multiProcCount           = 0;
  inline static unsigned m_maxWarpCount             = 0;
  inline static std::array<size_type, 3> m_maxBlock = {0, 0, 0};
  inline static unsigned m_maxWavesPerCU            = 0;
  inline static unsigned m_maxSharedWords           = 0;
  inline static int m_shmemPerSM                    = 0;
  inline static int m_maxShmemPerBlock              = 0;
  inline static int m_maxThreadsPerSM               = 0;

  inline static hipDeviceProp_t m_deviceProp;

  // Scratch Spaces for Reductions
  std::size_t m_scratchSpaceCount = 0;
  std::size_t m_scratchFlagsCount = 0;

  size_type *m_scratchSpace = nullptr;
  size_type *m_scratchFlags = nullptr;

  hipStream_t m_stream = nullptr;
  uint32_t m_instance_id =
      Kokkos::Tools::Experimental::Impl::idForInstance<HIP>(
          reinterpret_cast<uintptr_t>(this));
  bool m_manage_stream = false;

  // Team Scratch Level 1 Space
  int m_n_team_scratch                            = 10;
  mutable int64_t m_team_scratch_current_size[10] = {};
  mutable void *m_team_scratch_ptr[10]            = {};
  mutable std::atomic_int m_team_scratch_pool[10] = {};
  std::int32_t *m_scratch_locks;

  bool was_finalized = false;

  // FIXME_HIP: these want to be per-device, not per-stream...  use of 'static'
  // here will break once there are multiple devices though
  inline static unsigned long *constantMemHostStaging = nullptr;
  inline static hipEvent_t constantMemReusable        = nullptr;
  inline static std::mutex constantMemMutex;

  static HIPInternal &singleton();

  int verify_is_initialized(const char *const label) const;

  int is_initialized() const {
    return nullptr != m_scratchSpace && nullptr != m_scratchFlags;
  }

  void initialize(hipStream_t stream, bool manage_stream);
  void finalize();

  void print_configuration(std::ostream &) const;

  void fence() const;
  void fence(const std::string &) const;

  ~HIPInternal();

  HIPInternal() = default;

  // Resizing of reduction related scratch spaces
  size_type *scratch_space(const std::size_t size);
  size_type *scratch_flags(const std::size_t size);
  uint32_t impl_get_instance_id() const noexcept;
  int acquire_team_scratch_space();
  // Resizing of team level 1 scratch
  void *resize_team_scratch_space(int scratch_pool_id, std::int64_t bytes,
                                  bool force_shrink = false);
  void release_team_scratch_space(int scratch_pool_id);
};

}  // namespace Impl

namespace Experimental {
// Partitioning an Execution Space: expects space and integer arguments for
// relative weight
//   Customization point for backends
//   Default behavior is to return the passed in instance

namespace Impl {
inline void create_HIP_instances(std::vector<HIP> &instances) {
  for (int s = 0; s < int(instances.size()); s++) {
    hipStream_t stream;
    KOKKOS_IMPL_HIP_SAFE_CALL(hipStreamCreate(&stream));
    instances[s] = HIP(stream, true);
  }
}
}  // namespace Impl

template <class... Args>
std::vector<HIP> partition_space(const HIP &, Args...) {
  static_assert(
      (... && std::is_arithmetic_v<Args>),
      "Kokkos Error: partitioning arguments must be integers or floats");

  std::vector<HIP> instances(sizeof...(Args));
  Impl::create_HIP_instances(instances);
  return instances;
}

template <class T>
std::vector<HIP> partition_space(const HIP &, std::vector<T> &weights) {
  static_assert(
      std::is_arithmetic<T>::value,
      "Kokkos Error: partitioning arguments must be integers or floats");

  std::vector<HIP> instances(weights.size());
  Impl::create_HIP_instances(instances);
  return instances;
}
}  // namespace Experimental
}  // namespace Kokkos

#endif
