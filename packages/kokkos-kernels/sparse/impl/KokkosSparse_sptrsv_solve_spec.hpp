//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER
#ifndef KOKKOSSPARSE_IMPL_SPTRSV_SOLVE_SPEC_HPP_
#define KOKKOSSPARSE_IMPL_SPTRSV_SOLVE_SPEC_HPP_

#include <KokkosKernels_config.h>
#include <Kokkos_Core.hpp>
#include <Kokkos_ArithTraits.hpp>
#include "KokkosSparse_CrsMatrix.hpp"
#include "KokkosKernels_Handle.hpp"

// Include the actual functors
#if !defined(KOKKOSKERNELS_ETI_ONLY) || KOKKOSKERNELS_IMPL_COMPILE_LIBRARY
#include <KokkosSparse_sptrsv_solve_impl.hpp>
#include <KokkosSparse_sptrsv_symbolic_impl.hpp>
#endif

namespace KokkosSparse {
namespace Impl {
// Specialization struct which defines whether a specialization exists
template <class KernelHandle, class RowMapType, class EntriesType,
          class ValuesType, class BType, class XType>
struct sptrsv_solve_eti_spec_avail {
  enum : bool { value = false };
};

}  // namespace Impl
}  // namespace KokkosSparse

#define KOKKOSSPARSE_SPTRSV_SOLVE_ETI_SPEC_AVAIL(                           \
    SCALAR_TYPE, ORDINAL_TYPE, OFFSET_TYPE, LAYOUT_TYPE, EXEC_SPACE_TYPE,   \
    MEM_SPACE_TYPE)                                                         \
  template <>                                                               \
  struct sptrsv_solve_eti_spec_avail<                                       \
      KokkosKernels::Experimental::KokkosKernelsHandle<                     \
          const OFFSET_TYPE, const ORDINAL_TYPE, const SCALAR_TYPE,         \
          EXEC_SPACE_TYPE, MEM_SPACE_TYPE, MEM_SPACE_TYPE>,                 \
      Kokkos::View<                                                         \
          const OFFSET_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const ORDINAL_TYPE *, LAYOUT_TYPE,                                \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<SCALAR_TYPE *, LAYOUT_TYPE,                              \
                   Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,         \
                   Kokkos::MemoryTraits<Kokkos::Unmanaged> > > {            \
    enum : bool { value = true };                                           \
  };

// Include the actual specialization declarations
#include <KokkosSparse_sptrsv_solve_tpl_spec_avail.hpp>
#include <generated_specializations_hpp/KokkosSparse_sptrsv_solve_eti_spec_avail.hpp>

namespace KokkosSparse {
namespace Impl {

#if defined(KOKKOS_ENABLE_CUDA) && 10000 < CUDA_VERSION && \
    defined(KOKKOSKERNELS_ENABLE_EXP_CUDAGRAPH)
#define KOKKOSKERNELS_SPTRSV_CUDAGRAPHSUPPORT
#endif

// Unification layer
/// \brief Implementation of KokkosSparse::sptrsv_solve

template <class KernelHandle, class RowMapType, class EntriesType,
          class ValuesType, class BType, class XType,
          bool tpl_spec_avail =
              sptrsv_solve_tpl_spec_avail<KernelHandle, RowMapType, EntriesType,
                                          ValuesType, BType, XType>::value,
          bool eti_spec_avail =
              sptrsv_solve_eti_spec_avail<KernelHandle, RowMapType, EntriesType,
                                          ValuesType, BType, XType>::value>
struct SPTRSV_SOLVE {
  static void sptrsv_solve(KernelHandle *handle, const RowMapType row_map,
                           const EntriesType entries, const ValuesType values,
                           BType b, XType x);
};

#if !defined(KOKKOSKERNELS_ETI_ONLY) || KOKKOSKERNELS_IMPL_COMPILE_LIBRARY
//! Full specialization of sptrsv_solve
// Unification layer
template <class KernelHandle, class RowMapType, class EntriesType,
          class ValuesType, class BType, class XType>
struct SPTRSV_SOLVE<KernelHandle, RowMapType, EntriesType, ValuesType, BType,
                    XType, false, KOKKOSKERNELS_IMPL_COMPILE_LIBRARY> {
  static void sptrsv_solve(KernelHandle *handle, const RowMapType row_map,
                           const EntriesType entries, const ValuesType values,
                           BType b, XType x) {
    // Call specific algorithm type
    auto sptrsv_handle = handle->get_sptrsv_handle();
    Kokkos::Profiling::pushRegion(sptrsv_handle->is_lower_tri()
                                      ? "KokkosSparse_sptrsv[lower]"
                                      : "KokkosSparse_sptrsv[upper]");
    if (sptrsv_handle->is_lower_tri()) {
      if (sptrsv_handle->is_symbolic_complete() == false) {
        Experimental::lower_tri_symbolic(*sptrsv_handle, row_map, entries);
      }
      if (sptrsv_handle->get_algorithm() ==
          KokkosSparse::Experimental::SPTRSVAlgorithm::SEQLVLSCHD_TP1CHAIN) {
        Experimental::tri_solve_chain(*sptrsv_handle, row_map, entries, values,
                                      b, x, true);
      } else {
#ifdef KOKKOSKERNELS_SPTRSV_CUDAGRAPHSUPPORT
        using ExecSpace = typename RowMapType::memory_space::execution_space;
        if (std::is_same<ExecSpace, Kokkos::Cuda>::value)
          Experimental::lower_tri_solve_cg(*sptrsv_handle, row_map, entries,
                                           values, b, x);
        else
#endif
          Experimental::lower_tri_solve(*sptrsv_handle, row_map, entries,
                                        values, b, x);
      }
    } else {
      if (sptrsv_handle->is_symbolic_complete() == false) {
        Experimental::upper_tri_symbolic(*sptrsv_handle, row_map, entries);
      }
      if (sptrsv_handle->get_algorithm() ==
          KokkosSparse::Experimental::SPTRSVAlgorithm::SEQLVLSCHD_TP1CHAIN) {
        Experimental::tri_solve_chain(*sptrsv_handle, row_map, entries, values,
                                      b, x, false);
      } else {
#ifdef KOKKOSKERNELS_SPTRSV_CUDAGRAPHSUPPORT
        using ExecSpace = typename RowMapType::memory_space::execution_space;
        if (std::is_same<ExecSpace, Kokkos::Cuda>::value)
          Experimental::upper_tri_solve_cg(*sptrsv_handle, row_map, entries,
                                           values, b, x);
        else
#endif
          Experimental::upper_tri_solve(*sptrsv_handle, row_map, entries,
                                        values, b, x);
      }
    }
    Kokkos::Profiling::popRegion();
  }
};

#endif
}  // namespace Impl
}  // namespace KokkosSparse

