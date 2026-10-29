// Compare the native adapter with the unmodified upstream five-pass GPU preset.
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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#ifdef NDEBUG
#error Build GPU checks with assertions enabled.
#endif

using namespace CharonGL;
static void Require(bool good, const char* operation) {
    if (!good) { std::fprintf(stderr,"%s: %s\n",operation,Device::LastError()); std::exit(1); }
}

namespace Reference {
constexpr GLenum Framebuffer=0x8d40, Attachment=0x8ce0, Complete=0x8cd5;
constexpr GLenum ArrayBuffer=0x8892, StaticDraw=0x88e4, Texture0=0x84c0;
constexpr GLenum VertexShader=0x8b31, FragmentShader=0x8b30, CompileStatus=0x8b81, LinkStatus=0x8b82;
#define FUNCTIONS(X) \
 X(GLuint,CreateShader,(GLenum)) \
 X(void,ShaderSource,(GLuint,GLsizei,const char* const*,const GLint*)) \
 X(void,CompileShader,(GLuint)) \
 X(void,GetShaderiv,(GLuint,GLenum,GLint*)) \
 X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,char*)) \
 X(void,DeleteShader,(GLuint)) \
 X(GLuint,CreateProgram,()) \
 X(void,AttachShader,(GLuint,GLuint)) \
 X(void,BindAttribLocation,(GLuint,GLuint,const char*)) \
 X(void,LinkProgram,(GLuint)) \
 X(void,GetProgramiv,(GLuint,GLenum,GLint*)) \
 X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,char*)) \
 X(void,DeleteProgram,(GLuint)) \
 X(void,UseProgram,(GLuint)) \
 X(GLint,GetUniformLocation,(GLuint,const char*)) \
 X(void,Uniform1i,(GLint,GLint)) \
 X(void,Uniform1f,(GLint,GLfloat)) \
 X(void,Uniform2f,(GLint,GLfloat,GLfloat)) \
 X(void,UniformMatrix4fv,(GLint,GLsizei,GLboolean,const GLfloat*)) \
 X(void,GenBuffers,(GLsizei,GLuint*)) \
 X(void,BindBuffer,(GLenum,GLuint)) \
 X(void,BufferData,(GLenum,std::ptrdiff_t,const void*,GLenum)) \
 X(void,DeleteBuffers,(GLsizei,const GLuint*)) \
 X(void,GenVertexArrays,(GLsizei,GLuint*)) \
 X(void,BindVertexArray,(GLuint)) \
 X(void,DeleteVertexArrays,(GLsizei,const GLuint*)) \
 X(void,EnableVertexAttribArray,(GLuint)) \
 X(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)) \
 X(void,GenFramebuffers,(GLsizei,GLuint*)) \
 X(void,BindFramebuffer,(GLenum,GLuint)) \
 X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
 X(GLenum,CheckFramebufferStatus,(GLenum)) \
 X(void,DeleteFramebuffers,(GLsizei,const GLuint*)) \
 X(void,ActiveTexture,(GLenum))
#define DECLARE(result,name,args) using name##Proc=result(APIENTRY*)args; static name##Proc name;
FUNCTIONS(DECLARE)
#undef DECLARE

