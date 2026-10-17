// SPDX-FileCopyrightText: 2026 Masamune3210 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CDVD/MagicGateCrypto.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Emulated memory card and MechaCon are distinct endpoints of the SECRMAN
// challenge-response protocol. All key material is supplied externally.
// Crypto derivation follows the published ps3mca-tool MechaCon implementation.
namespace MagicGateCardAuth {
using MagicGateCrypto::B8;
using MagicGateCrypto::B16;
using MagicGateCrypto::Keys;

inline B8 Xor(const B8& a, const B8& b)
{
    B8 out{};
    for (size_t i=0;i<8;i++) out[i]=a[i]^b[i];
    return out;
}
inline B8 Crypt(const B8& input, const B16& key, const B8& iv, bool decrypt=false)
{
    B8 out{};
    const auto bytes=MagicGateCrypto::Detail::cbc(input.data(), 8, key.data(), true, decrypt, iv);
    if (bytes.size()==8) std::copy(bytes.begin(),bytes.end(),out.begin());
    return out;
}
inline B16 DeriveUnique(const Keys& keys,const B8& cardIV,const B8& cardMaterial)
{
    const B8 input=Xor(cardIV,cardMaterial);
    const B8 lo=Crypt(input,keys.cardHash1,keys.cardIV1);
    const B8 hi=Crypt(input,keys.cardHash2,keys.cardIV2);
    B16 out{};
    std::copy(lo.begin(),lo.end(),out.begin());
    std::copy(hi.begin(),hi.end(),out.begin()+8);
    return out;
}
inline bool Equal(const B8& x,const B8& y)
{
    return MagicGateCrypto::Detail::eq(x.data(),y.data(),8);
}

struct Card {
    int keyMode=-1;
    unsigned next=0;
    B8 iv{},material{},nonce{},mechaNonce{},challenge1{},challenge2{},challenge3{};
    B8 response1{},response2{},response3{},sessionKey{};
    B16 unique{};
    bool started=false,challengeValid=false,authenticated=false;
};
enum class Phase : unsigned {
    Idle, Reset, Started, HaveKey, HaveNonce, FirstBusy, FirstDone,
    FirstRead, ChallengesRead, HaveResponses, SecondBusy, SecondDone, Complete
};
struct Mecha {
    Phase phase=Phase::Idle;
    std::uint8_t cnum=0;
    B8 iv{},material{},nonce{},mechaNonce{},challenge1{},challenge2{},challenge3{};
    B8 response1{},response2{},response3{},sessionKey{};
    B16 unique{};
};
struct Runtime {
    Keys keys{};
    bool ready=false;
    Card cards[2][4]{};
    Mecha mecha{};
    int activePort=-1,activeSlot=-1;
    std::uint64_t sequence=0;