//
// Macro for declaration of full specialization of
// This is NOT for users!!!  All
// the declarations of full specializations go in this header file.
// We may spread out definitions (see _DEF macro below) across one or
// more .cpp files.
//
#define KOKKOSSPARSE_SPTRSV_SOLVE_ETI_SPEC_DECL(                            \
    SCALAR_TYPE, ORDINAL_TYPE, OFFSET_TYPE, LAYOUT_TYPE, EXEC_SPACE_TYPE,   \
    MEM_SPACE_TYPE)                                                         \
  extern template struct SPTRSV_SOLVE<                                      \
      KokkosKernels::Experimental::KokkosKernelsHandle<                     \
          const OFFSET_TYPE, const ORDINAL_TYPE, const SCALAR_TYPE,         \
          EXEC_SPACE_TYPE, MEM_SPACE_TYPE, MEM_SPACE_TYPE>,                 \
      Kokkos::View<                                                         \
          const OFFSET_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const ORDINAL_TYPE *, LAYOUT_TYPE,                                \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<SCALAR_TYPE *, LAYOUT_TYPE,                              \
                   Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,         \
                   Kokkos::MemoryTraits<Kokkos::Unmanaged> >,               \
      false, true>;

#define KOKKOSSPARSE_SPTRSV_SOLVE_ETI_SPEC_INST(                            \
    SCALAR_TYPE, ORDINAL_TYPE, OFFSET_TYPE, LAYOUT_TYPE, EXEC_SPACE_TYPE,   \
    MEM_SPACE_TYPE)                                                         \
  template struct SPTRSV_SOLVE<                                             \
      KokkosKernels::Experimental::KokkosKernelsHandle<                     \
          const OFFSET_TYPE, const ORDINAL_TYPE, const SCALAR_TYPE,         \
          EXEC_SPACE_TYPE, MEM_SPACE_TYPE, MEM_SPACE_TYPE>,                 \
      Kokkos::View<                                                         \
          const OFFSET_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const ORDINAL_TYPE *, LAYOUT_TYPE,                                \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<                                                         \
          const SCALAR_TYPE *, LAYOUT_TYPE,                                 \
          Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,                  \
          Kokkos::MemoryTraits<Kokkos::Unmanaged | Kokkos::RandomAccess> >, \
      Kokkos::View<SCALAR_TYPE *, LAYOUT_TYPE,                              \
                   Kokkos::Device<EXEC_SPACE_TYPE, MEM_SPACE_TYPE>,         \
                   Kokkos::MemoryTraits<Kokkos::Unmanaged> >,               \
      false, true>;

#include <KokkosSparse_sptrsv_solve_tpl_spec_decl.hpp>
#include <generated_specializations_hpp/KokkosSparse_sptrsv_solve_eti_spec_decl.hpp>

#endif
