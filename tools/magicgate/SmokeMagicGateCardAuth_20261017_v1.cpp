// SPDX-FileCopyrightText: 2026 Masamune3210 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone synthetic validation; no private card or firmware material.
#include "CDVD/MagicGateCardAuth.h"
#include <array>
#include <cstdio>
#include <cstdlib>

using namespace MagicGateCardAuth;
static void Check(bool ok, const char* what)
{
    if (!ok) { std::fprintf(stderr,"CARD_AUTH FAIL: %s\n",what); std::exit(1); }
}
static Keys SyntheticKeys()
{
    Keys k;
    k.hasCardKeys=true;
    for (unsigned i=0;i<16;i++)
    {
        k.cardHash1[i]=static_cast<std::uint8_t>(0x10+i*3);
        k.cardHash2[i]=static_cast<std::uint8_t>(0xa0+i*5);
    }
    for (unsigned i=0;i<8;i++)
    {
        k.cardIV1[i]=static_cast<std::uint8_t>(0x20+i);
        k.cardIV2[i]=static_cast<std::uint8_t>(0x40+i);
        k.challengeIV[i]=static_cast<std::uint8_t>(0x70+i);
    }
    return k;
}
static void RoundTrip(bool mutateResponse)
{
    Runtime r;
    const Keys k=SyntheticKeys();
    Check(r.StartMecha(k,3),"start MechaCon");
    Check(r.SelectCardKey(0,0,1),"select retail key");
    Check(r.CardStep(0,0,0,nullptr,nullptr),"card start");
    Check(r.Mecha81(3),"MechaCon start");
    B8 iv{},material{},nonce{},c1{},c2{},c3{},resp1{},resp2{},resp3{};
    Check(r.CardStep(0,0,1,nullptr,&iv),"read card IV");
    Check(r.CardStep(0,0,2,nullptr,&material),"read card material");
    std::array<std::uint8_t,16> pair{};
    std::copy(iv.begin(),iv.end(),pair.begin());
    std::copy(material.begin(),material.end(),pair.begin()+8);
    Check(r.Mecha82(pair.data(),pair.size()),"derive unique key");
    Check(r.CardStep(0,0,3,nullptr,nullptr),"card nonce start");
    Check(r.CardStep(0,0,4,nullptr,&nonce),"read card nonce");
    Check(r.Mecha83(nonce.data(),nonce.size()),"MechaCon nonce");
    Check(r.CardStep(0,0,5,nullptr,nullptr),"card phase1 marker");
    Check(r.Poll()==1,"first 8F busy");
    Check(r.Poll()==0,"first 8F verified");
    std::array<std::uint8_t,12> ch84{},ch85{};
    Check(r.Read84(ch84.data()),"84 challenge half");
    Check(r.Read85(ch85.data()),"85 challenge half");
    std::copy_n(ch84.begin(),8,c1.begin());
    std::copy_n(ch84.begin()+8,4,c2.begin());
    std::copy_n(ch85.begin(),4,c2.begin()+4);
    std::copy_n(ch85.begin()+4,8,c3.begin());
    Check(r.CardStep(0,0,6,&c3,nullptr),"card challenge3");
    Check(r.CardStep(0,0,7,&c2,nullptr),"card challenge2");
    for(unsigned n=8;n<=10;n++)Check(r.CardStep(0,0,n,nullptr,nullptr),"card challenge marker");
    B8 bad1=c1;bad1[0]^=1;
    Check(!r.CardStep(0,0,11,&bad1,nullptr),"reject invalid MechaCon challenge");
    Check(r.CardStep(0,0,11,&c1,nullptr),"verify MechaCon challenge");
    for(unsigned n=12;n<=14;n++)Check(r.CardStep(0,0,n,nullptr,nullptr),"card response marker");
    Check(r.CardStep(0,0,15,nullptr,&resp1),"card response1");
    Check(r.CardStep(0,0,16,nullptr,nullptr),"card response marker2");
    Check(r.CardStep(0,0,17,nullptr,&resp2),"card response2");
    std::copy(resp1.begin(),resp1.end(),pair.begin());
    std::copy(resp2.begin(),resp2.end(),pair.begin()+8);
    if(mutateResponse)pair[0]^=0x80;
    Check(r.Mecha86(pair.data(),pair.size()),"receive first two responses");
    Check(r.CardStep(0,0,18,nullptr,nullptr),"card response marker3");
    Check(r.CardStep(0,0,19,nullptr,&resp3),"card response3");
    Check(r.Mecha87(resp3.data(),resp3.size()),"receive third response");
    Check(r.Poll()==1,"second 8F busy");
    Check(r.Poll()==(mutateResponse?0x84:0),"second 8F authentication result");
    if(!mutateResponse)
    {
        Check(r.CardStep(0,0,20,nullptr,nullptr),"card finalize");
        Check(r.Complete88(),"complete verified mutual authentication");
        Check(!r.Complete88(),"reject double completion");
    }
    else
    {
        Check(!r.CardStep(0,0,20,nullptr,nullptr),"reject finalize after bad response");
        Check(!r.Complete88(),"reject completion after bad response");
    }
}
int main()
{
    Runtime r;
    Check(!r.StartMecha(Keys{},3),"reject missing optional retail keys");
    Check(r.Poll()==0x80,"reject card completion without keys");
    Check(r.StartMecha(SyntheticKeys(),3),"start with synthetic keys");
    Check(!r.SelectCardKey(0,0,0),"reject unsupported DEX keymode");
    Check(!r.CardStep(0,0,0,nullptr,nullptr),"reject unselected card keymode");
    RoundTrip(false);
    RoundTrip(true);
    std::puts("CARD_AUTH PASS: two-stage mutual authentication, corruption rejection and key-mode gating");
}
