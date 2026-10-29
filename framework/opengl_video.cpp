#include "headers/opengl_game.h"
#include "headers/opengl_movie_messages.h"
#include "headers/remote.h"
#include <array>
#include <exception>
#include <unordered_map>

namespace CharonGL {
// Implemented beside the scene callbacks so videos use the same gamma/filter.
bool FinishVideoFrame();

namespace {
// Preferred addresses refer to the supported Windows 1.14d image.
const auto imageBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
using CodecCall = void (__stdcall*)(void*);
using CodecWait = int (__stdcall*)(void*);
using SmackOpen = void* (__stdcall*)(const char*, unsigned, int);
using SmackBuffer = void (__stdcall*)(void*, unsigned, unsigned, unsigned, unsigned, void*, unsigned);
using BinkOpen = void* (__stdcall*)(HANDLE, unsigned);
using BinkCopy = int (__stdcall*)(void*, void*, int, unsigned, unsigned, unsigned, unsigned);
using SoundSystem = int (__stdcall*)(void*, unsigned);

REMOTEFUNC(void __fastcall, SOUNDSMP_SetGlobalVolume, (int), (imageBase + 0x515ce0 - 0x400000));
REMOTEFUNC(BOOL __fastcall, GetUseDirectCommand, (), (imageBase + 0x406d20 - 0x400000));
REMOTEFUNC(BOOL __stdcall, SFileOpenFileEx, (void*, const char*, unsigned, HANDLE*), (imageBase + 0x4192f0 - 0x400000));
REMOTEFUNC(BOOL __stdcall, SFileLoadFileEx, (void*, const char*, void**, DWORD*, unsigned, unsigned, unsigned), (imageBase + 0x41b680 - 0x400000));
REMOTEFUNC(BOOL __stdcall, SBmpDecodeImage, (int, void*, unsigned, void*, void*, int, unsigned*, unsigned*, unsigned*), (imageBase + 0x6c6a80 - 0x400000));
REMOTEFUNC(int __stdcall, SMemFree, (void*, const char*, int, unsigned), (imageBase + 0x412650 - 0x400000));
REMOTEREF(int, gnRenderState, (imageBase + 0x7c9144 - 0x400000));
REMOTEREF(DWORD, gdwGlideBinkClickTimestamp, (imageBase + 0x880c1c - 0x400000));
// Codec imports are function pointers stored in the relocated IAT.
REMOTEREF(CodecCall, binkClose, (imageBase + 0x6cc5cc - 0x400000));
REMOTEREF(SoundSystem, binkSetSoundSystem, (imageBase + 0x6cc5d4 - 0x400000));
REMOTEREF(void*, binkSoundSystem, (imageBase + 0x6cc5d8 - 0x400000));
REMOTEREF(BinkOpen, binkOpen, (imageBase + 0x6cc5b8 - 0x400000));
REMOTEREF(CodecWait, binkWait, (imageBase + 0x6cc5c8 - 0x400000));
REMOTEREF(CodecCall, binkDoFrame, (imageBase + 0x6cc5c4 - 0x400000));
REMOTEREF(BinkCopy, binkCopyToBuffer, (imageBase + 0x6cc5c0 - 0x400000));
REMOTEREF(CodecCall, binkNextFrame, (imageBase + 0x6cc5bc - 0x400000));
REMOTEREF(CodecCall, smackClose, (imageBase + 0x6cc608 - 0x400000));
REMOTEREF(SmackOpen, smackOpen, (imageBase + 0x6cc604 - 0x400000));
REMOTEREF(SmackBuffer, smackToBuffer, (imageBase + 0x6cc60c - 0x400000));
REMOTEREF(CodecWait, smackWait, (imageBase + 0x6cc5f8 - 0x400000));
REMOTEREF(CodecCall, smackDoFrame, (imageBase + 0x6cc5fc - 0x400000));
REMOTEREF(CodecCall, smackNextFrame, (imageBase + 0x6cc600 - 0x400000));

// The original Smacker callbacks share one 512 x 64 indexed decode surface.
// Keep it alive across window recreation: SmackToBuffer retains its address.
std::array<std::uint8_t, 512 * 64> smackPixels{};
std::array<std::uint32_t, 256> smackPalette{};
std::unordered_map<void*, void*> smackStreams;
bool cancelBink = false;

void SetMode(void* context, char mode) { Write(context, 4, mode); }
char Mode(void* context) { return Read<char>(context, 4); }
bool Renderable() {
    return Device::Ready() && gnRenderState == 0;
}
void SetSmackPalette(const std::uint8_t* rgb, unsigned stride) {
    // Match the original RGB565 texture precision, without reducing RGB to luma.
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned r = rgb[i * stride] >> 3;
        const unsigned g = rgb[i * stride + 1] >> 2;
        const unsigned b = rgb[i * stride + 2] >> 3;
        smackPalette[i] = Rgba((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
    }
}
bool DrawVideo(Image image, float x, float y, float width, float height, bool linear) {
    if (!image.name) return false;
    const Vertex vertices[] = {
        {x,y,0,0,Rgba(255,255,255)}, {x+width,y,1,0,Rgba(255,255,255)},
        {x,y+height,0,1,Rgba(255,255,255)}, {x+width,y+height,1,1,Rgba(255,255,255)}
    };
    const bool result = Device::Draw(Primitive::TriangleStrip,vertices,4,image,
        nullptr,Blend::Replace,255,false,linear);
    Device::DeleteImage(image);
    return result;
}
bool DrawSmack(void* context, unsigned width, unsigned height) {
    if (!width || width > 512 || !height || height > 64) {
        Device::Fail("Smacker frame exceeds the recovered 512 x 64 decode surface"); return false;
    }
    std::vector<std::uint32_t> rgba(std::size_t(width) * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            rgba[std::size_t(y) * width + x] = smackPalette[smackPixels[std::size_t(y) * 512 + x]];
    return DrawVideo(Device::UploadRgba(rgba.data(),{int(width),int(height)}),
        float(Read<int>(context,8)), float(Read<int>(context,12)) - float(height),
        float(width),float(height),true);
}

struct Movie {
    HANDLE file = nullptr;
    void* codec = nullptr;
    bool started = false;
    ~Movie() {
        if (codec) {
            SOUNDSMP_SetGlobalVolume(0);
            binkClose(codec);
        }
        if (file) CloseHandle(file);
        if (codec) SOUNDSMP_SetGlobalVolume(255);
        if (started) videoPlaying = false;
    }
};

void PlayMovie(const char* filename, int audio, void* frameCallback) {
    if (!filename) { Device::Fail("Null cutscene filename"); return; }
    if (videoPlaying || !Renderable()) return;
    Movie movie;
    videoPlaying = true; movie.started = true; cancelBink = false;
    binkSetSoundSystem(binkSoundSystem,0);
    const unsigned scope = GetUseDirectCommand() ? 5 : 4;
    if (!SFileOpenFileEx(nullptr,filename,scope,&movie.file) || !movie.file) {
        Device::Fail("Cannot open cutscene through the game file service"); return;
    }
    const unsigned flags = audio == 1 ? 0x30800000u : audio == 2 ? 0x10800000u
        : audio == 3 ? 0x40800000u : 0x00800000u;
    movie.codec = binkOpen(movie.file,flags);
    if (!movie.codec) { Device::Fail("Bink could not open the cutscene"); return; }
    const unsigned width = Read<unsigned>(movie.codec,0), height = Read<unsigned>(movie.codec,4);
    // Bound allocations and signed codec pitch arithmetic before invoking it.
    if (!width || !height || width > 8192 || height > 8192) {
        Device::Fail("Invalid Bink frame dimensions"); return;
    }
    std::vector<std::uint8_t> bgr(std::size_t(width) * height * 4);
    std::vector<std::uint32_t> rgba(std::size_t(width) * height);
    const DWORD start = GetTickCount();
    bool ended = false, clicked = false;
    while (!cancelBink && Renderable() && (!ended || DWORD(GetTickCount()-start) < 500)) {
        // Original 0x50ff34 is a zero-argument CALL; return value is ignored.
        if (frameCallback) reinterpret_cast<void (__stdcall*)()>(frameCallback)();
        if (clicked || cancelBink || !Renderable()) break;
        const auto message = PumpMovieMessage(gdwGlideBinkClickTimestamp);
        if (message == MovieMessage::Quit) return;
        if (message != MovieMessage::None) {
            if (message == MovieMessage::Click) {
                clicked = true;
            }
            continue;
        }
        if (binkWait(movie.codec)) { Sleep(1); continue; }
        binkDoFrame(movie.codec);
        // This version's surface 2 is little-endian 32-bit BGRX (the original
        // Glide movie path also selects 2). Decode only into owned CPU memory.
        if (!binkCopyToBuffer(movie.codec,bgr.data(),int(width*4),height,0,0,2)) {
            Device::Fail("Bink could not copy a decoded cutscene frame"); return;
        }
        for (std::size_t i=0;i<rgba.size();++i)
            rgba[i] = Rgba(bgr[i*4+2],bgr[i*4+1],bgr[i*4]);
        if (!Device::BeginFrame()) return;
        Device::Clear();
        const Size canvas = Device::CanvasSize();
        const Viewport viewport = FitViewport({int(width),int(height)},canvas);
        if (!DrawVideo(Device::UploadRgba(rgba.data(),{int(width),int(height)}),
            float(viewport.x),float(viewport.y),float(viewport.width),float(viewport.height),false)
            || !FinishVideoFrame()) return;
        if (Read<unsigned>(movie.codec,12) == Read<unsigned>(movie.codec,8)) ended = true;
        else binkNextFrame(movie.codec);
    }
}

void Decode(const char* filename, void* context, int mode) {
    if (!context || !filename) { Device::Fail("Missing Smacker filename/context"); return; }
    smackPixels.fill(0);
    SetMode(context,char(mode));
    if (char(mode) == 'p') {
        void* data = nullptr;
        DWORD size = 0;
        const BOOL loaded = SFileLoadFileEx(nullptr,filename,&data,&size,0,3,0);
        if (data) {
            std::array<std::uint8_t,1024> colors{};
            std::array<std::uint8_t,470*60> pixels{};
            unsigned width=0,height=0,bpp=0;
            const BOOL decoded = loaded && SBmpDecodeImage(2,data,size,colors.data(),
                pixels.data(),int(pixels.size()),&width,&height,&bpp);
            SMemFree(data,"Charon OpenGL video",0,0);
            if (decoded && width == 470 && height == 60 && bpp == 8) {
                for (unsigned y=0;y<60;++y)
                    std::memcpy(smackPixels.data()+std::size_t(y)*512,pixels.data()+std::size_t(y)*470,470);
                SetSmackPalette(colors.data(),4); return;
            }
        }
        SetMode(context,0); Device::Fail("Cannot decode the 470 x 60 static video image"); return;
    }
    if (char(mode) != 's') return; // Original ignores unrecognized modes.
    if (Read<void*>(context,0)) {
        const auto found=smackStreams.find(context);
        if (found==smackStreams.end() || found->second!=Read<void*>(context,0)) {
            SetMode(context,0); Device::Fail("Smacker context does not own a native codec");
        }
        return;
    }
    // A caller can recycle its context storage after a renderer release. Never
    // overwrite a codec that is still owned by the registry.
    const auto previous=smackStreams.find(context);
    if (previous!=smackStreams.end()) {
        smackClose(previous->second); smackStreams.erase(previous);
    }
    void* codec = smackOpen(filename,0xfe000,-1);
    if (!codec) { SetMode(context,0); Device::Fail("Smacker could not open the video"); return; }
    const unsigned width=Read<unsigned>(codec,4),height=Read<unsigned>(codec,8);
    if (!width || width>512 || !height || height>64) {
        smackClose(codec); SetMode(context,0);
        Device::Fail("Smacker frame exceeds the recovered 512 x 64 decode surface"); return;
    }
    try { smackStreams.emplace(context,codec); }
    catch (...) { smackClose(codec); SetMode(context,0); throw; }
    Write(context,0,codec);
    smackToBuffer(codec,0,0,512,64,smackPixels.data(),0);
}
} // namespace

void __fastcall PlayCutScene(const char* filename,int audio,void* callback) {
    try { PlayMovie(filename,audio,callback); }
    catch (const std::exception&) { Device::Fail("Cannot allocate native cutscene buffers"); }
}
void __fastcall DecodeSmacker(const char* filename,void* context,int mode) {
    try { Decode(filename,context,mode); }
    catch (const std::exception&) { if (context) SetMode(context,0); Device::Fail("Cannot allocate native Smacker state"); }
}
void __fastcall CloseSmacker(void* context) {
    if (!context) { Device::Fail("Null Smacker context"); return; }
    if (Mode(context)=='s') {
        void* codec=Read<void*>(context,0);
        if (codec) {
            const auto found=smackStreams.find(context);
            if (found!=smackStreams.end() && found->second==codec) {
                smackClose(codec); smackStreams.erase(found);
            } else if (Device::Ready()) {
                Device::Fail("Smacker context does not own a native codec");
            }
            Write<void*>(context,0,nullptr);
        }
    }
    SetMode(context,0);
}
void __fastcall PlayerSmacker(void* context) {
    if (!context) { Device::Fail("Null Smacker context"); return; }
    try {
        if (Mode(context)=='p') { if (Renderable()) DrawSmack(context,470,60); return; }
        if (Mode(context)!='s') return;
        void* codec=Read<void*>(context,0);
        if (!codec) return;
        const auto found=smackStreams.find(context);
        if (found==smackStreams.end() || found->second!=codec) {
            Device::Fail("Smacker context does not own a native codec"); return;
        }
        const unsigned width=Read<unsigned>(codec,4),height=Read<unsigned>(codec,8);
        bool last=false;
        if (!smackWait(codec)) {
            if (!Device::Ready()) return;
            smackDoFrame(codec);
            last=Read<unsigned>(codec,0x374)==Read<unsigned>(codec,12);
            if (!last) smackNextFrame(codec);
            SetSmackPalette(static_cast<const std::uint8_t*>(codec)+0x6c,3);
        }
        if (Renderable()) {
            DrawSmack(context,width,height);
            // Original PlayerSmacker falls through to this second static-size
            // draw even for a streaming frame (0x50e867..79).
            DrawSmack(context,470,60);
        }
        // Original closes before reading the last palette/dimensions. Snapshot
        // those first so the native path never dereferences a freed codec.
        if (last) CloseSmacker(context);
    } catch (const std::exception&) { Device::Fail("Cannot allocate native Smacker frame"); }
}
void ClearVideoResources() {
    cancelBink=true;
    for (const auto& stream:smackStreams) smackClose(stream.second);
    smackStreams.clear();
    smackPixels.fill(0); smackPalette.fill(0);
}
} // namespace CharonGL
