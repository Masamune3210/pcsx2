// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "BaseblockEx.h"

BASEBLOCKEX* BaseBlocks::New(u32 startpc, uptr fnptr, Metrics* metrics)
{
	std::pair<linkiter_t, linkiter_t> range = links.equal_range(startpc);
	for (linkiter_t i = range.first; i != range.second; ++i)
	{
		const bool wrote = PatchLink(reinterpret_cast<s32*>(i->second), fnptr, metrics);
		if (linkObserver)
			linkObserver(LinkEvent::TargetCompiled, startpc, i->second, fnptr, wrote, linkObserverContext);
	}

	return blocks.insert(startpc, fnptr);
}

int BaseBlocks::LastIndex(u32 startpc) const
{
	if (0 == blocks.size())
		return -1;

	int imin = 0, imax = blocks.size() - 1;

	while (imin != imax)
	{
		const int imid = (imin + imax + 1) >> 1;

		if (blocks[imid].startpc > startpc)
			imax = imid - 1;
		else
			imin = imid;
	}

	return imin;
}

#if 0
BASEBLOCKEX* BaseBlocks::GetByX86(uptr ip)
{
	if (0 == blocks.size())
		return 0;

	int imin = 0, imax = blocks.size() - 1;

	while(imin != imax) {
		const int imid = (imin+imax+1)>>1;

		if (blocks[imid].fnptr > ip)
			imax = imid - 1;
		else
			imin = imid;
	}

	if (ip < blocks[imin].fnptr ||
		ip >= blocks[imin].fnptr + blocks[imin].x86size)
		return 0;

	return &blocks[imin];
}
#endif

void BaseBlocks::Link(u32 pc, s32* jumpptr, Metrics* metrics)
{
	BASEBLOCKEX* targetblock = Get(pc);
	const uptr destination = targetblock && targetblock->startpc == pc ?
		targetblock->fnptr : recompiler;
	const bool wrote = PatchLink(jumpptr, destination, metrics);
	if (linkObserver)
		linkObserver(LinkEvent::Register, pc, reinterpret_cast<uptr>(jumpptr),
			destination, wrote, linkObserverContext);
	links.insert(std::pair<u32, uptr>(pc, (uptr)jumpptr));
}
