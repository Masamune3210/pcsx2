// SPDX-FileCopyrightText: 2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "x86/BaseblockEx.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

// Diagnostics only. Source ownership is recorded at rel32 creation, so later
// target relinking can distinguish a live caller from a retired caller. The
// witness NEVER changes BaseBlocks linking, dispatch, eviction, or host code.
// Do not equate "retired" with "not executing": the host may still be returning
// to a retired caller when the metadata is already gone.
class EERecLinkOwnershipWitness
{
public:
    static constexpr size_t MaxSites = 400000;
    enum class OwnerState : u8 { Live, Retired, Compiling, Unknown, Count };
    struct Tally
    {
        u64 attempts = 0;
        u64 writes = 0;
    };
    struct Sample
    {
        uptr site = 0;
        uptr ownerHost = 0;
        uptr destination = 0;
        u32 ownerPA = 0;
        u32 ownerVA = 0;
        u32 targetPA = 0;
        u32 hostBytes = 0;
        u8 asid = 0;
        BaseBlocks::LinkEvent event = BaseBlocks::LinkEvent::TargetRemoved;
        bool wrote = false;
    };
private:
    struct Owner
    {
        u32 physicalPC = 0;
        u32 virtualPC = 0;
        u32 hostBytes = 0;
        u32 maxLinkEndOffset = 0;
        u8 asid = 0;
        bool finalized = false;
        bool retired = false;
        bool invalidOffset = false;
    };
    struct Site
    {
        uptr ownerHost = 0;
        u32 targetPA = 0;
        bool detached = false;
    };

    std::unordered_map<uptr, Owner> m_owners;
    std::unordered_map<uptr, Site> m_sites;
    std::array<Sample, 8> m_retiredSamples{};
    size_t m_sampleCount = 0;

public:
    u64 registrations = 0;
    u64 registrationWrites = 0;
    u64 droppedSites = 0;
    u64 unownedRegistrations = 0;
    u64 invalidOffsets = 0;
    u64 finalizedOwners = 0;
    u64 retiredOwners = 0;
    u64 retiredDuringEEExecution = 0;
    u64 sourceDetachAttempts = 0;
    u64 sourceDetachWrites = 0;
    u64 untrackedDetaches = 0;
    u64 postDetachRetargets = 0;
    u64 dispatcherLinksTranslation = 0;
    u64 dispatcherLinksCrossPage = 0;
    // [TargetCompiled, TargetRemoved][OwnerState]
    std::array<std::array<Tally, static_cast<size_t>(OwnerState::Count)>, 2> patches{};

    void Reset()
    {
        m_owners.clear();
        m_sites.clear();
        m_sampleCount = 0;
        registrations = registrationWrites = droppedSites = unownedRegistrations = 0;
        invalidOffsets = finalizedOwners = retiredOwners = retiredDuringEEExecution = 0;
        sourceDetachAttempts = sourceDetachWrites = untrackedDetaches = postDetachRetargets = 0;
        dispatcherLinksTranslation = dispatcherLinksCrossPage = 0;
        patches = {};
    }

    void Register(u32 targetPA, uptr site, uptr ownerHost, u32 ownerPA,
        u32 ownerVA, u8 asid, bool wrote)
    {
        ++registrations;
        registrationWrites += wrote ? 1 : 0;
        if (!ownerHost || site < ownerHost)
        {
            ++unownedRegistrations;
            return;
        }
        if (m_sites.size() >= MaxSites)
        {
            ++droppedSites;
            return;
        }
        auto [ownerIt, inserted] = m_owners.try_emplace(ownerHost);
        Owner& owner = ownerIt->second;
        if (inserted)
        {
            owner.physicalPC = ownerPA;
            owner.virtualPC = ownerVA;
            owner.asid = asid;
        }
        const uptr offset = site - ownerHost;
        if (offset > UINT32_MAX - sizeof(s32))
        {
            ++unownedRegistrations;
            return;
        }
        owner.maxLinkEndOffset = std::max(owner.maxLinkEndOffset,
            static_cast<u32>(offset + sizeof(s32)));
        m_sites.insert_or_assign(site, Site{ownerHost, targetPA});
    }

    void Finalize(uptr hostStart, u32 physicalPC, u32 hostBytes)
    {
        const auto it = m_owners.find(hostStart);
        if (it == m_owners.end() || it->second.physicalPC != physicalPC)
            return;
        Owner& owner = it->second;
        if (!owner.finalized)
            ++finalizedOwners;
        owner.finalized = true;
        owner.hostBytes = hostBytes;
        if (owner.maxLinkEndOffset > hostBytes && !owner.invalidOffset)
        {
            owner.invalidOffset = true;
            ++invalidOffsets;
        }
    }

    // Called BEFORE BaseBlocks::Remove() redirects incoming links.
    void Retire(const BASEBLOCKEX& block, bool eeExecuting)
    {
        const auto it = m_owners.find(block.fnptr);
        if (it == m_owners.end() || it->second.physicalPC != block.startpc ||
            it->second.retired)
            return;
        it->second.retired = true;
        ++retiredOwners;
        retiredDuringEEExecution += eeExecuting ? 1 : 0;
    }

    void Detach(uptr site, bool wrote)
    {
        ++sourceDetachAttempts;
        sourceDetachWrites += wrote ? 1 : 0;
        auto it = m_sites.find(site);
        if (it == m_sites.end() || it->second.detached)
        {
            ++untrackedDetaches;
            return;
        }
        it->second.detached = true;
    }

    void Retarget(BaseBlocks::LinkEvent event, u32 targetPA, uptr site,
        uptr destination, bool wrote)
    {
        const size_t eventIndex = event == BaseBlocks::LinkEvent::TargetCompiled ? 0 : 1;
        OwnerState state = OwnerState::Unknown;
        const Owner* owner = nullptr;
        uptr ownerHost = 0;
        const auto s = m_sites.find(site);
        if (s != m_sites.end())
        {
            postDetachRetargets += s->second.detached ? 1 : 0;
            ownerHost = s->second.ownerHost;
            const auto o = m_owners.find(ownerHost);
            if (o != m_owners.end())
            {
                owner = &o->second;
                state = owner->retired ? OwnerState::Retired :
                    (owner->finalized ? OwnerState::Live : OwnerState::Compiling);
            }
        }
        Tally& tally = patches[eventIndex][static_cast<size_t>(state)];
        ++tally.attempts;
        tally.writes += wrote ? 1 : 0;
        if (state == OwnerState::Retired && m_sampleCount < m_retiredSamples.size())
        {
            Sample& sample = m_retiredSamples[m_sampleCount++];
            sample.site = site;
            sample.ownerHost = ownerHost;
            sample.destination = destination;
            sample.ownerPA = owner->physicalPC;
            sample.ownerVA = owner->virtualPC;
            sample.targetPA = targetPA;
            sample.hostBytes = owner->hostBytes;
            sample.asid = owner->asid;
            sample.event = event;
            sample.wrote = wrote;
        }
    }

    size_t TrackedSites() const { return m_sites.size(); }
    size_t TrackedOwners() const { return m_owners.size(); }
    size_t SampleCount() const { return m_sampleCount; }
    const Sample& GetSample(size_t i) const { return m_retiredSamples[i]; }
};