static GLuint Shader(GLenum type,const std::string& text) {
    GLuint shader=CreateShader(type); const char* source=text.c_str();
    ShaderSource(shader,1,&source,nullptr); CompileShader(shader);
    GLint good=0; GetShaderiv(shader,CompileStatus,&good);
    if (!good) { char log[8192]{}; GetShaderInfoLog(shader,sizeof(log),nullptr,log);
        std::fprintf(stderr,"Upstream reference compile: %s\n",log); std::exit(1); }
    return shader;
}
static GLuint Program(const std::string& vertex,const std::string& fragment) {
    GLuint v=Shader(VertexShader,vertex),f=Shader(FragmentShader,fragment),program=CreateProgram();
    AttachShader(program,v); AttachShader(program,f);
    BindAttribLocation(program,0,"VertexCoord"); BindAttribLocation(program,1,"TexCoord");
    LinkProgram(program); DeleteShader(v); DeleteShader(f);
    GLint good=0; GetProgramiv(program,LinkStatus,&good);
    if (!good) { char log[8192]{}; GetProgramInfoLog(program,sizeof(log),nullptr,log);
        std::fprintf(stderr,"Upstream reference link: %s\n",log); std::exit(1); }
    return program;
}
struct Target {
    GLuint texture=0,framebuffer=0;
    Target(Size size, bool floating=false,const void* data=nullptr) {
        glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812f);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812f);
        glTexImage2D(GL_TEXTURE_2D,0,floating ? 0x8814 : 0x8058,size.width,size.height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        GenFramebuffers(1,&framebuffer); BindFramebuffer(Framebuffer,framebuffer);
        FramebufferTexture2D(Framebuffer,Attachment,GL_TEXTURE_2D,texture,0);
        Require(CheckFramebufferStatus(Framebuffer)==Complete,"reference framebuffer");
    }
    ~Target() { DeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture); }
    Target(const Target&)=delete;
    Target& operator=(const Target&)=delete;
};
static void Flip(std::vector<std::uint8_t>& pixels,Size size) {
    for (int y=0;y<size.height/2;++y)
        for (int x=0;x<size.width*4;++x)
            std::swap(pixels[std::size_t(y)*size.width*4+x],pixels[std::size_t(size.height-1-y)*size.width*4+x]);
}
struct Pipeline {
    GLuint programs[5]{},output=0,vao=0,vbo=0;
    Pipeline() {
#define LOAD(result,name,args) name=reinterpret_cast<name##Proc>(wglGetProcAddress("gl" #name)); Require(name!=nullptr,"load reference GL " #name);
        FUNCTIONS(LOAD)
#undef LOAD
        for (int pass=0;pass<5;++pass) {
            const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"third_party/scalefx/upstream/shaders"/("scalefx-pass"+std::to_string(pass)+".glsl");
            std::ifstream file(path,std::ios::binary); Require(bool(file),"read upstream reference shader");
            std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
            // Only the GLSL version and stage defines differ from the upstream source.
            text.erase(0,text.find('\n')+1);
            programs[pass]=Program("#version 330 core\n#define VERTEX\n"+text,"#version 330 core\n#define FRAGMENT\n"+text);
        }
        output=Program(R"GLSL(#version 330 core
in vec4 VertexCoord; in vec4 TexCoord; out vec2 uv;
void main(){gl_Position=VertexCoord;uv=TexCoord.xy;}
)GLSL",R"GLSL(#version 330 core
in vec2 uv; uniform sampler2D source; uniform float gammaValue; out vec4 color;
void main(){color=vec4(pow(max(texture(source,uv).rgb,vec3(0)),vec3(1.0/gammaValue)),1);}
)GLSL");
        const float vertices[]={-1,-1,0,0,-1,1,0,1,1,1,1,1,1,-1,1,0};
        GenVertexArrays(1,&vao); BindVertexArray(vao); GenBuffers(1,&vbo); BindBuffer(ArrayBuffer,vbo);
        BufferData(ArrayBuffer,sizeof(vertices),vertices,StaticDraw);
        EnableVertexAttribArray(0); VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
        EnableVertexAttribArray(1); VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(2*sizeof(float)));
    }
    ~Pipeline() {
        for (auto program:programs) DeleteProgram(program);
        DeleteProgram(output); DeleteBuffers(1,&vbo); DeleteVertexArrays(1,&vao);
    }
    std::vector<std::uint8_t> Run(std::vector<std::uint8_t> source,Size size,Size outputSize,float gamma) {
        ActiveTexture(Texture0); Flip(source,size);
        Target original(size,false,source.data());
        Target p0(size,true),p1(size,true),p2(size),p3(size),p4({size.width*3,size.height*3}),final(outputSize);
        Target* passes[]={&p0,&p1,&p2,&p3,&p4};
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); BindVertexArray(vao);
        const GLfloat identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (int i=0;i<5;++i) {
            GLuint program=programs[i]; UseProgram(program);
            BindFramebuffer(Framebuffer,passes[i]->framebuffer);
            glViewport(0,0,size.width*(i==4 ? 3 : 1),size.height*(i==4 ? 3 : 1));
            ActiveTexture(Texture0); glBindTexture(GL_TEXTURE_2D,i ? passes[i-1]->texture : original.texture);
            Uniform1i(GetUniformLocation(program,"Texture"),0);
            Uniform2f(GetUniformLocation(program,"TextureSize"),float(size.width),float(size.height));
            Uniform2f(GetUniformLocation(program,"InputSize"),float(size.width),float(size.height));
            Uniform2f(GetUniformLocation(program,"OutputSize"),float(size.width*(i==4 ? 3 : 1)),float(size.height*(i==4 ? 3 : 1)));
            UniformMatrix4fv(GetUniformLocation(program,"MVPMatrix"),1,GL_FALSE,identity);
            ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,i==2 ? p0.texture : original.texture);
            Uniform1i(GetUniformLocation(program,"PassPrev2Texture"),1);
            Uniform1i(GetUniformLocation(program,"PassPrev5Texture"),1);
            Uniform2f(GetUniformLocation(program,"PassPrev5TextureSize"),float(size.width),float(size.height));
            Uniform2f(GetUniformLocation(program,"PassPrev5InputSize"),float(size.width),float(size.height));
            glDrawArrays(GL_TRIANGLE_FAN,0,4);
        }
        BindFramebuffer(Framebuffer,final.framebuffer); glViewport(0,0,outputSize.width,outputSize.height);
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
        const auto view=FitViewport(size,outputSize);
        glViewport(view.x,outputSize.height-view.y-view.height,view.width,view.height);
        ActiveTexture(Texture0); glBindTexture(GL_TEXTURE_2D,p4.texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        UseProgram(output); Uniform1i(GetUniformLocation(output,"source"),0);
        Uniform1f(GetUniformLocation(output,"gammaValue"),gamma); glDrawArrays(GL_TRIANGLE_FAN,0,4);
        std::vector<std::uint8_t> result(std::size_t(outputSize.width)*outputSize.height*4);
        glReadPixels(0,0,outputSize.width,outputSize.height,GL_RGBA,GL_UNSIGNED_BYTE,result.data()); Flip(result,outputSize);
        Require(glGetError()==GL_NO_ERROR,"run independent ScaleFX pipeline");
        BindFramebuffer(Framebuffer,0); return result;
    }
};
#undef FUNCTIONS
}