    void Reset()
    {
        keys=Keys{};
        ready=false;
        for (auto& row:cards) for (auto& card:row) card=Card{};
        mecha=Mecha{};
        activePort=activeSlot=-1;
        sequence=0;
    }
    Card* ActiveCard()
    {
        if(activePort<0 || activePort>=2 || activeSlot<0 || activeSlot>=4) return nullptr;
        return &cards[activePort][activeSlot];
    }
    bool StartMecha(const Keys& supplied,std::uint8_t cnum)
    {
        keys=supplied;
        ready=keys.hasCardKeys;
        mecha=Mecha{};
        mecha.cnum=cnum;
        mecha.phase=ready?Phase::Reset:Phase::Idle;
        activePort=activeSlot=-1;
        return ready;
    }
    void ResetCard(unsigned port,unsigned slot)
    {
        if(port>=2 || slot>=4) return;
        cards[port][slot]=Card{};
        if(activePort==static_cast<int>(port) && activeSlot==static_cast<int>(slot))
        {
            activePort=activeSlot=-1;
            mecha.phase=Phase::Idle;
        }
    }
    bool SelectCardKey(unsigned port,unsigned slot,std::uint8_t mode)
    {
        if(port>=2 || slot>=4 || !ready || mode!=1) return false;
        Card& card=cards[port][slot];
        card=Card{};
        card.keyMode=mode;
        return true;
    }
    bool CardStep(unsigned port,unsigned slot,std::uint8_t step,const B8* input,B8* output)
    {
        if(port>=2 || slot>=4 || !ready) return false;
        Card& card=cards[port][slot];
        if(card.keyMode!=1) return false;
        if(step==0)
        {
            if(mecha.phase!=Phase::Reset) return false;
            card=Card{};
            card.keyMode=1;
            activePort=static_cast<int>(port);
            activeSlot=static_cast<int>(slot);
            B8 seed{{0x50,0x43,0x53,0x58,0x32,0x4d,static_cast<std::uint8_t>(port),static_cast<std::uint8_t>(slot)}};
            card.iv=Crypt(seed,keys.cardHash1,keys.cardIV1);
            seed[0]^=0xa5; seed[1]^=0x5a;
            card.material=Crypt(seed,keys.cardHash2,keys.cardIV2);
            card.unique=DeriveUnique(keys,card.iv,card.material);
            B8 nonceSeed=card.iv;
            for(unsigned i=0;i<8;i++)nonceSeed[i]^=static_cast<std::uint8_t>((sequence>>(8*i))&0xff);
            ++sequence;
            card.nonce=Crypt(nonceSeed,card.unique,keys.challengeIV);
            card.started=true;
            card.next=1;
            mecha.phase=Phase::Started;
            return true;
        }
        if(!card.started || activePort!=static_cast<int>(port) ||
           activeSlot!=static_cast<int>(slot) || card.next!=step || step>0x14)
            return false;
        switch(step)
        {
            case 0x01: if(!output) return false; *output=card.iv; break;
            case 0x02: if(!output) return false; *output=card.material; break;
            case 0x04: if(!output) return false; *output=card.nonce; break;
            case 0x06: if(!input) return false; card.challenge3=*input; break;
            case 0x07: if(!input) return false; card.challenge2=*input; break;
            case 0x0b:
            {
                if(!input) return false;
                card.challenge1=*input;
                card.mechaNonce=Crypt(card.challenge1,card.unique,keys.challengeIV,true);
                card.challengeValid=
                    Equal(Crypt(card.challenge2,card.unique,card.challenge1,true),card.nonce) &&
                    Equal(Crypt(card.challenge3,card.unique,card.challenge2,true),card.iv);
                if(!card.challengeValid) return false;
                card.response1=Crypt(card.nonce,card.unique,keys.challengeIV);
                card.response2=Crypt(card.mechaNonce,card.unique,card.response1);
                // Emulated-card session material changes per transaction.
                B8 sk=Xor(card.iv,card.nonce);
                card.sessionKey=Crypt(sk,card.unique,card.response2);
                card.response3=Crypt(card.sessionKey,card.unique,card.response2);
                break;
            }
            case 0x0f: if(!card.challengeValid || !output) return false; *output=card.response1; break;
            case 0x11: if(!card.challengeValid || !output) return false; *output=card.response2; break;
            case 0x13: if(!card.challengeValid || !output) return false; *output=card.response3; break;
            case 0x14:
                if(!card.challengeValid || mecha.phase!=Phase::SecondDone) return false;
                card.authenticated=true;
                break;
            default: break;
        }
        ++card.next;
        return true;
    }
    bool Mecha81(std::uint8_t cnum)
    {
        if(!ready || mecha.phase!=Phase::Started || mecha.cnum!=cnum) return false;
        return true;
    }
    bool Mecha82(const std::uint8_t* bytes,std::size_t n)
    {
        Card* card=ActiveCard();
        if(!ready || mecha.phase!=Phase::Started || !card || card->next<3 ||
           n!=16 || !bytes || !MagicGateCrypto::Detail::eq(bytes,card->iv.data(),8) ||
           !MagicGateCrypto::Detail::eq(bytes+8,card->material.data(),8))
            return false;
        std::copy_n(bytes,8,mecha.iv.begin());
        std::copy_n(bytes+8,8,mecha.material.begin());
        mecha.unique=DeriveUnique(keys,mecha.iv,mecha.material);
        mecha.phase=Phase::HaveKey;
        return true;
    }
    bool Mecha83(const std::uint8_t* bytes,std::size_t n)
    {
        Card* card=ActiveCard();
        if(!ready || mecha.phase!=Phase::HaveKey || !card || card->next<5 ||
           n!=8 || !bytes || !MagicGateCrypto::Detail::eq(bytes,card->nonce.data(),8))
            return false;
        std::copy_n(bytes,8,mecha.nonce.begin());
        B8 seed=Xor(mecha.nonce,mecha.iv);
        mecha.mechaNonce=Crypt(seed,mecha.unique,keys.challengeIV);
        mecha.phase=Phase::HaveNonce;
        return true;
    }
    // SCmd 8F is a polling operation: 1 = calculating, 0 = verified, 0x84 = auth failure.
    std::uint8_t Poll()
    {
        Card* card=ActiveCard();
        if(!ready || !card) return 0x80;
        if(mecha.phase==Phase::HaveNonce && card->next==6)
        {
            mecha.phase=Phase::FirstBusy;
            return 1;
        }
        if(mecha.phase==Phase::FirstBusy)
        {
            mecha.challenge1=Crypt(mecha.mechaNonce,mecha.unique,keys.challengeIV);
            mecha.challenge2=Crypt(mecha.nonce,mecha.unique,mecha.challenge1);
            mecha.challenge3=Crypt(mecha.iv,mecha.unique,mecha.challenge2);
            mecha.phase=Phase::FirstDone;
            return 0;
        }
        if(mecha.phase==Phase::HaveResponses && card->next==0x14)
        {
            mecha.phase=Phase::SecondBusy;
            return 1;
        }
        if(mecha.phase==Phase::SecondBusy)
        {
            const B8 a=Crypt(mecha.response1,mecha.unique,keys.challengeIV,true);
            const B8 b=Crypt(mecha.response2,mecha.unique,mecha.response1,true);
            if(!card->challengeValid || !Equal(a,mecha.nonce) || !Equal(b,mecha.mechaNonce))
            {
                mecha.phase=Phase::Idle;
                return 0x84;
            }
            mecha.sessionKey=Crypt(mecha.response3,mecha.unique,mecha.response2,true);
            mecha.phase=Phase::SecondDone;
            return 0;
        }
        if(mecha.phase==Phase::FirstDone || mecha.phase==Phase::SecondDone) return 0;
        return 0x80;
    }
    bool Read84(std::uint8_t* out)
    {
        if(mecha.phase!=Phase::FirstDone) return false;
        std::copy(mecha.challenge1.begin(),mecha.challenge1.end(),out);
        std::copy_n(mecha.challenge2.begin(),4,out+8);
        mecha.phase=Phase::FirstRead;
        return true;
    }
    bool Read85(std::uint8_t* out)
    {
        if(mecha.phase!=Phase::FirstRead) return false;
        std::copy_n(mecha.challenge2.begin()+4,4,out);
        std::copy(mecha.challenge3.begin(),mecha.challenge3.end(),out+4);
        mecha.phase=Phase::ChallengesRead;
        return true;
    }
    bool Write86(const std::uint8_t* data,std::size_t length)
    {
        Card* card=ActiveCard();
        if(mecha.phase!=Phase::ChallengesRead || !card || !card->challengeValid ||
           card->next<0x12 || length!=16 || !data) return false;
        std::copy_n(data,8,mecha.response1.begin());
        std::copy_n(data+8,8,mecha.response2.begin());
        return true;
    }
    bool Write87(const std::uint8_t* data,std::size_t length)
    {
        Card* card=ActiveCard();
        if(mecha.phase!=Phase::ChallengesRead || !card || card->next<0x14 ||
           length!=8 || !data) return false;
        std::copy_n(data,8,mecha.response3.begin());
        mecha.phase=Phase::HaveResponses;
        return true;
    }
    bool Complete88()
    {
        Card* card=ActiveCard();
        if(mecha.phase!=Phase::SecondDone || !card || !card->authenticated) return false;
        mecha.phase=Phase::Complete;
        return true;
    }
};
inline Runtime g_runtime;
} // namespace MagicGateCardAuth
