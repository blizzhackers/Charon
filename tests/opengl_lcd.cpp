// Synthetic checks of the artistic LCD model, not measurements of a particular monitor.
#define NOMINMAX
#include "headers/opengl_device.h"
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>

#ifdef NDEBUG
#error Build GPU checks with assertions enabled.
#endif
#ifndef CHARON_OPENGL_TEST_CLOCK
#error Build this test and opengl_device.cpp with CHARON_OPENGL_TEST_CLOCK.
#endif

using namespace CharonGL;
using Color = std::array<unsigned,3>;
using State = std::array<double,3>;
static unsigned checks=0;

static void Require(bool good,const char* operation) {
    if (!good) { std::fprintf(stderr,"%s: %s\n",operation,Device::LastError()); std::exit(1); }
}
static void ResizeOutput(HWND window,Size size) {
    assert(SetWindowPos(window,nullptr,0,0,size.width,size.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
}
// The CPU oracle evaluates only the virtual-panel texels needed by a fixed probe grid.
// It uses doubles, and never reads shader code or production history.
static constexpr int panelWidth=1024,panelHeight=768;
static std::vector<Color> sourcePixels;
static Size sourceSize{};
struct Reference {
    bool ready=false;
    double time=0;
    float gamma=1;
    Size canvas{},output{};
    std::map<int,State> pixels;
};
static Reference history,pending;
static State SourceAt(int x,int y) {
    const double sx=(x+0.5)*sourceSize.width/panelWidth-0.5;
    const double sy=(panelHeight-y-0.5)*sourceSize.height/panelHeight-0.5;
    const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
    State result{};
    for (int j=0;j<2;++j) for (int i=0;i<2;++i) {
        const auto& color=sourcePixels[std::size_t(std::clamp(iy+j,0,sourceSize.height-1))*sourceSize.width+
            std::clamp(ix+i,0,sourceSize.width-1)];
        const double weight=(i ? sx-ix : 1-(sx-ix))*(j ? sy-iy : 1-(sy-iy));
        for (unsigned c=0;c<3;++c) result[c]+=color[c]/255.0*weight;
    }
    return result;
}
static double FrcFraction(int steps,double end,double ticks) {
    // Integrate each constant duty interval explicitly rather than using the shader's antiderivative.
    if (ticks<=0) return std::fmod(end+16,4)<steps ? 1 : 0;
    const double start=end-ticks;
    double high=0;
    for (int cycle=int(std::floor(start/4));cycle<=int(std::floor(end/4));++cycle)
        high+=std::max(0.0,std::min(end,cycle*4.0+steps)-std::max(start,cycle*4.0));
    return high/ticks;
}
static State Goal(int x,int y,float gamma,double end,double dt,bool reset) {
    const auto source=SourceAt(x,y);
    const double exponents[]={2.05,2.20,2.35},shift[]={0.17,0.20,0.23};
    const int bayer[2][2]={{0,2},{3,1}};
    State shaped{};
    for (unsigned c=0;c<3;++c) {
        const double q=std::pow(source[c],1.0/gamma)*63;
        const double low=std::floor(q)/63,high=std::min(1.0,low+1.0/63);
        const int steps=int(std::floor((q-std::floor(q))*4+0.5));
        const double fraction=reset ? steps/4.0 : FrcFraction(steps,end+bayer[y%2][x%2]+c,dt*60);
        const double exponent=exponents[c]+(2*(y+0.5)/panelHeight-1)*shift[c];
        shaped[c]=(1-fraction)*std::pow(low,exponent)+fraction*std::pow(high,exponent);
    }
    return {0.002+0.96*(0.94*shaped[0]+0.045*shaped[1]+0.015*shaped[2]),
        0.0022+0.96*(0.025*shaped[0]+0.95*shaped[1]+0.025*shaped[2]),
        0.0028+0.96*(0.02*shaped[0]+0.055*shaped[1]+0.925*shaped[2])};
}
static State Step(State previous,State goal,double dt,double speed=1) {
    for (unsigned c=0;c<3;++c) {
        const double dark=(1-goal[c])*(1-goal[c]);
        const double milliseconds=goal[c]>=previous[c] ? 22.4+16*dark : 28.8+22.4*dark;
        const double tau=(milliseconds/speed-4)/1000/std::log(9.0);
        previous[c]=goal[c]+(previous[c]-goal[c])*std::exp(-dt/tau);
    }
    return previous;
}
static double Coverage(double center,double width,double lo,double hi) {
    // Sum intersections with aperture intervals, independently of the GLSL integral.
    const double left=center-width/2,right=center+width/2;
    double area=0;
    for (int cell=int(std::floor(left));cell<=int(std::floor(right));++cell)
        area+=std::max(0.0,std::min(right,cell+hi)-std::max(left,cell+lo));
    return area/width/(hi-lo);
}
static int Encode(double value) {
    return int(std::lround(std::pow(std::clamp(value,0.0,1.0),1/2.2)*255));
}
static State OutputProfile(State light,double u,double v,int width,int height) {
    const double lo[]={.025,.358333,.691667},hi[]={.308333,.641667,.975};
    const double row=1+.10*(Coverage(v*panelHeight,double(panelHeight)/height,.035,.965)-1);
    const double nx=2*u-1,ny=2*v-1;
    const double patch=std::exp(-std::pow((u-.18)*2.8,2)-std::pow((v-.20)*4,2));
    const double gain=1-.018*(nx*nx+.6*ny*ny)+.012*patch;
    const double leak=.0007*(std::pow(std::abs(nx),8)+.75*std::pow(std::abs(ny),10))+.00025*patch;
    const double tint[]={1,.98,.92};
    for (unsigned c=0;c<3;++c)
        light[c]=light[c]*(1+.055*(Coverage(u*panelWidth,double(panelWidth)/width,lo[c],hi[c])-1))*row*gain+tint[c]*leak;
    return light;
}
static void Clear(Color color) {
    Device::Clear(std::uint8_t(color[0]),std::uint8_t(color[1]),std::uint8_t(color[2]));
    sourceSize=Device::CanvasSize(); sourcePixels.assign(std::size_t(sourceSize.width)*sourceSize.height,color);
}
static std::vector<std::uint8_t> Finish(double time,float gamma,Size& output) {
    Device::SetLcdTimeForTests(time);
    Require(Device::Finish(Filter::Lcd,gamma),"finish LCD");
    std::vector<std::uint8_t> result;
    Require(Device::ReadOutput(result,output),"read LCD output");
    return result;
}
static std::vector<std::uint8_t> CheckSource(double time,float gamma=1) {
    std::vector<std::uint8_t> before,after;
    Require(Device::ReadCanvas(before),"snapshot LCD source");
    Size output{}; auto actual=Finish(time,gamma,output);
    const auto canvas=Device::CanvasSize();
    const auto view=FitViewport(canvas,output);
    const double dt=time-history.time;
    const bool reset=!history.ready || history.gamma!=gamma || !std::isfinite(dt) || dt<0 || dt>.25 ||
        history.output.width!=output.width || history.output.height!=output.height ||
        history.canvas.width!=canvas.width || history.canvas.height!=canvas.height;
    pending={true,time,gamma,canvas,output,{}};
    const double phase=std::isfinite(time) ? std::fmod(time*60,4) : 0;
    auto panel=[&](int x,int y) {
        x=std::clamp(x,0,panelWidth-1); y=std::clamp(y,0,panelHeight-1);
        const int key=y*panelWidth+x;
        const auto found=pending.pixels.find(key);
        if (found!=pending.pixels.end()) return found->second;
        State state=Goal(x,y,gamma,phase,reset ? 0 : dt,reset);
        if (!reset) {
            const auto old=history.pixels.find(key); assert(old!=history.pixels.end());
            state=Step(old->second,state,dt);
        }
        pending.pixels[key]=state; return state;
    };
    for (int y=0;y<output.height;++y) for (int x=0;x<output.width;++x) {
        const auto at=(std::size_t(y)*output.width+x)*4;
        assert(actual[at+3]==255);
        if (x<view.x || x>=view.x+view.width || y<view.y || y>=view.y+view.height)
            assert(actual[at]==0 && actual[at+1]==0 && actual[at+2]==0);
    }
    // Include boundaries, center, and off-center samples where panel and output grids disagree.
    for (int py=0;py<9;++py) for (int px=0;px<11;++px) {
        const int x=px*(view.width-1)/10,y=py*(view.height-1)/8;
        const double u=(x+.5)/view.width,v=1-(y+.5)/view.height;
        const double tx=u*panelWidth-.5,ty=v*panelHeight-.5;
        const int ix=int(std::floor(tx)),iy=int(std::floor(ty));
        State light{};
        for (int j=0;j<2;++j) for (int i=0;i<2;++i) {
            const auto value=panel(ix+i,iy+j);
            const double weight=(i ? tx-ix : 1-(tx-ix))*(j ? ty-iy : 1-(ty-iy));
            for (unsigned c=0;c<3;++c) light[c]+=value[c]*weight;
        }
        light=OutputProfile(light,u,v,view.width,view.height);
        for (unsigned c=0;c<3;++c) {
            const auto at=(std::size_t(y+view.y)*output.width+x+view.x)*4+c;
            const int want=Encode(light[c]);
            if (std::abs(int(actual[at])-want)>1) {
                std::fprintf(stderr,"LCD check %u time %.9f probe %d,%d channel %u: GPU %u, CPU %d\n",
                    checks,time,x,y,c,unsigned(actual[at]),want); std::exit(1);
            }
        }
    }
    Require(Device::ReadCanvas(after),"verify LCD source unchanged"); assert(before==after);
    ++checks; return actual;
}
static std::vector<std::uint8_t> Check(Color color,double time,float gamma=1) {
    Clear(color); return CheckSource(time,gamma);
}
static void Commit() { Require(Device::Present(),"commit LCD frame"); history=pending; }
static void Reset() { Require(Device::Finish(Filter::Nearest),"leave LCD"); history.ready=false; }
static void Seed(Color color,double time=10) { Reset(); Check(color,time); Commit(); }

static void CheckProfileAndTransitions() {
    const Color black{0,0,0},white{255,255,255};
    for (Color color : {black,Color{8,8,8},Color{32,32,32},Color{64,64,64},Color{128,128,128},
            Color{192,192,192},white,Color{255,0,0},Color{0,255,0},Color{0,0,255},Color{70,130,210}})
        for (float gamma : {0.5f,1.0f,2.0f}) { Reset(); Check(color,10,gamma); Commit(); }
    // Negative controls: these inequalities fail if TN shift or the backlight floor is removed.
    Seed({128,128,128}); Size output{}; const auto ramp=Finish(10,1,output);
    const auto view=FitViewport(Device::CanvasSize(),output);
    const auto top=(std::size_t(view.y)*output.width+view.x+view.width/2)*4;
    const auto bottom=(std::size_t(view.y+view.height-1)*output.width+view.x+view.width/2)*4;
    assert(int(ramp[bottom])-int(ramp[top])>8);
    Seed(black); const auto dark=Finish(10,1,output); assert(dark[top]>8);
    for (Color from : {black,white,Color{30,70,120}}) {
        const Color to=from==white ? black : white;
        Seed(from); double time=10;
        for (double dt : {.001,.004,.008,.016,.033,.080}) { time+=dt; Check(to,time); Commit(); }
    }
    // Different refresh rates integrate different FRC intervals: compare a bounded envelope.
    std::array<int,3> endpoints{}; unsigned index=0;
    for (int fps : {30,60,144}) {
        Seed(white); std::vector<std::uint8_t> frame;
        for (int i=1;i<=fps/6;++i) { frame=Check({83,117,149},10+double(i)/fps); Commit(); }
        endpoints[index++]=frame[(std::size_t(view.y+view.height/2)*output.width+view.x+view.width/2)*4];
    }
    assert(*std::max_element(endpoints.begin(),endpoints.end())-*std::min_element(endpoints.begin(),endpoints.end())<=2);
    // The requested 20% faster panel must settle further in the same time than the old profile.
    const State low{.002,.0022,.0028},high{.962,.9622,.9628};
    assert(Step(low,high,.016)[0]>Step(low,high,.016,.8)[0]+.05);
}

static void CheckCommitSemantics() {
    const Color black{0,0,0},white{255,255,255},gray{80,120,160};
    Seed(black);
    Check(white,10.010);
    Check(black,10.010); // An unpresented white frame must never enter history.
    const auto first=Check(white,10.020);
    assert(first==Check(white,10.020)); Commit();
    Check(gray,10.030); Commit(); // Only the last successful presentation advances time.
    Reset(); Check(white,20);
    Check(black,20.010); Commit(); // Initialization also stays pending until Present.
}

static void CheckResets(HWND window) {
    const Color black{0,0,0},white{255,255,255};
    for (Filter other : {Filter::Nearest,Filter::Cas,Filter::Jinc2,Filter::Xbrz,Filter::Scalefx,Filter::Crt,Filter::CrtCurved}) {
        Seed(white); Require(Device::Finish(other),"switch away from LCD"); history.ready=false;
        Check(black,10.001); Commit();
    }
    Seed(white); Require(Device::ResizeCanvas({7,5}),"resize LCD canvas"); Check(black,10.001); Commit();
    Seed(white); ResizeOutput(window,{43,35}); Check(black,10.001); Commit();
    Seed(white); Check({64,128,192},10.001,2); Commit();
    for (double time : {9.0,10.251,(std::numeric_limits<double>::infinity)(),
            (std::numeric_limits<double>::quiet_NaN)()}) {
        Seed(white); Check(black,time); Commit(); Check(white,time+1); Commit();
    }
    Seed(white); ResizeOutput(window,{0,0}); Device::SetLcdTimeForTests(10.001);
    Require(Device::Finish(Filter::Lcd),"finish with no drawable output"); history.ready=false;
    ResizeOutput(window,{40,30}); Check(black,10.002); Commit();
    Seed(white); Device::Destroy(); history.ready=false;
    Require(Device::Create(window,{4,3}),"recreate LCD context"); Device::SetVsync(false);
    Check(black,10.001); Commit();
}

static void DrawSource(const std::vector<Color>& colors,Size size) {
    std::vector<std::uint32_t> rgba; rgba.reserve(colors.size());
    for (const auto& c : colors) rgba.push_back(Rgba(c[0],c[1],c[2]));
    Image image=Device::UploadRgba(rgba.data(),size); Require(image.name!=0,"upload spatial LCD fixture");
    const auto white=Rgba(255,255,255);
    const float w=float(size.width),h=float(size.height);
    const Vertex vertices[]={{0,0,0,0,white},{0,h,0,1,white},{w,h,1,1,white},{w,0,1,0,white}};
    Require(Device::Draw(Primitive::TriangleFan,vertices,4,image,nullptr,Blend::Replace,255,false),"draw spatial LCD fixture");
    Device::DeleteImage(image); sourcePixels=colors; sourceSize=size;
}
static void CheckLinearBilinearAndBindings(HWND window) {
    Require(Device::ResizeCanvas({2,1}),"resize spatial LCD fixture"); ResizeOutput(window,{17,11});
    DrawSource({{0,0,0},{255,0,64}},{2,1});
    Require(Device::BeginFrame(),"bind source for feedback check");
    using AttachmentProc=void(APIENTRY*)(GLenum,GLenum,GLenum,GLint*);
    using ActiveProc=void(APIENTRY*)(GLenum);
    const auto attachment=reinterpret_cast<AttachmentProc>(wglGetProcAddress("glGetFramebufferAttachmentParameteriv"));
    const auto active=reinterpret_cast<ActiveProc>(wglGetProcAddress("glActiveTexture")); assert(attachment && active);
    GLint scene=0; attachment(0x8d40,0x8ce0,0x8cd1,&scene); assert(scene!=0);
    Reset(); CheckSource(30);
    GLint framebuffer=-1,bound=0; glGetIntegerv(0x8ca6,&framebuffer); assert(framebuffer==0);
    active(0x84c1); glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound); assert(bound==0);
    active(0x84c0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound); assert(bound!=scene);
    assert(glGetError()==GL_NO_ERROR); Commit();
    // A moving edge exercises distinct response histories on either side of the scaler transition.
    for (unsigned frame=1;frame<=8;++frame) {
        DrawSource(frame%2 ? std::vector<Color>{{255,0,64},{0,0,0}} : std::vector<Color>{{0,0,0},{255,0,64}},{2,1});
        CheckSource(30+frame/120.0); Commit();
    }
    // A static ramp checks every drive regime with bilinear source sampling.
    Require(Device::ResizeCanvas({16,1}),"resize ramp fixture");
    std::vector<Color> ramp;
    for (unsigned x=0;x<16;++x) ramp.push_back({x*17,255-x*17,x*17});
    DrawSource(ramp,{16,1}); Reset(); CheckSource(40); Commit();
}

static void CheckFrcAndApertures(HWND window) {
    Require(Device::ResizeCanvas({4,3}),"resize FRC fixture"); ResizeOutput(window,{1536,1152});
    Seed({128,128,128},50);
    Size initialSize{}; const auto initial=Finish(50,1,initialSize);
    const unsigned meanTarget=initial[(std::size_t(577)*initialSize.width+769)*4];
    std::vector<unsigned> values;
    for (unsigned frame=1;frame<=16;++frame) {
        const auto actual=Check({128,128,128},50+frame/240.0); Commit();
        values.push_back(actual[(std::size_t(577)*1536+769)*4]);
    }
    // The response filters FRC, keeping its residual fluctuation below a visible flashing step.
    assert(*std::max_element(values.begin(),values.end())-*std::min_element(values.begin(),values.end())<=4);
    double mean=0; for (unsigned value : values) mean+=value;
    // Average a whole four-tick cycle after startup; optical mean must stay near the reset duty mean.
    assert(std::abs(mean/values.size()-meanTarget)<=1);
    // Explicit four-phase duty means are independent of phase and spatial offset, including long frames.
    for (int steps=0;steps<=4;++steps) for (double end : {.17,1.6,3.99,6.2}) {
        assert(std::abs(FrcFraction(steps,end,4)-steps/4.0)<1e-12);
        assert(std::abs(FrcFraction(steps,end,12)-steps/4.0)<1e-12);
    }
    // Supersampling reveals the faint RGB aperture without relying on the CPU oracle.
    Seed({255,255,255},60); Size output{}; const auto white=Finish(60,1,output);
    const auto center=(std::size_t(576)*output.width+768)*4;
    const auto neighbor=center+4;
    assert(std::abs(int(white[center])-int(white[center+2]))>=2 ||
        std::abs(int(white[neighbor])-int(white[neighbor+2]))>=2);
    ResizeOutput(window,{40,30});
}

static void Benchmark(HWND window) {
    ResizeOutput(window,{1440,1080}); Require(Device::ResizeCanvas({800,600}),"resize LCD benchmark");
    std::vector<Color> pattern;
    for (unsigned y=0;y<600;++y) for (unsigned x=0;x<800;++x)
        pattern.push_back({x*255/799,y*255/599,(x+y)*255/1398});
    DrawSource(pattern,{800,600}); Reset(); double time=100;
    for (unsigned i=0;i<5;++i) {
        Device::SetLcdTimeForTests(time+=1.0/60); Require(Device::Finish(Filter::Lcd),"warm up LCD"); glFinish(); Commit();
    }
    std::array<double,120> milliseconds{};
    for (double& elapsed : milliseconds) {
        Device::SetLcdTimeForTests(time+=1.0/60); glFinish(); const auto start=std::chrono::steady_clock::now();
        Require(Device::Finish(Filter::Lcd),"benchmark LCD"); glFinish();
        elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(); Commit();
    }
    double total=0; for (double elapsed : milliseconds) total+=elapsed; std::sort(milliseconds.begin(),milliseconds.end());
    std::printf("LCD 800x600 -> 1440x1080: mean %.3f ms, median %.3f ms, p95 %.3f ms; 5 ms target %s.\n",
        total/120,(milliseconds[59]+milliseconds[60])*0.5,milliseconds[113],total/120<=5 ? "met" : "not met");
    std::puts("Method: mixed RGB gradients, 5 warmups and 120 timed Finish + glFinish calls; Present commits each frame outside timing. Excludes upload, readback and SwapBuffers.");
}

static void Capture(HWND window,const char* input,const char* outputPath) {
    const Size canvas{800,600};
    std::vector<std::uint8_t> rgba(std::size_t(canvas.width)*canvas.height*4);
    FILE* file=nullptr; Require(fopen_s(&file,input,"rb")==0 && file!=nullptr,"open capture source");
    const auto count=std::fread(rgba.data(),1,rgba.size(),file);
    const bool exact=count==rgba.size() && std::fgetc(file)==EOF && !std::ferror(file);
    std::fclose(file); Require(exact,"read 800x600 capture source RGBA");
    ResizeOutput(window,{1440,1080}); Require(Device::ResizeCanvas(canvas),"resize capture source");
    std::vector<Color> colors; colors.reserve(std::size_t(canvas.width)*canvas.height);
    for (std::size_t i=0;i<rgba.size();i+=4) colors.push_back({rgba[i],rgba[i+1],rgba[i+2]});
    DrawSource(colors,canvas); Reset(); Size output{}; const auto result=Finish(0,1,output);
    assert(output.width==1440 && output.height==1080);
    Require(fopen_s(&file,outputPath,"wb")==0 && file!=nullptr,"open capture output");
    const auto written=std::fwrite(result.data(),1,result.size(),file);
    const bool closed=std::fclose(file)==0;
    Require(written==result.size() && closed,"write capture RGBA");
    std::printf("LCD capture: %dx%d top-down RGBA, reset FRC duty mean at time zero.\n",output.width,output.height);
}

int main(int argc,char** argv) {
    static_assert(sizeof(void*)==4,"Exercise the game's 32-bit GL driver.");
    static_assert(int(Filter::Nearest)==0 && int(Filter::Cas)==1 && int(Filter::Jinc2)==2 && int(Filter::Xbrz)==3 &&
        int(Filter::Scalefx)==4 && int(Filter::Crt)==5 && int(Filter::CrtCurved)==6 && int(Filter::Lcd)==7 && int(Filter::Count)==8,
        "Stable persisted filter ordering");
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.style=CS_OWNDC; cls.lpfnWndProc=DefWindowProcW; cls.hInstance=instance; cls.lpszClassName=L"CharonLcdTest";
    assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"LCD GPU checks",WS_POPUP,0,0,40,30,nullptr,nullptr,instance,nullptr); assert(window);
    Require(Device::Create(window,{4,3}),"create LCD device"); Device::SetVsync(false);
    std::printf("OpenGL renderer: %s; vendor: %s; version: %s\n",glGetString(GL_RENDERER),glGetString(GL_VENDOR),glGetString(GL_VERSION));
    if (argc==4 && std::string(argv[1])=="--capture") Capture(window,argv[2],argv[3]);
    else if (argc==2 && std::string(argv[1])=="--benchmark") Benchmark(window);
    else {
        CheckProfileAndTransitions(); CheckCommitSemantics(); CheckResets(window); CheckLinearBilinearAndBindings(window); CheckFrcAndApertures(window);
        std::printf("LCD: %u GPU/double-reference checks passed, plus TN/FRC/aperture invariants, virtual-panel bilinear reconstruction, source preservation and texture binding checks.\n",checks);
    }
    Device::Destroy(); assert(DestroyWindow(window)); assert(UnregisterClassW(cls.lpszClassName,instance));
}
