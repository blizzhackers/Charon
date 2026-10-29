// Runs only the original CPU RLE routine. The game entry point is never called.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include "headers/opengl_sprite_decode.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#pragma comment(lib,"advapi32.lib")
#ifdef NDEBUG
#error Original comparison requires assertions.
#endif

static std::string HashFile(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) return {};
    HCRYPTPROV provider=0; HCRYPTHASH hash=0;
    bool good=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)!=FALSE;
    if (good) good=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    std::array<BYTE,65536> buffer{};
    DWORD count=0;
    while (good) {
        if (!ReadFile(file,buffer.data(),DWORD(buffer.size()),&count,nullptr)) {good=false; break;}
        if (!count) break;
        good=CryptHashData(hash,buffer.data(),count,0)!=FALSE;
    }
    std::array<BYTE,32> digest{}; DWORD length=DWORD(digest.size());
    if (good) good=CryptGetHashParam(hash,HP_HASHVAL,digest.data(),&length,0)!=FALSE;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider,0);
    CloseHandle(file);
    if (!good || length!=32) return {};
    std::string result;
    for (BYTE byte:digest) {const char* hex="0123456789abcdef"; result+=hex[byte>>4]; result+=hex[byte&15];}
    return result;
}

static bool RelocateImage(void* image) {
    auto* bytes=static_cast<std::uint8_t*>(image);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes+dos->e_lfanew);
    if (pe->Signature!=IMAGE_NT_SIGNATURE || pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return false;
    const auto delta=std::uint32_t(reinterpret_cast<std::uintptr_t>(image)-pe->OptionalHeader.ImageBase);
    if (!delta) return true;
    const auto directory=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!directory.VirtualAddress || directory.VirtualAddress>=pe->OptionalHeader.SizeOfImage ||
        directory.Size>pe->OptionalHeader.SizeOfImage-directory.VirtualAddress) return false;
    std::size_t cursor=directory.VirtualAddress, end=cursor+directory.Size;
    while (cursor<end) {
        if (end-cursor<sizeof(IMAGE_BASE_RELOCATION)) return false;
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(bytes+cursor);
        if (block->SizeOfBlock<sizeof(*block) || block->SizeOfBlock>end-cursor || block->SizeOfBlock%2)
            return false;
        const auto* entries=reinterpret_cast<const WORD*>(block+1);
        const auto count=(block->SizeOfBlock-sizeof(*block))/sizeof(WORD);
        for (std::size_t i=0;i<count;++i) {
            const unsigned type=entries[i]>>12;
            if (type==IMAGE_REL_BASED_ABSOLUTE) continue;
            const auto rva=std::uint64_t(block->VirtualAddress)+(entries[i]&0xfff);
            if (type!=IMAGE_REL_BASED_HIGHLOW || rva+4>pe->OptionalHeader.SizeOfImage) return false;
            auto* target=bytes+std::size_t(rva);
            DWORD protection=0,ignored=0;
            if (!VirtualProtect(target,4,PAGE_WRITECOPY,&protection)) return false;
            std::uint32_t value=0; std::memcpy(&value,target,4); value+=delta;
            std::memcpy(target,&value,4);
            if (!VirtualProtect(target,4,protection,&ignored)) return false;
        }
        cursor+=block->SizeOfBlock;
    }
    return FlushInstructionCache(GetCurrentProcess(),image,pe->OptionalHeader.SizeOfImage)!=FALSE;
}

int wmain(int argc,wchar_t** argv) {
    static_assert(sizeof(void*)==4,"Original x86 machine code requires an x86 process.");
    if (argc!=2 || HashFile(argv[1])!="631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf") {
        std::fputs("Provide the exact supported Game.exe; SHA-256 verification failed.\n",stderr); return 1;
    }
    HANDLE file=CreateFileW(argv[1],GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file==INVALID_HANDLE_VALUE) return 2;
    HANDLE image=CreateFileMappingW(file,nullptr,PAGE_READONLY|SEC_IMAGE,0,0,nullptr);
    void* original=image?MapViewOfFile(image,FILE_MAP_READ,0,0,0):nullptr;
    if (!original || !RelocateImage(original)) {
        std::fprintf(stderr,"Original image mapping/relocation failed: %lu\n",GetLastError());
        if (original) UnmapViewOfFile(original);
        if (image) CloseHandle(image); CloseHandle(file); return 2;
    }
    const auto base=reinterpret_cast<std::uintptr_t>(original);
    // Only PE relocation fixups change private copy-on-write pages. The original
    // file is read only; no entry point, imports, TLS or game initialization run.
    std::uintptr_t jumpTable=0,firstTarget=0;
    std::memcpy(&jumpTable,reinterpret_cast<const void*>(base+0x282a1f),4);
    std::memcpy(&firstTarget,reinterpret_cast<const void*>(base+0x282b34),4);
    if (jumpTable!=base+0x282b34 || firstTarget<base+0x2829c0 || firstTarget>=base+0x282c00) {
        std::fputs("Original image's internal memcpy references were not relocated correctly.\n",stderr);
        UnmapViewOfFile(original); CloseHandle(image); CloseHandle(file); return 2;
    }
    using OriginalDecode=void (__fastcall*)(const std::uint8_t*,std::uint8_t*,int,int);
    // Verified ECX/EDX live-ins, RET 8, and its sole descendant is original memcpy.
    const auto decode=reinterpret_cast<OriginalDecode>(base+0x206d40);
    std::mt19937 random(0x43484152);
    constexpr std::size_t guard=32;
    unsigned cases=0;
    for (int sample=0;sample<1024;++sample) {
        const int width=1+int(random()%256),height=1+int(random()%256);
        std::vector<std::uint8_t> stream;
        for (int row=0;row<height;++row) {
            for (int x=0;x<width;) {
                const int available=(std::min)(127,width-x);
                const int run=1+int(random()%available);
                const bool skip=(random()%3)==0;
                stream.push_back(std::uint8_t(run|(skip?0x80:0)));
                if (!skip) for (int n=0;n<run;++n) stream.push_back(std::uint8_t(random()));
                x+=run;
            }
            stream.push_back(0x80);
        }
        for (unsigned flip=0;flip<2;++flip) {
            const auto bytes=std::size_t(width)*height;
            const auto reference=[&] {
                std::vector<std::uint8_t> result(bytes+2*guard,0xc5);
                std::fill(result.begin()+guard,result.end()-guard,std::uint8_t(0));
                auto* output=result.data()+guard+(flip?0:std::size_t(height-1)*width);
                decode(stream.data(),output,height,flip?width:-width);
                assert(std::all_of(result.begin(),result.begin()+guard,[](auto b){return b==0xc5;}));
                assert(std::all_of(result.end()-guard,result.end(),[](auto b){return b==0xc5;}));
                return std::vector<std::uint8_t>(result.begin()+guard,result.end()-guard);
            };
            const auto first=reference(),second=reference();
            assert(first==second);
            CharonGL::Sprite::Decoded candidate;
            assert(CharonGL::Sprite::Decode(stream.data(),stream.size(),width,height,flip,candidate));
            assert(candidate.indices==first);
            auto altered=first; altered[0]^=1;
            assert(candidate.indices!=altered);
            ++cases;
        }
    }
    UnmapViewOfFile(original); CloseHandle(image); CloseHandle(file);
    std::printf("Original-only sprite decoder: %u cases passed A/A, A/B, output guards and negative controls.\n",cases);
}
