// SPDX-License-Identifier: GPL-3.0-or-later
// Local-only authentic KELF regression: reads user-owned files; never prints keys or plaintext.
// No KELF, encrypted content, or key material is embedded in this tool.
#include "CDVD/MagicGateCrypto.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static bool readFile(const char* filename, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fputs("Usage: VerifyMagicGateFirstBlock <selected-BIOS.mgkeys> <416-byte-KELF-header> <1024-byte-content-ciphertext>\n",stderr);
        return 2;
    }
    std::vector<std::uint8_t> config, headerBytes, encryptedBlock;
    if (!readFile(argv[1],config) || !readFile(argv[2],headerBytes) || !readFile(argv[3],encryptedBlock)) {
        std::fputs("Failed to read one or more local input files\n",stderr);
        return 2;
    }
    if (headerBytes.size()!=416 || encryptedBlock.size()!=0x400) {
        std::fputs("Incorrect authentic fixture lengths\n",stderr);
        return 2;
    }
    MagicGateCrypto::Keys keys;
    std::string why;
    if (!MagicGateCrypto::ParseKeys(std::string(config.begin(),config.end()),keys,why)) {
        std::fprintf(stderr,"Key file invalid: %s\n",why.c_str());
        return 1;
    }
    MagicGateCrypto::HeaderResult header;
    if (!MagicGateCrypto::VerifyHeader(headerBytes.data(),headerBytes.size(),keys,header)) {
        std::fprintf(stderr,"Header verification failed: %s\n",header.error.c_str());
        return 1;
    }
    const std::array<std::uint8_t,8> expectedSignature={0xB0,0xC2,0x34,0x14,0xAD,0xAB,0x3B,0xD6};
    if (header.blockCount!=20 || header.bit.size()!=328 ||
        MagicGateCrypto::Detail::u32(header.bit.data()+8)!=0x400 ||
        MagicGateCrypto::Detail::u32(header.bit.data()+12)!=3 ||
        !MagicGateCrypto::Detail::eq(header.bit.data()+16,expectedSignature.data(),8)) {
        std::fputs("Authentic BIT first-block descriptor does not match the regression record\n",stderr);
        return 1;
    }
    MagicGateCrypto::ContentResult result;
    if (!MagicGateCrypto::DecryptContentTransfer(encryptedBlock.data(),encryptedBlock.size(),
                                                keys,header,{},result)) {
        std::fprintf(stderr,"Content verification failed: %s\n",result.error.c_str());
        return 1;
    }
    const std::array<std::uint8_t,4> elf={0x7f,'E','L','F'};
    if (result.plaintext.size()!=0x400 ||
        !MagicGateCrypto::Detail::eq(result.plaintext.data(),elf.data(),4) ||
        result.next.blockIndex!=1 || result.next.blockOffset!=0) {
        std::fputs("Content passed signature check but did not produce expected ELF boundary\n",stderr);
        return 1;
    }
    std::puts("PASS: authenticated header, BIT descriptor, 1024-byte content MAC and MIPS ELF prefix");
    return 0;
}
