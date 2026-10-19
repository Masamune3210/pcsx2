// SPDX-FileCopyrightText: 2026
// SPDX-License-Identifier: GPL-3.0+
//
// Standalone MSBuild harness only. R5900.h binds namespace-scope references
// cpuRegs/fpuRegs to this symbol even though EEActiveTLB's pure selector
// does not examine live CPU state. The normal emulator defines it in
// pcsx2/R5900.cpp; do not link that compilation unit into this test.
#include "R5900.h"

alignas(16) cpuRegistersPack _cpuRegistersPack{};
