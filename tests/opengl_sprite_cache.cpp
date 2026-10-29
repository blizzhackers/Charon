// Exercise the production cache, including its borrowed game-frame view.
#include "framework/opengl_sprites.cpp"
#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Build sprite-cache checks with assertions enabled.
#endif

namespace CharonGL {
    GameOptions* gameOptions = nullptr;
    std::array<std::uint32_t,256> palette{};
    std::array<const std::uint8_t*,72> paletteTables{};
    Light globalLight{};
    std::uint8_t constantAlpha = 255;
    Blend currentBlend = Blend::Replace;
    bool videoPlaying = false;
    void SelectDrawMode(int) {}
    bool PerspectiveEnabled() { return false; }
    int PerspectiveScale() { return 256; }
    void Project(std::int32_t x,std::int32_t y,std::int32_t,int& sx,int& sy) { sx=x; sy=y; }
    namespace Device {
        unsigned uploads = 0, deletes = 0, drawn = 0, errors = 0;
        std::vector<std::uint8_t> lastUpload;
        Size CanvasSize() { return {640,480}; }
        void Fail(const char*) { ++errors; }
        Image UploadIndices(const std::uint8_t* pixels,Size size) {
            lastUpload.assign(pixels,pixels+std::size_t(size.width)*size.height);
            return {++uploads,size.width,size.height};
        }
        void DeleteImage(Image image) { if (image.name) ++deletes; }
        bool Draw(Primitive,const Vertex*,std::size_t,Image image,const std::uint32_t*,Blend,
            std::uint8_t,bool,bool) { drawn=image.name; return true; }
    }
}

static int __stdcall Loaded(void*,int,int) { return 1; }

int main() {
    using namespace CharonGL;
    SPRITECACHE_GetOrLoadSprite = Loaded;
    std::array<std::uint8_t,64> gfx{};
    std::array<std::uint8_t,36> frame{};
    Write(gfx.data(),0x3c,static_cast<void*>(frame.data()));
    Write(frame.data(),4,2); Write(frame.data(),8,1);
    Write(frame.data(),28,std::uint32_t(4));
    frame[32]=2; frame[33]=1; frame[34]=2; frame[35]=0x80;
    const auto draw=[&](const std::uint8_t* remap=nullptr,int mode=0) {
        assert(DrawImage(gfx.data(),0,1,0xffffffff,mode,remap));
        assert(Device::errors==0);
        return Device::drawn;
    };
    const auto first=draw();
    assert(Device::uploads==1 && cache.size()==1);
    const auto& owned=cache.begin()->second->encoded;
    assert(owned.data()!=frame.data()+32 && owned.size()==4);
    for(int i=0;i<10;++i) assert(draw()==first);
    assert(Device::uploads==1);
    // Recycled pointers and in-place mutation must compare bytes, not addresses.
    frame[33]=3;
    const auto changed=draw(); assert(changed!=first && Device::uploads==2);
    frame[33]=1; assert(draw()==first && Device::uploads==2);
    auto other=frame;
    Write(gfx.data(),0x3c,static_cast<void*>(other.data()));
    assert(draw()==first && Device::uploads==2);
    std::array<std::uint8_t,256> remap{};
    for(unsigned i=0;i<256;++i) remap[i]=std::uint8_t(i);
    assert(draw(remap.data())==first);
    remap[1]=7;
    const auto mapped=draw(remap.data()); assert(mapped!=first && Device::uploads==3);
    assert(Device::lastUpload[56]==7 && Device::lastUpload[57]==2);
    remap[1]=8;
    assert(draw(remap.data())!=mapped && Device::uploads==4);
    assert(Device::lastUpload[56]==8);
    remap[1]=7; assert(draw(remap.data())==mapped);
    std::array<std::uint8_t,256> brighter{};
    for(unsigned i=0;i<256;++i) brighter[i]=std::uint8_t(i);
    paletteTables[70]=brighter.data();
    assert(draw(nullptr,7)==first);
    brighter[1]=9; const auto bright=draw(nullptr,7);
    assert(bright!=first && Device::uploads==5);
    assert(Device::lastUpload[56]==9);
    brighter[1]=10; assert(draw(nullptr,7)!=bright && Device::uploads==6);
    ClearSpriteCache(); assert(cache.empty() && cacheBytes==0 && Device::deletes==6);
    assert(draw()!=first && Device::uploads==7);
    ClearSpriteCache(); assert(Device::deletes==7);
    // A hash bucket is only a candidate: compare the owned bytes exactly.
    const auto collision=draw();
    cache.begin()->second->encoded[1]=42;
    assert(draw()!=collision && Device::uploads==9);
    ClearSpriteCache(); assert(Device::deletes==9);
    std::puts("Production sprite cache: hits, recycled/mutated frames, remaps, brighter tables, clear passed.");
}
