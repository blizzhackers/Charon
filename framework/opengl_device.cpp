#include "headers/opengl_device.h"
#include "headers/opengl_crt.h"
#include "headers/opengl_lcd.h"
#include "third_party/xbrz/4xbrz.h"
#include "third_party/jinc2/jinc2.h"
#include "third_party/cas/cas.h"
#include "third_party/scalefx/scalefx.h"
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace CharonGL {
    Viewport FitViewport(Size canvas, Size output) {
        if (canvas.width <= 0 || canvas.height <= 0 || output.width <= 0 || output.height <= 0)
            return {};
        int width = output.width, height = output.height;
        if (std::int64_t(output.width) * canvas.height > std::int64_t(output.height) * canvas.width)
            width = int(std::int64_t(output.height) * canvas.width / canvas.height);
        else
            height = int(std::int64_t(output.width) * canvas.height / canvas.width);
        return {(output.width - width) / 2, (output.height - height) / 2, width, height};
    }

    POINT MapMouse(POINT point, Size canvas, Size output, bool curved) {
        const auto view = FitViewport(canvas, output);
        if (view.width <= 0 || view.height <= 0) return {};
        if (curved) {
            const double x = (double(point.x) - view.x) * 2.0 / view.width - 1.0;
            const double y = (double(point.y) - view.y) * 2.0 / view.height - 1.0;
            const double sourceX = (x * (1.0 + CrtCurvature * y * y) + 1.0) * 0.5 * canvas.width;
            const double sourceY = (y * (1.0 + CrtCurvature * x * x) + 1.0) * 0.5 * canvas.height;
            return {LONG((std::max)(0.0, (std::min)(sourceX, double(canvas.width - 1)))),
                LONG((std::max)(0.0, (std::min)(sourceY, double(canvas.height - 1))))};
        }
        const auto x = (std::int64_t(point.x) - view.x) * canvas.width / view.width;
        const auto y = (std::int64_t(point.y) - view.y) * canvas.height / view.height;
        return {LONG((std::max)(std::int64_t(0), (std::min)(x, std::int64_t(canvas.width - 1)))),
            LONG((std::max)(std::int64_t(0), (std::min)(y, std::int64_t(canvas.height - 1))))};
    }

    POINT MapCursor(POINT point, Size canvas, Size output, bool curved) {
        const auto view = FitViewport(canvas, output);
        if (view.width <= 0 || view.height <= 0) return {};
        if (!curved)
            return {view.x + LONG(std::int64_t(point.x) * view.width / canvas.width),
                view.y + LONG(std::int64_t(point.y) * view.height / canvas.height)};
        const double sourceX = (std::max)(LONG(0), (std::min)(point.x, LONG(canvas.width - 1))) * 2.0 / canvas.width - 1.0;
        const double sourceY = (std::max)(LONG(0), (std::min)(point.y, LONG(canvas.height - 1))) * 2.0 / canvas.height - 1.0;
        double x = sourceX, y = sourceY;
        for (int i = 0; i < 8; ++i) {
            const double nextX = sourceX / (1.0 + CrtCurvature * y * y);
            const double nextY = sourceY / (1.0 + CrtCurvature * x * x);
            x = nextX;
            y = nextY;
        }
        return {view.x + (std::min)(LONG(view.width - 1), LONG(std::round((x + 1.0) * 0.5 * view.width))),
            view.y + (std::min)(LONG(view.height - 1), LONG(std::round((y + 1.0) * 0.5 * view.height)))};
    }

    namespace Device {
        namespace {
            constexpr GLenum Framebuffer = 0x8d40, ColorAttachment = 0x8ce0, FramebufferComplete = 0x8cd5;
            constexpr GLenum ArrayBuffer = 0x8892, StreamDraw = 0x88e0, VertexShader = 0x8b31;
            constexpr GLenum FragmentShader = 0x8b30, CompileStatus = 0x8b81, LinkStatus = 0x8b82;
            constexpr GLenum R8ui = 0x8232, RedInteger = 0x8d94, Rgba8 = 0x8058, Rgba32f = 0x8814, ClampEdge = 0x812f;
            constexpr GLenum Texture0 = 0x84c0;
            constexpr Size LcdPanelSize{1024,768};

#define GL_FUNCTIONS(X) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, ()) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const char*)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const char*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, std::ptrdiff_t, const void*, GLenum)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))
#define DECLARE_GL(result, name, args) using name##Proc = result (APIENTRY*) args; name##Proc name = nullptr;
            GL_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL
            using SwapIntervalProc = BOOL(WINAPI*)(int);
            SwapIntervalProc swapInterval = nullptr;

            struct Target { GLuint framebuffer = 0, texture = 0; Size size{}; };
            HWND window = nullptr;
            HDC dc = nullptr, previousDc = nullptr;
            HGLRC context = nullptr, previousContext = nullptr;
            DWORD thread = 0;
            Size canvas{};
            Target scene, enlarged;
            struct ScaleFxState { GLuint programs[5]{}; Target targets[4]; } scalefx;
            struct LcdState {
                GLuint program = 0, outputProgram = 0;
                Target targets[2];
                bool ready = false, pending = false;
                unsigned readIndex = 0;
                double time = 0, pendingTime = 0;
                Size output{}, pendingOutput{};
                float gamma = 1, pendingGamma = 1;
            } lcd;
            GLuint drawProgram = 0, outputProgram = 0, xbrzProgram = 0, jinc2Program = 0, casProgram = 0, crtProgram = 0;
            struct DrawUniforms { GLint canvas, kind, chromakey, linearIndex, alpha; } drawUniforms{};
            Size drawCanvas{};
            std::array<std::uint32_t,256> uploadedPalette{};
            bool paletteValid = false;
#ifdef CHARON_OPENGL_TEST_CONTEXT
            bool failDestroyActivation = false;
#endif
#ifdef CHARON_OPENGL_TEST_CLOCK
            bool lcdTestClock = false;
            double lcdTestTime = 0;
#endif
            GLuint vertexArray = 0, vertexBuffer = 0, paletteTexture = 0;
            GLint maxTextureSize = 0;
            std::string error;
            std::mutex pendingMutex;
            std::vector<GLuint> pendingDeletes;

            const char* drawVertex = R"GLSL(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
uniform vec2 canvas;
out vec2 uv;
out vec4 tint;
void main() {
    gl_Position=vec4(position.x*2.0/canvas.x-1.0,1.0-position.y*2.0/canvas.y,0,1);
    uv=texcoord; tint=color;
})GLSL";
            const char* drawFragment = R"GLSL(#version 330 core
in vec2 uv;
in vec4 tint;
uniform usampler2D indices;
uniform sampler2D colors;
uniform sampler2D image;
uniform int kind;
uniform int chromakey;
uniform int linearIndex;
uniform float alpha;
out vec4 fragment;
vec4 indexed(ivec2 p) {
    p=clamp(p,ivec2(0),textureSize(indices,0)-1);
    uint i=texelFetch(indices,p,0).r;
    return texelFetch(colors,ivec2(int(i),0),0);
}
void main() {
    vec4 sampleColor=vec4(1);
    if(kind==1) {
        vec2 p=uv*vec2(textureSize(indices,0))-vec2(0.5);
        ivec2 base=ivec2(floor(p));
        if(linearIndex!=0) {
            vec2 f=fract(p);
            sampleColor=mix(mix(indexed(base),indexed(base+ivec2(1,0)),f.x),
                mix(indexed(base+ivec2(0,1)),indexed(base+ivec2(1,1)),f.x),f.y);
        } else sampleColor=indexed(ivec2(floor(p+vec2(0.5))));
    } else if(kind==2) sampleColor=texture(image,uv);
    if(kind!=0 && chromakey!=0 && all(equal(sampleColor.rgb,vec3(0)))) discard;
    fragment=vec4(sampleColor.rgb*tint.rgb,sampleColor.a*tint.a*alpha);
})GLSL";
            const char* outputVertex = R"GLSL(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texcoord;
out vec2 uv;
void main() {gl_Position=vec4(position,0,1); uv=texcoord;}
)GLSL";
            const char* outputFragment = R"GLSL(#version 330 core
