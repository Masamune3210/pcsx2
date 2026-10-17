// SPDX-FileCopyrightText: 2026 Masamune3210 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Clean-room implementation of standard FIPS DES/3DES and PS2 KELF header authentication.
// No proprietary keys or binary fixtures. Key values must be supplied by the user.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace MagicGateCrypto {

using B8 = std::array<std::uint8_t, 8>;
using B16 = std::array<std::uint8_t, 16>;

struct Keys {
    B8 sigMaster{}, sigHash{}, kbitIV{}, kcIV{}, rootMaster{}, tableIV{}, contentIV{};
    bool hasContentIV = false;
    B16 kbitMaster{}, kcMaster{}, rootHash{};
};

struct HeaderResult {
    std::vector<std::uint8_t> bit;
    B16 kbit{}, kc{};
    std::uint32_t contentOffset = 0, contentSize = 0;
    std::uint8_t blockCount = 0, contentKeyCount = 0;
    std::string error;
    bool valid = false;
};

namespace Detail {
constexpr std::uint8_t IP[64] = {
    58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,
    62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,
    61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7
};
constexpr std::uint8_t FP[64] = {
    40,8,48,16,56,24,64,32,39,7,47,15,55,23,63,31,
    38,6,46,14,54,22,62,30,37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28,35,3,43,11,51,19,59,27,
    34,2,42,10,50,18,58,26,33,1,41,9,49,17,57,25
};
constexpr std::uint8_t PC1[56] = {
    57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,
    59,51,43,35,27,19,11,3,60,52,44,36,63,55,47,39,
    31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,
    29,21,13,5,28,20,12,4
};
constexpr std::uint8_t PC2[48] = {
    14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,
    26,8,16,7,27,20,13,2,41,52,31,37,47,55,30,40,
    51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32
};
constexpr std::uint8_t SHIFTS[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
constexpr std::uint8_t E[48] = {
    32,1,2,3,4,5,4,5,6,7,8,9,8,9,10,11,12,13,
    12,13,14,15,16,17,16,17,18,19,20,21,20,21,22,23,
    24,25,24,25,26,27,28,29,28,29,30,31,32,1
};
constexpr std::uint8_t P[32] = {
    16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,
    2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25
};
constexpr std::uint8_t S[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,
     0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
     4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,
     15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,
     3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
     0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,
     13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,
     13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
     13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,
     1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,
     13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
     10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,
     3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,
     14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
     4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,
     11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,
     10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
     9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,
     4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,
     13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
     1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,
     6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,
     1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
     7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,
     2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};
template<std::size_t N> inline std::uint64_t permute(std::uint64_t v, int bits,
                                                       const std::uint8_t (&p)[N]) {
    std::uint64_t r = 0;
    for (std::uint8_t n : p) r = (r << 1) | ((v >> (bits - n)) & 1);
    return r;
}
inline std::uint64_t fromBe(const std::uint8_t* b) {
    std::uint64_t out = 0;
    for (int i=0;i<8;++i) out=(out<<8)|b[i];
    return out;
}
inline void toBe(std::uint64_t v, std::uint8_t* b) {
    for (int i=7;i>=0;--i) { b[i]=static_cast<std::uint8_t>(v); v>>=8; }
}
inline std::array<std::uint64_t,16> schedule(const std::uint8_t* key) {
    std::array<std::uint64_t,16> rk{};
    const std::uint64_t pc1=permute(fromBe(key),64,PC1);
    std::uint32_t c=(pc1>>28)&0x0fffffff,d=pc1&0x0fffffff;
    for(int i=0;i<16;++i) {
        int n=SHIFTS[i];
        c=((c<<n)|(c>>(28-n)))&0x0fffffff;
        d=((d<<n)|(d>>(28-n)))&0x0fffffff;
        rk[i]=permute((static_cast<std::uint64_t>(c)<<28)|d,56,PC2);
    }
    return rk;
}
inline std::uint64_t desBlock(std::uint64_t input,const std::uint8_t* key,bool decrypt) {
    const auto rk=schedule(key);
    const std::uint64_t ip=permute(input,64,IP);
    std::uint32_t l=ip>>32,r=static_cast<std::uint32_t>(ip);
    for(int i=0;i<16;++i) {
        const std::uint64_t x=permute(static_cast<std::uint64_t>(r),32,E)^rk[decrypt?15-i:i];
        std::uint32_t sb=0;
        for(int j=0;j<8;++j) {
            const unsigned six=static_cast<unsigned>((x>>(42-6*j))&63);
            const unsigned row=((six&32)>>4)|(six&1);
            const unsigned col=(six>>1)&15;
            sb=(sb<<4)|S[j][row*16+col];
        }
        const std::uint32_t f=static_cast<std::uint32_t>(permute(sb,32,P));
        const std::uint32_t next=l^f;
        l=r;r=next;
    }
    return permute((static_cast<std::uint64_t>(r)<<32)|l,64,FP);
}
inline std::uint64_t ede2(std::uint64_t input,const B16& keys,bool decrypt) {
    if(!decrypt) return desBlock(desBlock(desBlock(input,keys.data(),false),keys.data()+8,true),keys.data(),false);
    return desBlock(desBlock(desBlock(input,keys.data(),true),keys.data()+8,false),keys.data(),true);
}
inline std::vector<std::uint8_t> cbc(const std::uint8_t* input,std::size_t n,
                                     const std::uint8_t* key,bool trip,bool decrypt,
                                     const B8& iv) {
    std::vector<std::uint8_t> result;
    if(n==0 || n%8!=0) return result;
    result.resize(n);
    std::uint64_t prev=fromBe(iv.data());
    B16 key2{};
    if(trip) std::memcpy(key2.data(),key,16);
    for(std::size_t i=0;i<n;i+=8) {
        const std::uint64_t v=fromBe(input+i);
        const std::uint64_t out=decrypt ?
            ((trip?ede2(v,key2,true):desBlock(v,key,true))^prev) :
            (trip?ede2(v^prev,key2,false):desBlock(v^prev,key,false));
        toBe(out,result.data()+i);
        prev=decrypt?v:out;
    }
    return result;
}
inline std::uint16_t u16(const std::uint8_t* p) {return p[0]|(static_cast<unsigned>(p[1])<<8);}
inline std::uint32_t u32(const std::uint8_t* p) {return u16(p)|(static_cast<std::uint32_t>(u16(p+2))<<16);}
inline bool eq(const std::uint8_t* a,const std::uint8_t* b,std::size_t n) {
    unsigned diff=0;
    for(std::size_t i=0;i<n;++i) diff|=a[i]^b[i];
    return diff==0;
}
inline bool fail(HeaderResult& out, const char* what) {
    out=HeaderResult{};
    out.error=what;
    return false;
}
} // Detail

// Plain KEY=HEX file. No key values are stored in PCSX2 or written to logs.
inline bool ParseKeys(const std::string& file,Keys& output,std::string& error,
    std::vector<std::string>* ignoredFields=nullptr) {
    struct Spec {const char* name; std::uint8_t* buffer;std::size_t len;};
    Keys tmp{};
    Spec specs[]={
        {"MG_SIG_MASTER_KEY",tmp.sigMaster.data(),8},
        {"MG_SIG_HASH_KEY",tmp.sigHash.data(),8},
        {"MG_KBIT_MASTER_KEY",tmp.kbitMaster.data(),16},
        {"MG_KBIT_IV",tmp.kbitIV.data(),8},
        {"MG_KC_MASTER_KEY",tmp.kcMaster.data(),16},
        {"MG_KC_IV",tmp.kcIV.data(),8},
        {"MG_ROOTSIG_MASTER_KEY",tmp.rootMaster.data(),8},
        {"MG_ROOTSIG_HASH_KEY",tmp.rootHash.data(),16},
        {"MG_CONTENT_TABLE_IV",tmp.tableIV.data(),8},
        {"MG_CONTENT_IV",tmp.contentIV.data(),8}
    };
    std::uint32_t seen=0;
    std::istringstream lines(file);
    std::string line;
    while (std::getline(lines,line)) {
        const std::size_t start=line.find_first_not_of(" \t\r");
        if(start==std::string::npos || line[start]=='#' || line[start]==';')continue;
        const std::size_t eq=line.find('=',start);
        if(eq==std::string::npos) {error="Malformed key file";return false;}
        const std::size_t last=line.find_last_not_of(" \t\r",eq-1);
        const std::string name=line.substr(start,last-start+1);
        const auto found=std::find_if(std::begin(specs),std::end(specs),
            [&](const Spec& s){return name==s.name;});
        if(found==std::end(specs)) {
            if(ignoredFields) ignoredFields->push_back(name);
            continue;
        }
        const unsigned index=static_cast<unsigned>(found-std::begin(specs));
        if(seen&(1u<<index)){error="Duplicate key field: "+name;return false;}
        const std::size_t valueStart=line.find_first_not_of(" \t\r",eq+1);
        if(valueStart==std::string::npos){error="Empty key field: "+name;return false;}
        const std::size_t valueEnd=line.find_last_not_of(" \t\r");
        if(valueEnd-valueStart+1!=2*found->len){error="Wrong key length: "+name;return false;}
        auto hex=[](char c)->int {
            if(c>='0'&&c<='9')return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            if(c>='A'&&c<='F')return c-'A'+10;
            return -1;
        };
        for(std::size_t i=0;i<found->len;++i) {
            int a=hex(line[valueStart+2*i]),b=hex(line[valueStart+2*i+1]);
            if(a<0||b<0){error="Invalid hex in field: "+name;return false;}
            found->buffer[i]=static_cast<std::uint8_t>((a<<4)|b);
        }
        seen|=1u<<index;
    }
    constexpr std::uint32_t requiredMask=(1u<<9)-1u;
    if((seen&requiredMask)!=requiredMask){error="Missing required MagicGate keys";return false;}
    tmp.hasContentIV=(seen&(1u<<9))!=0;
    output=tmp;
    error.clear();
    return true;
}

inline bool VerifyHeader(const std::uint8_t* bytes,std::size_t len,const Keys& keys,HeaderResult& out) {
    using namespace Detail;
    out=HeaderResult{};
    if(bytes==nullptr || len<0x60 || len>0x800 || u16(bytes+0x14)!=len)
        return fail(out,"Invalid header size");
    const unsigned flags=u16(bytes+0x18),bitCount=u16(bytes+0x1a);
    const std::size_t contentSize=u32(bytes+0x10);
    // Prefix records come before the encrypted keys. Reject before reading variable records.
    if(bitCount>(len-0x20)/16)return fail(out,"Invalid encrypted prefix count");
    std::size_t keyOffset=0x20+bitCount*16;
    if(flags&1) {
        if(keyOffset>=len)return fail(out,"Invalid header extension");
        keyOffset+=static_cast<std::size_t>(bytes[keyOffset])+1;
    }
    if((flags&0xF000)==0)keyOffset+=8;
    if(keyOffset<8 || keyOffset+32+8+16>len || (keyOffset-8)%8)
        return fail(out,"Invalid KELF key/signature offsets");
    const std::size_t sigOffset=keyOffset-8, bitOffset=keyOffset+32;
    B8 zero{};
    const auto macChain=cbc(bytes,sigOffset,keys.sigMaster.data(),false,false,zero);
    if(macChain.empty())return fail(out,"Invalid signature prefix size");
    std::uint64_t mac=fromBe(macChain.data()+macChain.size()-8);
    mac=desBlock(mac,keys.sigHash.data(),true);
    mac=desBlock(mac,keys.sigMaster.data(),false);
    B8 hdrSig{};
    toBe(mac,hdrSig.data());
    if(!eq(hdrSig.data(),bytes+sigOffset,8))return fail(out,"Header signature mismatch");
    B8 seed{};
    for(int i=0;i<8;++i)seed[i]=bytes[i]^bytes[i+8];
    B16 kek{};
    B8 half{};
    for(int i=0;i<8;++i)half[i]=seed[i]^keys.kbitIV[i];
    toBe(ede2(fromBe(half.data()),keys.kbitMaster,false),kek.data());
    for(int i=0;i<8;++i)half[i]=seed[i]^keys.kcIV[i];
    toBe(ede2(fromBe(half.data()),keys.kcMaster,false),kek.data()+8);
    B16 kbit{},kc{};
    for(int i=0;i<2;++i) {
        toBe(ede2(fromBe(bytes+keyOffset+i*8),kek,true),kbit.data()+i*8);
        toBe(ede2(fromBe(bytes+keyOffset+16+i*8),kek,true),kc.data()+i*8);
    }
    const std::size_t bitLen=len-bitOffset-16;
    if(bitLen<8 || bitLen>0x3f8 || bitLen%8!=0 || (bitLen-8)%16!=0)
        return fail(out,"Invalid encrypted BIT size");
    auto bit=cbc(bytes+bitOffset,bitLen,kbit.data(),true,true,keys.tableIV);
    if(bit.size()!=bitLen)return fail(out,"BIT cipher error");
    const std::uint8_t blockCount=bit[4];
    if(bitLen!=8+static_cast<std::size_t>(blockCount)*16 ||
       bit[5]!=0 || bit[6]!=0 || bit[7]!=0)
        return fail(out,"Malformed BIT");
    const std::uint32_t contentOffset=u32(bit.data());
    if(contentOffset!=len) return fail(out,"BIT content offset mismatch");
    std::uint64_t sum=0;
    B8 digest{};
    std::copy(kbit.begin(),kbit.begin()+8,digest.begin());
    if(!eq(kbit.data(),kbit.data()+8,8))
        for(int i=0;i<8;++i)digest[i]^=kbit[i+8];
    for(int i=0;i<8;++i)digest[i]^=kc[i];
    if(!eq(kc.data(),kc.data()+8,8))
        for(int i=0;i<8;++i)digest[i]^=kc[i+8];
    for(std::size_t pos=0;pos<bitLen;pos+=8)
        for(int i=0;i<8;++i)digest[i]^=bit[pos+i];
    B16 sigKeys{};
    std::copy(keys.sigMaster.begin(),keys.sigMaster.end(),sigKeys.begin());
    std::copy(keys.sigHash.begin(),keys.sigHash.end(),sigKeys.begin()+8);
    B8 bitSig{};
    toBe(ede2(fromBe(digest.data()),sigKeys,false),bitSig.data());
    if(!eq(bitSig.data(),bytes+len-16,8))return fail(out,"BIT signature mismatch");
    std::vector<std::uint8_t> signedData;
    signedData.reserve(16+8*blockCount);
    signedData.insert(signedData.end(),hdrSig.begin(),hdrSig.end());
    signedData.insert(signedData.end(),bitSig.begin(),bitSig.end());
    for(std::size_t i=0;i<blockCount;++i) {
        const std::uint8_t* desc=bit.data()+8+i*16;
        const std::uint32_t sz=u32(desc),fl=u32(desc+4);
        if(fl&~3u)return fail(out,"Unsupported BIT block flags");
        sum+=sz;
        if(sum>0xffffffffu)return fail(out,"Content size overflow");
        if(fl&2)signedData.insert(signedData.end(),desc+8,desc+16);
    }
    if(sum!=contentSize)return fail(out,"BIT blocks do not cover declared content");
    const auto rootChain=cbc(signedData.data(),signedData.size(),keys.rootMaster.data(),false,false,zero);
    if(rootChain.empty())return fail(out,"Root signature input empty");
    B8 root{};
    toBe(ede2(fromBe(rootChain.data()+rootChain.size()-8),keys.rootHash,true),root.data());
    if(!eq(root.data(),bytes+len-8,8))return fail(out,"Root signature mismatch");
    out.valid=true;
    out.contentOffset=contentOffset;
    out.contentSize=static_cast<std::uint32_t>(contentSize);
    out.blockCount=blockCount;
    out.contentKeyCount=static_cast<std::uint8_t>((flags>>4)&3);
    out.bit=std::move(bit);
    out.kbit=kbit;
    out.kc=kc;
    return true;
}

struct ContentCursor {
    std::size_t blockIndex = 0;
    std::uint32_t blockOffset = 0;
};

struct ContentResult {
    std::vector<std::uint8_t> plaintext;
    ContentCursor next;
    std::string error;
    bool valid = false;
};

// Process a SCmd92 input window. A signed/encrypted descriptor must arrive
// complete so its authentication can finish before any plaintext is released.
// Unsigned plaintext descriptors may be split across arbitrary windows.
inline bool DecryptContentTransfer(const std::uint8_t* input, std::size_t length,
                                   const Keys& keys, const HeaderResult& header,
                                   ContentCursor cursor, ContentResult& result) {
    using namespace Detail;
    result = ContentResult{};
    auto failContent = [&](const char* why) {
        result = ContentResult{};
        result.error = why;
        return false;
    };
    if (!header.valid || header.bit.size() != 8 + static_cast<std::size_t>(header.blockCount)*16)
        return failContent("Missing authenticated BIT");
    if (!input || !length || length > 0xffff)
        return failContent("Invalid content transfer length");
    result.plaintext.reserve(length);
    std::size_t pos = 0;
    while (pos < length) {
        if (cursor.blockIndex >= header.blockCount)
            return failContent("Content exceeds authenticated BIT");
        const std::uint8_t* desc = header.bit.data() + 8 + cursor.blockIndex*16;
        const std::uint32_t size = u32(desc);
        const std::uint32_t flags = u32(desc+4);
        if (cursor.blockOffset > size)
            return failContent("Invalid content cursor");
        // The loader copies unprotected BIT blocks itself. Only protected blocks
        // are submitted to MechaCon via SCmd 0x92/0x8D. Advance the BIT cursor
        // across those descriptors without consuming transfer bytes.
        if (!size || cursor.blockOffset == size || flags == 0) {
            ++cursor.blockIndex;
            cursor.blockOffset = 0;
            continue;
        }
        const std::size_t count = std::min<std::size_t>(size-cursor.blockOffset, length-pos);
        if (flags && (cursor.blockOffset || count != size))
            return failContent("Protected BIT block needs a complete transfer");
        if ((flags & 1) && (!keys.hasContentIV || (size % 8) || (header.contentKeyCount != 1 && header.contentKeyCount != 2)))
            return failContent("Missing IV or unsupported content cipher");
        if ((flags & 2) && (size < 8 || (size % 8)))
            return failContent("Invalid signed block size");
        std::vector<std::uint8_t> plain;
        if (flags & 1) {
            plain = cbc(input+pos, count, header.kc.data(),
                        header.contentKeyCount == 2, true, keys.contentIV);
            if (plain.size() != count)
                return failContent("Content decryption failed");
        } else {
            plain.assign(input+pos, input+pos+count);
        }
        if (flags & 2) {
            B8 calculated{};
            if (flags & 1) {
                // Encrypted-block MAC: XOR decrypted eight-byte words,
                // then two-key EDE under the header signature keys.
                for (std::size_t j=0; j<plain.size(); j+=8)
                    for (int k=0; k<8; ++k) calculated[k]^=plain[j+k];
                B16 macKeys{};
                std::copy(keys.sigMaster.begin(), keys.sigMaster.end(), macKeys.begin());
                std::copy(keys.sigHash.begin(), keys.sigHash.end(), macKeys.begin()+8);
                toBe(ede2(fromBe(calculated.data()),macKeys,false),calculated.data());
            } else {
                // Unencrypted signed-block MAC: DES CBC, D(Khash), E(Kmaster).
                B8 zero{};
                const auto chain = cbc(plain.data(), plain.size(), keys.sigMaster.data(), false, false, zero);
                if (chain.empty())
                    return failContent("Invalid signed block");
                std::uint64_t signature=fromBe(chain.data()+chain.size()-8);
                signature=desBlock(signature,keys.sigHash.data(),true);
                signature=desBlock(signature,keys.sigMaster.data(),false);
                toBe(signature,calculated.data());
            }
            if (!eq(calculated.data(), desc+8, 8))
                return failContent("Content block signature mismatch");
        }
        result.plaintext.insert(result.plaintext.end(),plain.begin(),plain.end());
        pos += count;
        cursor.blockOffset += static_cast<std::uint32_t>(count);
        if (cursor.blockOffset == size) {
            ++cursor.blockIndex;
            cursor.blockOffset = 0;
        }
    }
    result.next = cursor;
    result.valid = true;
    return true;
}

} // namespace MagicGateCrypto
