// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "x86/EEJitCoherence.h"

#include <gtest/gtest.h>

TEST(EEJitSourceHistory, RepeatsRequireMatchingSourceContextAndFingerprint)
{
	EEJitSourceHistory<16> history;
	const EEJitSourceFingerprint key{0x403000, 0x12000, 8, 1, 0x12345678};
	EXPECT_FALSE(history.Observe(key));
	EXPECT_TRUE(history.Observe(key));
	for (u32 field = 0; field < 5; ++field)
	{
		auto different = key;
		if (field == 0) different.virtualPC += 0x1000;
		if (field == 1) different.physicalPC += 0x1000;
		if (field == 2) ++different.words;
		if (field == 3) different.mode = 0;
		if (field == 4) ++different.content;
		EXPECT_FALSE(history.Observe(different));
	}
}

TEST(EEJitSourceHistory, CollisionAndResetCannotInventRepeat)
{
	EEJitSourceHistory<1> history;
	const EEJitSourceFingerprint first{0x403000, 0x12000, 8, 1, 1};
	const EEJitSourceFingerprint second{0x403000, 0x12000, 8, 1, 2};
	EXPECT_FALSE(history.Observe(first));
	EXPECT_TRUE(history.Observe(first));
	EXPECT_FALSE(history.Observe(second));
	EXPECT_FALSE(history.Observe(first));
	history.Reset();
	EXPECT_FALSE(history.Observe(first));
}