in vec2 uv;
uniform sampler2D image;
uniform float gammaValue;
out vec4 fragment;
void main() {fragment=vec4(pow(max(texture(image,uv).rgb,vec3(0)),vec3(1.0/gammaValue)),1);}
)GLSL";

            bool ValidProc(PROC proc) {
                return proc && proc != reinterpret_cast<PROC>(1) && proc != reinterpret_cast<PROC>(2) &&
                    proc != reinterpret_cast<PROC>(3) && proc != reinterpret_cast<PROC>(-1);
            }
            bool Current() {
                if (!context || GetCurrentThreadId() != thread || wglGetCurrentContext() != context) {
                    Fail("OpenGL operation has no owning current context");
                    return false;
                }
                return true;
            }
            void InvalidateLcd() { lcd.ready = lcd.pending = false; }
            double LcdNow() {
#ifdef CHARON_OPENGL_TEST_CLOCK
                if (lcdTestClock) return lcdTestTime;
#endif
                LARGE_INTEGER counter{}, frequency{};
                if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
                    return std::numeric_limits<double>::quiet_NaN();
                return double(counter.QuadPart)/double(frequency.QuadPart);
            }
            bool Check(const char* operation) {
                GLenum code = glGetError();
                if (code == GL_NO_ERROR) return true;
                error = std::string(operation) + ": OpenGL error " + std::to_string(code);
                OutputDebugStringA(error.c_str());
                return false;
            }
            void DrainDeletes() {
                std::vector<GLuint> names;
                { std::lock_guard<std::mutex> lock(pendingMutex); names.swap(pendingDeletes); }
                if (!names.empty()) glDeleteTextures(GLsizei(names.size()), names.data());
            }
            GLuint Compile(GLenum type, const char* source) {
                GLuint shader = CreateShader(type);
                ShaderSource(shader, 1, &source, nullptr);
                CompileShader(shader);
                GLint good = 0;
                GetShaderiv(shader, CompileStatus, &good);
                if (good) return shader;
                char log[8192]{};
                GetShaderInfoLog(shader, sizeof(log), nullptr, log);
                Fail(log);
                DeleteShader(shader);
                return 0;
            }
            GLuint Program(const char* vertex, const char* fragment) {
                GLuint vs = Compile(VertexShader, vertex);
                if (!vs) return 0;
                GLuint fs = Compile(FragmentShader, fragment);
                if (!fs) { DeleteShader(vs); return 0; }
                GLuint program = CreateProgram();
                AttachShader(program, vs); AttachShader(program, fs);
                BindAttribLocation(program, 0, "VertexCoord");
                BindAttribLocation(program, 1, "TexCoord");
                BindAttribLocation(program, 2, "COLOR");
                LinkProgram(program);
                DeleteShader(vs); DeleteShader(fs);
                GLint good = 0;
                GetProgramiv(program, LinkStatus, &good);
                if (good) return program;
                char log[8192]{};
                GetProgramInfoLog(program, sizeof(log), nullptr, log);
                Fail(log); DeleteProgram(program); return 0;
            }
            void DisposeTarget(Target& target) {
                if (target.framebuffer) DeleteFramebuffers(1, &target.framebuffer);
                if (target.texture) glDeleteTextures(1, &target.texture);
                target = {};
            }
            bool AllocateTarget(Target& target, Size size, GLenum format = Rgba8) {
                if (size.width <= 0 || size.height <= 0 || size.width > maxTextureSize || size.height > maxTextureSize) {
                    Fail("Framebuffer size is outside driver limits"); return false;
                }
                Target fresh;
                fresh.size = size;
                glGenTextures(1, &fresh.texture);
                glBindTexture(GL_TEXTURE_2D, fresh.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ClampEdge);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ClampEdge);
                glTexImage2D(GL_TEXTURE_2D, 0, format, size.width, size.height, 0, GL_RGBA,
                    format == Rgba32f ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr);
                GenFramebuffers(1, &fresh.framebuffer);
                BindFramebuffer(Framebuffer, fresh.framebuffer);
                FramebufferTexture2D(Framebuffer, ColorAttachment, GL_TEXTURE_2D, fresh.texture, 0);
                if (CheckFramebufferStatus(Framebuffer) != FramebufferComplete || !Check("allocate framebuffer")) {
                    DisposeTarget(fresh); Fail("OpenGL framebuffer is incomplete"); return false;
                }
                DisposeTarget(target); target = fresh;
                glViewport(0, 0, size.width, size.height);
                glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
                return true;
            }
            void Vertices(const Vertex* data, std::size_t count) {
                BindVertexArray(vertexArray);
                BindBuffer(ArrayBuffer, vertexBuffer);
                BufferData(ArrayBuffer, std::ptrdiff_t(count * sizeof(Vertex)), data, StreamDraw);
            }
            void FullscreenQuad() {
                const Vertex quad[] = {{-1,-1,0,0,0xffffffff}, {-1,1,0,1,0xffffffff},
                    {1,1,1,1,0xffffffff}, {1,-1,1,0,0xffffffff}};
                Vertices(quad, 4);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }
            Size OutputSize() {
                RECT client{};
                GetClientRect(window, &client);
                return {int(client.right - client.left), int(client.bottom - client.top)};
            }
            bool ReadPixels(std::vector<std::uint8_t>& pixels, Size size) {
                if (size.width <= 0 || size.height <= 0) { Fail("Cannot read an empty framebuffer"); return false; }
                pixels.resize(std::size_t(size.width) * size.height * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (!Check("read framebuffer")) return false;
                const auto rowBytes = std::size_t(size.width) * 4;
                for (int row = 0; row < size.height / 2; ++row)
                    std::swap_ranges(pixels.begin() + row * rowBytes, pixels.begin() + (row + 1) * rowBytes,
                        pixels.begin() + (size.height - 1 - row) * rowBytes);
                return true;
            }
        }

        void Fail(const char* message) {
            error = message && *message ? message : "Unspecified OpenGL renderer error";
            OutputDebugStringA(error.c_str());
        }
        const char* LastError() { return error.c_str(); }
        bool Ready() { return context && scene.framebuffer; }
        Size CanvasSize() { return canvas; }

        bool Create(HWND hwnd, Size size) {
            if (context) { Fail("OpenGL context already exists"); return false; }
            error.clear();
            previousDc = wglGetCurrentDC(); previousContext = wglGetCurrentContext();
            window = hwnd; thread = GetCurrentThreadId(); dc = GetDC(hwnd);
            if (!dc) { Fail("GetDC failed"); return false; }
            PIXELFORMATDESCRIPTOR format{};
            format.nSize = sizeof(format); format.nVersion = 1;
            format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32;
            int selected = GetPixelFormat(dc);
            if (!selected) {
                selected = ChoosePixelFormat(dc, &format);
                if (!selected || !SetPixelFormat(dc, selected, &format)) {
                    Fail("Unable to set an OpenGL window pixel format"); Destroy(); return false;
                }
            } else {
                DescribePixelFormat(dc, selected, sizeof(format), &format);
                if (!(format.dwFlags & PFD_SUPPORT_OPENGL) || !(format.dwFlags & PFD_DOUBLEBUFFER)) {
                    Fail("Existing window pixel format does not support double-buffered OpenGL"); Destroy(); return false;
                }
            }
            HGLRC bootstrap = wglCreateContext(dc);
            if (!bootstrap || !wglMakeCurrent(dc, bootstrap)) {
                if (bootstrap) wglDeleteContext(bootstrap);
                Fail("Unable to create the WGL bootstrap context"); Destroy(); return false;
            }
            using CreateContext = HGLRC(WINAPI*)(HDC,HGLRC,const int*);
            PROC create = wglGetProcAddress("wglCreateContextAttribsARB");
            const int attributes[] = {0x2091,3,0x2092,3,0x9126,1,0};
            if (ValidProc(create)) context = reinterpret_cast<CreateContext>(create)(dc, nullptr, attributes);
            wglMakeCurrent(nullptr, nullptr); wglDeleteContext(bootstrap);
            if (!context || !wglMakeCurrent(dc, context)) {
                Fail("OpenGL 3.3 core context creation failed"); Destroy(); return false;
            }
#define LOAD_GL(result, name, args) \
            name = reinterpret_cast<name##Proc>(wglGetProcAddress("gl" #name)); \
            if (!ValidProc(reinterpret_cast<PROC>(name))) { Fail("Missing gl" #name); Destroy(); return false; }
            GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
            swapInterval = reinterpret_cast<SwapIntervalProc>(wglGetProcAddress("wglSwapIntervalEXT"));
            if (!ValidProc(reinterpret_cast<PROC>(swapInterval))) swapInterval = nullptr;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
            drawProgram = Program(drawVertex, drawFragment);
            outputProgram = Program(outputVertex, outputFragment);
            const std::string xbrzVertex = std::string("#version 330 core\n#define VERTEX\n") + XbrzShader;
            const std::string xbrzFragment = std::string("#version 330 core\n#define FRAGMENT\n") + XbrzShader;
            xbrzProgram = Program(xbrzVertex.c_str(), xbrzFragment.c_str());
            jinc2Program = Program(outputVertex,Jinc2Shader);
            casProgram = Program(outputVertex,CasShader);
            crtProgram = Program(outputVertex,CrtShader);
            lcd.program = Program(outputVertex,LcdResponseShader);
            lcd.outputProgram = Program(outputVertex,LcdOutputShader);
            if (!drawProgram || !outputProgram || !xbrzProgram || !jinc2Program || !casProgram || !crtProgram || !lcd.program || !lcd.outputProgram) { Destroy(); return false; }
            for (unsigned pass = 0; pass < 5; ++pass) {
                const std::string vertex = std::string("#version 330 core\n#define VERTEX\n") + ScaleFxShaders[pass];
                const std::string fragment = std::string("#version 330 core\n#define FRAGMENT\n") + ScaleFxShaders[pass];
                scalefx.programs[pass] = Program(vertex.c_str(),fragment.c_str());
                if (!scalefx.programs[pass]) { Destroy(); return false; }
            }
            UseProgram(drawProgram);
            drawUniforms = {GetUniformLocation(drawProgram,"canvas"), GetUniformLocation(drawProgram,"kind"),
                GetUniformLocation(drawProgram,"chromakey"), GetUniformLocation(drawProgram,"linearIndex"),
                GetUniformLocation(drawProgram,"alpha")};
            Uniform1i(GetUniformLocation(drawProgram,"indices"),0);
            Uniform1i(GetUniformLocation(drawProgram,"colors"),1);
            Uniform1i(GetUniformLocation(drawProgram,"image"),2);
            GenVertexArrays(1, &vertexArray); BindVertexArray(vertexArray);
            GenBuffers(1, &vertexBuffer); BindBuffer(ArrayBuffer, vertexBuffer);
            EnableVertexAttribArray(0); EnableVertexAttribArray(1); EnableVertexAttribArray(2);
            VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
            VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex,u)));
            VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex,color)));
            glGenTextures(1, &paletteTexture); glBindTexture(GL_TEXTURE_2D, paletteTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D,0,Rgba8,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DITHER);
            if (!ResizeCanvas(size) || !Check("initialize OpenGL device")) { Destroy(); return false; }
            SetVsync(true);
            return true;
        }

        void Destroy() {
            if (context && GetCurrentThreadId() != thread) { Fail("OpenGL context destroyed from another thread"); return; }
            bool activated = false;
            if (context) {
#ifdef CHARON_OPENGL_TEST_CONTEXT
                if (!failDestroyActivation)
#endif
                    activated = wglMakeCurrent(dc, context) != FALSE;
            }
            if (activated) {
                DrainDeletes();
                if (DeleteFramebuffers) {
                    DisposeTarget(scene); DisposeTarget(enlarged);
                    for (auto& target : scalefx.targets) DisposeTarget(target);
                    for (auto& target : lcd.targets) DisposeTarget(target);
                }
                if (DeleteProgram) {
                    if (drawProgram) DeleteProgram(drawProgram);
                    if (outputProgram) DeleteProgram(outputProgram);
                    if (xbrzProgram) DeleteProgram(xbrzProgram);
                    if (jinc2Program) DeleteProgram(jinc2Program);
                    if (casProgram) DeleteProgram(casProgram);
                    if (crtProgram) DeleteProgram(crtProgram);
                    if (lcd.program) DeleteProgram(lcd.program);
                    if (lcd.outputProgram) DeleteProgram(lcd.outputProgram);
                    for (GLuint program : scalefx.programs) if (program) DeleteProgram(program);
                }
                if (vertexBuffer && DeleteBuffers) DeleteBuffers(1, &vertexBuffer);
                if (vertexArray && DeleteVertexArrays) DeleteVertexArrays(1, &vertexArray);
                if (paletteTexture) glDeleteTextures(1, &paletteTexture);
            }
            if (context) {
                if (wglGetCurrentContext() == context && !wglMakeCurrent(nullptr, nullptr)) {
                    Fail("Unable to detach OpenGL context during destruction"); return;
                }
                // Context deletion releases its objects even when activation failed.
                if (!wglDeleteContext(context)) {
                    Fail("Unable to delete OpenGL context during destruction"); return;
                }
                context = nullptr;
            }
            if (previousContext && !wglMakeCurrent(previousDc, previousContext))
                Fail("Unable to restore previous OpenGL context");
            if (dc && window) ReleaseDC(window, dc);
            dc = nullptr; window = nullptr; previousContext = nullptr; previousDc = nullptr;
            canvas = {}; scene = {}; enlarged = {}; scalefx = {}; lcd = {};
            drawCanvas = {}; paletteValid = false;
            drawProgram = outputProgram = xbrzProgram = jinc2Program = casProgram = crtProgram = vertexArray = vertexBuffer = paletteTexture = 0;
            { std::lock_guard<std::mutex> lock(pendingMutex); pendingDeletes.clear(); }
        }

        bool ResizeCanvas(Size size) {
            if (!Current()) return false;
            if (size.width == canvas.width && size.height == canvas.height && scene.framebuffer) return BeginFrame();
            if (!AllocateTarget(scene, size)) return false;
            DisposeTarget(enlarged); canvas = size;
            for (auto& target : scalefx.targets) DisposeTarget(target);
            for (auto& target : lcd.targets) DisposeTarget(target);
            InvalidateLcd();
            return BeginFrame();
        }
        Image UploadIndices(const std::uint8_t* pixels, Size size) {
            if (!Current() || !pixels || size.width <= 0 || size.height <= 0 || size.width > maxTextureSize || size.height > maxTextureSize) {
                Fail("Invalid indexed texture upload"); return {};
            }
            Image image; image.width = size.width; image.height = size.height;
            glGenTextures(1, &image.name); glBindTexture(GL_TEXTURE_2D, image.name);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,ClampEdge);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,ClampEdge);
            glTexImage2D(GL_TEXTURE_2D,0,R8ui,size.width,size.height,0,RedInteger,GL_UNSIGNED_BYTE,pixels);
            if (!Check("upload indexed texture")) { glDeleteTextures(1,&image.name); return {}; }
            return image;
        }
        Image UploadRgba(const std::uint32_t* pixels, Size size) {
            if (!Current() || !pixels || size.width <= 0 || size.height <= 0 || size.width > maxTextureSize || size.height > maxTextureSize) {
                Fail("Invalid RGBA texture upload"); return {};
            }
            Image image; image.width = size.width; image.height = size.height; image.indexed = false;
            glGenTextures(1,&image.name); glBindTexture(GL_TEXTURE_2D,image.name);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,ClampEdge);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,ClampEdge);
            glTexImage2D(GL_TEXTURE_2D,0,Rgba8,size.width,size.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            if (!Check("upload RGBA texture")) { glDeleteTextures(1,&image.name); return {}; }
            return image;
        }
        void DeleteImage(Image image) {
            if (!image.name) return;
            if (GetCurrentThreadId() == thread && context && wglGetCurrentContext() == context)
                glDeleteTextures(1,&image.name);
            else { std::lock_guard<std::mutex> lock(pendingMutex); pendingDeletes.push_back(image.name); }
        }
        bool BeginFrame() {
            if (!Current()) return false;
            lcd.pending = false;
            DrainDeletes(); BindFramebuffer(Framebuffer,scene.framebuffer);
            glViewport(0,0,canvas.width,canvas.height); glDisable(GL_SCISSOR_TEST);
            return Check("begin frame");
        }
        void Clear(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            if (!BeginFrame()) return;
            glClearColor(red/255.0f,green/255.0f,blue/255.0f,1); glClear(GL_COLOR_BUFFER_BIT);
        }
        bool Draw(Primitive primitive, const Vertex* vertices, std::size_t count, Image image,
            const std::uint32_t* palette, Blend blend, std::uint8_t alpha, bool colorKey, bool linearSampling) {
            if (!Current()) return false;
            if (!vertices || !count || count > std::size_t((std::numeric_limits<GLsizei>::max)()) ||
                count > std::size_t((std::numeric_limits<std::ptrdiff_t>::max)())/sizeof(Vertex) ||
                (image.name && image.indexed && !palette)) { Fail("Invalid native draw submission"); return false; }
            BindFramebuffer(Framebuffer,scene.framebuffer); glViewport(0,0,canvas.width,canvas.height);
            UseProgram(drawProgram);
            if (drawCanvas.width != canvas.width || drawCanvas.height != canvas.height) {
                Uniform2f(drawUniforms.canvas,float(canvas.width),float(canvas.height));
                drawCanvas = canvas;
            }
            Uniform1i(drawUniforms.kind,image.name ? (image.indexed ? 1 : 2) : 0);
            Uniform1i(drawUniforms.chromakey,colorKey ? 1 : 0);
            Uniform1i(drawUniforms.linearIndex,linearSampling ? 1 : 0);
            Uniform1f(drawUniforms.alpha,alpha/255.0f);
            ActiveTexture(Texture0+(image.indexed ? 0 : 2)); glBindTexture(GL_TEXTURE_2D,image.name);
            if (image.name && !image.indexed) {
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linearSampling ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linearSampling ? GL_LINEAR : GL_NEAREST);
            }
            if (image.name && image.indexed) {
                ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,paletteTexture);
                if (!paletteValid || std::memcmp(uploadedPalette.data(),palette,sizeof(uploadedPalette)) != 0) {
                    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,1,GL_RGBA,GL_UNSIGNED_BYTE,palette);
                    if (!Check("upload palette")) { paletteValid = false; return false; }
                    std::memcpy(uploadedPalette.data(),palette,sizeof(uploadedPalette));
                    paletteValid = true;
                }
            }
            if (blend == Blend::Replace) glDisable(GL_BLEND);
            else {
                glEnable(GL_BLEND);
                if (blend == Blend::Alpha) BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
                else if (blend == Blend::Add) BlendFuncSeparate(GL_ONE,GL_ONE,GL_ZERO,GL_ONE);
                else BlendFuncSeparate(GL_ZERO,GL_SRC_COLOR,GL_ZERO,GL_ONE);
            }
            GLenum topology = GL_TRIANGLES;
            switch (primitive) {
            case Primitive::TriangleStrip: topology=GL_TRIANGLE_STRIP; break;
            case Primitive::TriangleFan: topology=GL_TRIANGLE_FAN; break;
            case Primitive::Lines: topology=GL_LINES; break;
            case Primitive::LineStrip: topology=GL_LINE_STRIP; break;
            case Primitive::Points: topology=GL_POINTS; break;
            default: break;
            }
            Vertices(vertices,count); glDrawArrays(topology,0,GLsizei(count));
            return Check("draw");
        }
        bool Finish(Filter filter, float gamma) {
            if (!Current()) return false;
            lcd.pending = false;
            if (filter != Filter::Lcd) InvalidateLcd();
            if (!std::isfinite(gamma) || gamma <= 0) { InvalidateLcd(); Fail("Invalid display gamma"); return false; }
            Size output = OutputSize();
            if (output.width <= 0 || output.height <= 0) { InvalidateLcd(); return true; }
            glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
            BindFramebuffer(Framebuffer,0); glViewport(0,0,output.width,output.height);
            glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
            const auto view = FitViewport(canvas,output);
            if (view.width <= 0 || view.height <= 0) { InvalidateLcd(); return Check("finish empty viewport"); }
            GLuint source = scene.texture;
            ActiveTexture(Texture0);
            if (filter == Filter::Lcd) {
                for (auto& target : lcd.targets) {
                    if (target.size.width != LcdPanelSize.width || target.size.height != LcdPanelSize.height)
                        if (!AllocateTarget(target,LcdPanelSize,Rgba32f)) { InvalidateLcd(); return false; }
                }
                const double now = LcdNow(), elapsed = now-lcd.time;
                const bool reset = !lcd.ready || lcd.output.width != output.width || lcd.output.height != output.height ||
                    lcd.gamma != gamma || !std::isfinite(elapsed) || elapsed < 0 || elapsed > 0.25;
                const auto& target = lcd.targets[lcd.readIndex^1];
                BindFramebuffer(Framebuffer,target.framebuffer); glViewport(0,0,LcdPanelSize.width,LcdPanelSize.height);
                UseProgram(lcd.program);
                glBindTexture(GL_TEXTURE_2D,scene.texture);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                Uniform1i(GetUniformLocation(lcd.program,"image"),0);
                Uniform1i(GetUniformLocation(lcd.program,"history"),1);
                Uniform1i(GetUniformLocation(lcd.program,"resetHistory"),reset ? 1 : 0);
                Uniform1f(GetUniformLocation(lcd.program,"gammaValue"),gamma);
                Uniform1f(GetUniformLocation(lcd.program,"frameSeconds"),reset ? 0.0f : float(elapsed));
                // A fixed virtual 60 Hz FRC cycle, independent of the real monitor and game FPS.
                double phase = std::isfinite(now) ? std::fmod(now,4.0/60.0)*60.0 : 0.0;
                if (phase < 0) phase += 4.0;
                Uniform1f(GetUniformLocation(lcd.program,"frcPhaseEnd"),float(phase));
                ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,lcd.targets[lcd.readIndex].texture);
                ActiveTexture(Texture0); FullscreenQuad();
                ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,0); ActiveTexture(Texture0);
                source = target.texture;
                lcd.pendingTime = now; lcd.pendingOutput = output; lcd.pendingGamma = gamma;
            } else if (filter == Filter::Xbrz) {
                if (canvas.width > maxTextureSize/4 || canvas.height > maxTextureSize/4) { Fail("Upscaling target exceeds driver limits"); return false; }
                const Size enlargedSize{canvas.width*4,canvas.height*4};
                if (enlarged.size.width != enlargedSize.width || enlarged.size.height != enlargedSize.height)
                    if (!AllocateTarget(enlarged,enlargedSize)) return false;
                BindFramebuffer(Framebuffer,enlarged.framebuffer); glViewport(0,0,enlargedSize.width,enlargedSize.height);
                glBindTexture(GL_TEXTURE_2D,scene.texture);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                UseProgram(xbrzProgram);
                Uniform1i(GetUniformLocation(xbrzProgram,"Texture"),0);
                Uniform2f(GetUniformLocation(xbrzProgram,"InputSize"),float(canvas.width),float(canvas.height));
                Uniform2f(GetUniformLocation(xbrzProgram,"TextureSize"),float(canvas.width),float(canvas.height));
                Uniform2f(GetUniformLocation(xbrzProgram,"OutputSize"),float(enlargedSize.width),float(enlargedSize.height));
                const GLfloat identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                UniformMatrix4fv(GetUniformLocation(xbrzProgram,"MVPMatrix"),1,GL_FALSE,identity);
                FullscreenQuad(); source = enlarged.texture;
            } else if (filter == Filter::Scalefx) {
                if (canvas.width > maxTextureSize/3 || canvas.height > maxTextureSize/3) { Fail("ScaleFX target exceeds driver limits"); return false; }
                const Size enlargedSize{canvas.width*3,canvas.height*3};
                for (unsigned pass = 0; pass < 4; ++pass) {
                    auto& target = scalefx.targets[pass];
                    if (target.size.width != canvas.width || target.size.height != canvas.height)
                        if (!AllocateTarget(target,canvas,pass < 2 ? Rgba32f : Rgba8)) return false;
                }
                if (enlarged.size.width != enlargedSize.width || enlarged.size.height != enlargedSize.height)
                    if (!AllocateTarget(enlarged,enlargedSize)) return false;
                const GLfloat identity[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                for (unsigned pass = 0; pass < 5; ++pass) {
                    const auto& target = pass < 4 ? scalefx.targets[pass] : enlarged;
                    BindFramebuffer(Framebuffer,target.framebuffer); glViewport(0,0,target.size.width,target.size.height);
                    const GLuint program = scalefx.programs[pass];
                    UseProgram(program);
                    ActiveTexture(Texture0); glBindTexture(GL_TEXTURE_2D,pass ? scalefx.targets[pass-1].texture : scene.texture);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                    Uniform1i(GetUniformLocation(program,"Texture"),0);
                    if (pass == 2 || pass == 4) {
                        // Edge classification reuses pass 0; reconstruction samples the original scene.
                        ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,pass == 2 ? scalefx.targets[0].texture : scene.texture);
                        Uniform1i(GetUniformLocation(program,pass == 2 ? "PassPrev2Texture" : "PassPrev5Texture"),1);
                        ActiveTexture(Texture0);
                    }
                    Uniform2f(GetUniformLocation(program,"InputSize"),float(canvas.width),float(canvas.height));
                    Uniform2f(GetUniformLocation(program,"TextureSize"),float(canvas.width),float(canvas.height));
                    Uniform2f(GetUniformLocation(program,"OutputSize"),float(target.size.width),float(target.size.height));
                    Uniform2f(GetUniformLocation(program,"PassPrev2TextureSize"),float(canvas.width),float(canvas.height));
                    Uniform2f(GetUniformLocation(program,"PassPrev2InputSize"),float(canvas.width),float(canvas.height));
                    Uniform2f(GetUniformLocation(program,"PassPrev5TextureSize"),float(canvas.width),float(canvas.height));
                    Uniform2f(GetUniformLocation(program,"PassPrev5InputSize"),float(canvas.width),float(canvas.height));
                    UniformMatrix4fv(GetUniformLocation(program,"MVPMatrix"),1,GL_FALSE,identity);
                    FullscreenQuad();
                }
                // The next frame draws into the scene again; do not leave it bound as an input.
                ActiveTexture(Texture0+1); glBindTexture(GL_TEXTURE_2D,0); ActiveTexture(Texture0);
                source = enlarged.texture;
            } else if (filter == Filter::Cas) {
                const Size scaledSize{view.width,view.height};
                if (enlarged.size.width != scaledSize.width || enlarged.size.height != scaledSize.height)
                    if (!AllocateTarget(enlarged,scaledSize)) return false;
                BindFramebuffer(Framebuffer,enlarged.framebuffer); glViewport(0,0,view.width,view.height);
                UseProgram(outputProgram);
                Uniform1i(GetUniformLocation(outputProgram,"image"),0);
                Uniform1f(GetUniformLocation(outputProgram,"gammaValue"),1.0f);
                glBindTexture(GL_TEXTURE_2D,scene.texture);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                FullscreenQuad(); source = enlarged.texture;
            }
            BindFramebuffer(Framebuffer,0);
            // FitViewport is top-left based; OpenGL's viewport is bottom-left based.
            glViewport(view.x,output.height-view.y-view.height,view.width,view.height);
            const bool crt = filter == Filter::Crt || filter == Filter::CrtCurved;
            const GLuint program = filter == Filter::Jinc2 ? jinc2Program : filter == Filter::Cas ? casProgram :
                crt ? crtProgram : filter == Filter::Lcd ? lcd.outputProgram : outputProgram;
            UseProgram(program);
            Uniform1i(GetUniformLocation(program,"image"),0);
            Uniform1f(GetUniformLocation(program,"gammaValue"),gamma);
            if (crt || filter == Filter::Lcd) {
                Uniform2f(GetUniformLocation(program,"outputSize"),float(view.width),float(view.height));
                Uniform2f(GetUniformLocation(program,"outputOrigin"),float(view.x),float(output.height-view.y-view.height));
                if (crt) Uniform1f(GetUniformLocation(program,"curvature"),filter == Filter::CrtCurved ? CrtCurvature : 0.0f);
            }
            glBindTexture(GL_TEXTURE_2D,source);
            const GLint sampling = filter == Filter::Xbrz || filter == Filter::Scalefx || filter == Filter::Lcd ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,sampling);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,sampling);
            FullscreenQuad();
            const bool result = Check("finish frame");
            if (!result) InvalidateLcd();
            else lcd.pending = filter == Filter::Lcd;
            return result;
        }
        bool Present() {
            if (!Current()) return false;
            if (!SwapBuffers(dc)) { InvalidateLcd(); Fail("SwapBuffers failed"); return false; }
            if (lcd.pending) {
                // Unpresented Finish calls must not become temporal history.
                lcd.readIndex ^= 1; lcd.time = lcd.pendingTime;
                lcd.output = lcd.pendingOutput; lcd.gamma = lcd.pendingGamma;
                lcd.ready = true; lcd.pending = false;
            }
            return true;
        }
        bool ReadCanvas(std::vector<std::uint8_t>& rgba) {
            if (!Current()) return false;
            GLint previous = 0; glGetIntegerv(0x8ca6,&previous);
            BindFramebuffer(Framebuffer,scene.framebuffer);
            bool good = ReadPixels(rgba,canvas);
            BindFramebuffer(Framebuffer,GLuint(previous)); return good;
        }
        bool ReadOutput(std::vector<std::uint8_t>& rgba, Size& size) {
            if (!Current()) return false;
            size=OutputSize(); GLint previous=0; glGetIntegerv(0x8ca6,&previous);
            BindFramebuffer(Framebuffer,0); glReadBuffer(GL_BACK);
            bool good=ReadPixels(rgba,size);
            BindFramebuffer(Framebuffer,GLuint(previous)); return good;
        }
        void SetVsync(bool enabled) {
            if (context && swapInterval) swapInterval(enabled ? 1 : 0);
        }
#ifdef CHARON_OPENGL_TEST_CONTEXT
        void FailDestroyActivationForTests(bool fail) { failDestroyActivation = fail; }
#endif
#ifdef CHARON_OPENGL_TEST_CLOCK
        void SetLcdTimeForTests(double seconds) { lcdTestClock = true; lcdTestTime = seconds; }
#endif
    }
}
