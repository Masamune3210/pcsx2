// SPDX-License-Identifier: GPL-3.0-or-later
// Public synthetic DES/3DES tests, no MagicGate keys or original KELF.
#include "CDVD/MagicGateCrypto.h"
#include <cstdio>
#include <string>
using namespace MagicGateCrypto;
using namespace MagicGateCrypto::Detail;
static int failures=0;
static void check(const char* name, std::uint64_t actual, std::uint64_t expected) {
    if(actual!=expected){std::fprintf(stderr,"%s FAILED\n",name);++failures;}
}
int main() {
    std::uint8_t key[]={0x13,0x34,0x57,0x79,0x9b,0xbc,0xdf,0xf1};
    const auto des=desBlock(0x0123456789abcdefULL,key,false);
    check("FIPS DES",des,0x85e813540f0ab405ULL);
    check("DES inverse",desBlock(des,key,true),0x0123456789abcdefULL);
    B16 k16={0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
             0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
    const auto ede=ede2(0x0001020304050607ULL,k16,false);
    check("EDE2",ede,0x52c5c0705d9089e1ULL);
    check("EDE2 inverse",ede2(ede,k16,true),0x0001020304050607ULL);
    const std::uint8_t plain[16]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    const B8 iv={0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80};
    const std::uint8_t reference[16]={
        0xfa,0x5a,0xf5,0x65,0xce,0x0b,0x09,0x82,
        0xad,0xb7,0x74,0xc1,0x27,0xf4,0xa6,0xc3
    };
    const auto encrypted=cbc(plain,16,k16.data(),true,false,iv);
    if(encrypted.size()!=16||!eq(encrypted.data(),reference,16)) {
        std::fputs("EDE2 CBC FAILED\n",stderr);++failures;
    } else {
        const auto decrypted=cbc(encrypted.data(),16,k16.data(),true,true,iv);
        if(decrypted.size()!=16||!eq(decrypted.data(),plain,16)) {
            std::fputs("EDE2 CBC inverse FAILED\n",stderr);++failures;
        }
    }
    Keys parsed{};std::string why;
    if(ParseKeys("MG_SIG_MASTER_KEY=1234\n",parsed,why)){std::fputs("Accepted malformed keys\n",stderr);++failures;}
    std::uint8_t zeroHeader[416]={};
    HeaderResult out;
    if(VerifyHeader(zeroHeader,sizeof(zeroHeader),parsed,out)||out.valid||!out.bit.empty()) {
        std::fputs("Accepted zero KELF header\n",stderr);++failures;
    }
    if(!failures)std::puts("PASS synthetic DES/3DES CBC and negative KELF checks");
    return failures?1:0;
}
