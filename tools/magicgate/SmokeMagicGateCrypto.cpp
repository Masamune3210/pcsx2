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

    // Header-only .mgkeys files remain valid; content IV is optional but validated.
    const std::pair<const char*,int> fields[] = {
        {"MG_SIG_MASTER_KEY",8}, {"MG_SIG_HASH_KEY",8},
        {"MG_KBIT_MASTER_KEY",16}, {"MG_KBIT_IV",8},
        {"MG_KC_MASTER_KEY",16}, {"MG_KC_IV",8},
        {"MG_ROOTSIG_MASTER_KEY",8}, {"MG_ROOTSIG_HASH_KEY",16},
        {"MG_CONTENT_TABLE_IV",8}
    };
    std::string config;
    for(const auto& field:fields)
        config+=std::string(field.first)+"="+std::string(field.second*2,'0')+"\\n";
    if(!ParseKeys(config,parsed,why) || parsed.hasContentIV) {
        std::fputs("Header-only key parsing FAILED\n",stderr);++failures;
    }
    const std::string withContentIV=config+"MG_CONTENT_IV=0011223344556677\\n";
    if(!ParseKeys(withContentIV,parsed,why) || !parsed.hasContentIV ||
       parsed.contentIV[0]!=0 || parsed.contentIV[7]!=0x77) {
        std::fputs("Optional content IV parsing FAILED\n",stderr);++failures;
    }
    if(ParseKeys(withContentIV+"MG_CONTENT_IV=0011223344556677\\n",parsed,why) ||
       ParseKeys(config+"MG_CONTENT_IV=00\\n",parsed,why)) {
        std::fputs("Invalid optional IV accepted\n",stderr);++failures;
    }

    // A synthetic two-descriptor BIT: authenticated EDE2-CBC content,
    // followed by an unprotected range that may cross transfer boundaries.
    Keys contentKeys{};
    std::copy(key,key+8,contentKeys.sigMaster.begin());
    std::copy(k16.begin(),k16.begin()+8,contentKeys.sigHash.begin());
    contentKeys.contentIV=iv;
    contentKeys.hasContentIV=true;
    HeaderResult header{};
    header.valid=true;
    header.blockCount=2;
    header.contentKeyCount=2;
    header.contentSize=24;
    header.bit.resize(8+2*16);
    header.bit[4]=2;
    header.bit[8]=16; // first block: 16 bytes, encrypted and signed
    header.bit[12]=3;
    header.bit[24]=8; // second block: 8 raw unsigned bytes
    const std::uint8_t firstPlain[16]={0x7f,'E','L','F',4,5,6,7,8,9,10,11,12,13,14,15};
    auto firstCipher=cbc(firstPlain,16,k16.data(),true,false,iv);
    header.kc=k16;
    B8 signature{};
    for(int j=0;j<16;j+=8)
        for(int k=0;k<8;++k)signature[k]^=firstPlain[j+k];
    B16 macKeys{};
    std::copy(contentKeys.sigMaster.begin(),contentKeys.sigMaster.end(),macKeys.begin());
    std::copy(contentKeys.sigHash.begin(),contentKeys.sigHash.end(),macKeys.begin()+8);
    toBe(ede2(fromBe(signature.data()),macKeys,false),header.bit.data()+16);
    ContentResult transfer;
    if(!DecryptContentTransfer(firstCipher.data(),firstCipher.size(),contentKeys,header,{},transfer) ||
       transfer.plaintext.size()!=16 || !eq(transfer.plaintext.data(),firstPlain,16) ||
       transfer.next.blockIndex!=1 || transfer.next.blockOffset!=0) {
        std::fputs("Protected content transfer FAILED\n",stderr);++failures;
    }
    auto altered=firstCipher;
    altered[4]^=0x80;
    if(DecryptContentTransfer(altered.data(),altered.size(),contentKeys,header,{},transfer) ||
       transfer.valid || !transfer.plaintext.empty()) {
        std::fputs("Tampered content accepted\n",stderr);++failures;
    }
    if(DecryptContentTransfer(firstCipher.data(),8,contentKeys,header,{},transfer) ||
       transfer.valid || !transfer.plaintext.empty()) {
        std::fputs("Partial protected descriptor accepted\n",stderr);++failures;
    }
    contentKeys.hasContentIV=false;
    if(DecryptContentTransfer(firstCipher.data(),16,contentKeys,header,{},transfer)) {
        std::fputs("Missing content IV accepted\n",stderr);++failures;
    }
    contentKeys.hasContentIV=true;
    const std::uint8_t raw[8]={1,2,3,4,5,6,7,8};
    ContentCursor cursor{1,0};
    if(!DecryptContentTransfer(raw,3,contentKeys,header,cursor,transfer) ||
       transfer.next.blockIndex!=1 || transfer.next.blockOffset!=3 ||
       transfer.plaintext.size()!=3 || !eq(transfer.plaintext.data(),raw,3)) {
        std::fputs("Unsigned partial content FAILED\n",stderr);++failures;
    }
    cursor={1,3};
    if(!DecryptContentTransfer(raw+3,5,contentKeys,header,cursor,transfer) ||
       transfer.next.blockIndex!=2 || transfer.next.blockOffset!=0 ||
       transfer.plaintext.size()!=5 || !eq(transfer.plaintext.data(),raw+3,5)) {
        std::fputs("Unsigned descriptor completion FAILED\n",stderr);++failures;
    }
    if(DecryptContentTransfer(raw,8,contentKeys,header,{2,0},transfer)) {
        std::fputs("Past-BIT data accepted\n",stderr);++failures;
    }
    // Signed plaintext descriptor follows the DES CBC -> D(hash) -> E(master) MAC.
    header.bit[12]=2;
    B8 zeroIV{};
    const auto signedChain=cbc(firstPlain,16,contentKeys.sigMaster.data(),false,false,zeroIV);
    std::uint64_t signedMac=fromBe(signedChain.data()+8);
    signedMac=desBlock(signedMac,contentKeys.sigHash.data(),true);
    signedMac=desBlock(signedMac,contentKeys.sigMaster.data(),false);
    toBe(signedMac,header.bit.data()+16);
    if(!DecryptContentTransfer(firstPlain,16,contentKeys,header,{},transfer) ||
       transfer.next.blockIndex!=1 ||
       !eq(transfer.plaintext.data(),firstPlain,16)) {
        std::fputs("Signed plaintext content FAILED\n",stderr);++failures;
    }
    header.bit[16]^=1;
    if(DecryptContentTransfer(firstPlain,16,contentKeys,header,{},transfer)) {
        std::fputs("Incorrect signed-plaintext MAC accepted\n",stderr);++failures;
    }
    if(!failures)std::puts("PASS synthetic DES/3DES CBC, content descriptors, MACs and negative KELF checks");
    return failures?1:0;
}
