// Giga Grains — granular instrument and effect
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026, the Giga Grains authors (see git history)

#ifndef GG_CORE_FPCONTRACT_H
#define GG_CORE_FPCONTRACT_H

/// GG_FP_CONTRACT_OFF: the first statement of every function body in a header whose arithmetic
/// must round the same way on every platform (INTENT §14). An inline function is compiled with
/// the flags of whichever file includes it, so the project's -ffp-contract=off alone doesn't
/// reach a header included by code built with other flags, such as Clang's default
/// -ffp-contract=on, which contracts a * b + c into a fused multiply-add. A function inlined into
/// another keeps its own setting.
///
/// - Clang (ClangCL included): #pragma clang fp contract(off), which keeps that function body from
///   being contracted under -ffp-contract=on or fast-honor-pragmas. Under -ffp-contract=fast Clang
///   disregards the pragma, and nothing in a header can stop it.
/// - GCC: empty. GCC relies on the project-wide -ffp-contract=off (cmake/compiler-options.cmake),
///   so code that includes these headers under GCC must build with that flag too.
#if defined(__clang__)
#define GG_FP_CONTRACT_OFF _Pragma("clang fp contract(off)")
#else
#define GG_FP_CONTRACT_OFF
#endif

#endif // GG_CORE_FPCONTRACT_H
