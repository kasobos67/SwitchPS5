// ProsperoEden - HarfBuzz, compiled into the launcher from its single-file source.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HarfBuzz itself is not part of this repository and is not under this file's licence: the build
// fetches its pinned release (tools/deps.json) and this file only includes it. Its licence is the
// "Old MIT" one in that release's COPYING file.
//
// The launcher shapes text on one thread from OpenType fonts whose tables it trusts (the
// console's own), so the parts for threads, variable fonts, Apple's AAT tables and legacy
// fallbacks are left out.

#if defined(__clang__)
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC system_header
#endif

#define HB_LEAN
#define HB_MINI
#define HB_NO_MT
#include "harfbuzz.cc"