static void Compare(const std::vector<std::uint8_t>& actual,const std::vector<std::uint8_t>& expected,Size source,Size target,unsigned fixture,float gamma) {
    assert(actual.size()==expected.size());
    unsigned worst=0; std::size_t at=0;
    for (std::size_t i=0;i<actual.size();++i) {
        const unsigned error=unsigned(std::abs(int(actual[i])-int(expected[i])));
        if (error>worst) { worst=error; at=i; }
    }
    if (worst>1) {
        std::fprintf(stderr,"ScaleFX %dx%d -> %dx%d fixture %u gamma %.2f: difference %u at byte %zu (native %u, upstream %u)\n",
            source.width,source.height,target.width,target.height,fixture,gamma,worst,at,unsigned(actual[at]),unsigned(expected[at]));
        std::exit(1);
    }
}
static unsigned cases=0,changedEdges=0;
static void CheckCase(HWND window,Reference::Pipeline& reference,Size size,Size target,unsigned fixture,float gamma=1) {
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(size),"resize ScaleFX source");
    std::vector<std::uint32_t> source(std::size_t(size.width)*size.height);
    std::mt19937 random(0x53465800+fixture);
    const std::array<std::uint32_t,4> palette={Rgba(8,24,48),Rgba(224,112,32),Rgba(40,184,120),Rgba(184,80,216)};
    for (int y=0;y<size.height;++y) for (int x=0;x<size.width;++x) {
        source[std::size_t(y)*size.width+x]=fixture==0 ? palette[2] : fixture==1 ? palette[(x>=y) ? 1 : 0]
            : fixture==2 ? palette[(x+y)%2] : fixture==3 ? palette[(x==size.width/2 || y==size.height/2) ? 2 : 0]
            : fixture==4 ? palette[random()%4] : Rgba(random()%256,random()%256,random()%256);
    }
    const Image image=Device::UploadRgba(source.data(),size); Require(image.name!=0,"upload ScaleFX fixture");
    const auto white=Rgba(255,255,255);
    const Vertex vertices[]={{0,0,0,0,white},{0,float(size.height),0,1,white},{float(size.width),float(size.height),1,1,white},{float(size.width),0,1,0,white}};
    Require(Device::Draw(Primitive::TriangleFan,vertices,4,image,nullptr,Blend::Replace,255,false),"draw ScaleFX source");
    std::vector<std::uint8_t> before,after,actual,repeated,nearest;
    Require(Device::ReadCanvas(before),"snapshot logical canvas");
    const auto expected=reference.Run(before,size,target,gamma);
    for (Filter filter:{Filter::Nearest,Filter::Xbrz,Filter::Cas,Filter::Jinc2,Filter::Crt,Filter::CrtCurved})
        Require(Device::Finish(filter),"switch through existing filters");
    Require(Device::Finish(Filter::Scalefx,gamma),"finish ScaleFX");
    GLint activeTexture=0,auxiliaryTexture=0;
    glGetIntegerv(0x84e0,&activeTexture); assert(activeTexture==Reference::Texture0);
    Reference::ActiveTexture(Reference::Texture0+1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&auxiliaryTexture);
    Reference::ActiveTexture(Reference::Texture0);
    // Later solid/RGBA draws must not sample their own scene framebuffer.
    Require(auxiliaryTexture==0,"release ScaleFX auxiliary input before later draws");
    Size output{}; Require(Device::ReadOutput(actual,output),"read ScaleFX result");
    assert(output.width==target.width && output.height==target.height);
    Compare(actual,expected,size,target,fixture,gamma);
    Require(Device::Finish(Filter::Scalefx,gamma),"repeat ScaleFX frame");
    Require(Device::ReadOutput(repeated,output),"read repeated ScaleFX frame"); assert(actual==repeated);
    Require(Device::ReadCanvas(after),"verify source canvas after ScaleFX"); assert(before==after);
    const auto view=FitViewport(size,target);
    for (int y=0;y<target.height;++y) for (int x=0;x<target.width;++x) {
        const auto at=(std::size_t(y)*target.width+x)*4; assert(actual[at+3]==255);
        if (x<view.x || x>=view.x+view.width || y<view.y || y>=view.y+view.height)
            assert(actual[at]==0 && actual[at+1]==0 && actual[at+2]==0);
    }
    if (gamma==1 && target.width==size.width*3 && target.height==size.height*3) {
        for (std::size_t i=0;i<actual.size();i+=4) {
            const auto color=Rgba(actual[i],actual[i+1],actual[i+2],actual[i+3]);
            assert(std::find(source.begin(),source.end(),color)!=source.end());
        }
        if (fixture==1) {
            Require(Device::Finish(Filter::Nearest),"nearest diagonal control");
            Require(Device::ReadOutput(nearest,output),"read nearest control"); assert(actual!=nearest); ++changedEdges;
        }
    }
    Device::DeleteImage(image); ++cases;
}
static void Benchmark(HWND window) {
    const Size size{800,600},target{1440,1080};
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::Create(window,size),"create benchmark device"); Device::SetVsync(false);
    std::printf("OpenGL renderer: %s; vendor: %s; version: %s\n",
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)),reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::vector<std::uint32_t> source(std::size_t(size.width)*size.height);
    std::mt19937 random(0x53465842);
    const std::array<std::uint32_t,4> palette={Rgba(8,24,48),Rgba(224,112,32),Rgba(40,184,120),Rgba(184,80,216)};
    for (int y=0;y<size.height;++y) for (int x=0;x<size.width;++x)
        source[std::size_t(y)*size.width+x]=palette[x<size.width/2 ? ((x/12+y/9)%4) : random()%4];
    const Image image=Device::UploadRgba(source.data(),size); Require(image.name!=0,"upload benchmark source");
    const auto white=Rgba(255,255,255);
    const Vertex vertices[]={{0,0,0,0,white},{0,float(size.height),0,1,white},
        {float(size.width),float(size.height),1,1,white},{float(size.width),0,1,0,white}};
    Require(Device::Draw(Primitive::TriangleFan,vertices,4,image,nullptr,Blend::Replace,255,false),"draw benchmark source");
    for (unsigned i=0;i<5;++i) { Require(Device::Finish(Filter::Scalefx),"warm up ScaleFX"); glFinish(); }
    std::array<double,120> milliseconds{};
    for (double& elapsed:milliseconds) {
        glFinish(); const auto start=std::chrono::steady_clock::now();
        Require(Device::Finish(Filter::Scalefx),"benchmark ScaleFX frame"); glFinish();
        elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    double total=0; for (double elapsed:milliseconds) total+=elapsed;
    std::sort(milliseconds.begin(),milliseconds.end());
    std::printf("ScaleFX 800x600 -> 1440x1080: mean %.3f ms, median %.3f ms, p95 %.3f ms.\n",
        total/milliseconds.size(),(milliseconds[59]+milliseconds[60])*0.5,milliseconds[113]);
    std::puts("Method: mixed diagonal bands/random palette fixture; 5 warmups, 120 Finish calls; glFinish before/after each timed frame; includes CPU submission and GPU completion, excludes upload, readback, references and SwapBuffers.");
    Device::DeleteImage(image); Device::Destroy();
}
int main(int argc,char** argv) {
    static_assert(sizeof(void*)==4,"Exercise the game's 32-bit driver.");
    static_assert(int(Filter::Nearest)==0 && int(Filter::Cas)==1 && int(Filter::Jinc2)==2 && int(Filter::Xbrz)==3 &&
        int(Filter::Scalefx)==4 && int(Filter::Crt)==5 && int(Filter::CrtCurved)==6,"Stable persisted filter ordering");
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.style=CS_OWNDC; cls.lpfnWndProc=DefWindowProcW; cls.hInstance=instance; cls.lpszClassName=L"CharonScaleFxTest";
    assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"ScaleFX GPU checks",WS_POPUP,0,0,27,21,nullptr,nullptr,instance,nullptr); assert(window);
    if (argc==2 && std::string(argv[1])=="--benchmark") {
        Benchmark(window); DestroyWindow(window); UnregisterClassW(cls.lpszClassName,instance); return 0;
    }
    for (unsigned context=0;context<2;++context) {
        Require(Device::Create(window,{9,7}),"create ScaleFX device"); Device::SetVsync(false);
        {
            Reference::Pipeline reference;
            if (context==0) {
                for (unsigned fixture=0;fixture<6;++fixture) {
                    CheckCase(window,reference,{9,7},{27,21},fixture);
                    CheckCase(window,reference,{9,7},{17,13},fixture);
                }
                for (Size target:{Size{9,7},Size{5,4},Size{43,21},Size{27,35}}) CheckCase(window,reference,{9,7},target,4);
                CheckCase(window,reference,{1,1},{7,11},5);
                CheckCase(window,reference,{1,7},{9,21},4);
                CheckCase(window,reference,{7,1},{21,9},4);
                CheckCase(window,reference,{9,7},{31,25},4,0.5f);
                CheckCase(window,reference,{9,7},{31,25},4,2.0f);
                CheckCase(window,reference,{31,23},{93,69},5);
                CheckCase(window,reference,{800,600},{1440,1080},4);
            } else CheckCase(window,reference,{11,9},{37,31},5,2.0f);
        }
        Require(Device::Present(),"present ScaleFX test frame"); Device::Destroy();
    }
    assert(changedEdges>0);
    DestroyWindow(window); UnregisterClassW(cls.lpszClassName,instance);
    std::printf("ScaleFX: %u upstream GPU/reference cases passed, including diagonal palette preservation, fractional scaling, gamma, resize and context recreation.\n",cases);
}
