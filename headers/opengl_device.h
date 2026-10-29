#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CharonGL {
    struct Size { int width, height; };
    struct Viewport { int x, y, width, height; };
    enum class Filter { Nearest, Cas, Jinc2, Xbrz, Scalefx, Crt, CrtCurved, Lcd, Count };
    constexpr Filter MigrateLegacyFilter(unsigned value, unsigned filterVersion = 0) {
        constexpr Filter original[] = {Filter::Nearest, Filter::Cas, Filter::Xbrz,
            Filter::Jinc2, Filter::Cas, Filter::Crt, Filter::CrtCurved};
        // The removed last slot in version 1 continues to fall back to Nearest.
        constexpr Filter previous[] = {Filter::Nearest, Filter::Cas, Filter::Xbrz,
            Filter::Jinc2, Filter::Crt, Filter::CrtCurved, Filter::Nearest};
        return filterVersion == 0 ? original[value % 7] : previous[value % 7];
    }
    inline constexpr float CrtCurvature = 0.05f;
    enum class Blend { Replace, Alpha, Add, Multiply };
    enum class Primitive { Triangles, TriangleStrip, TriangleFan, Lines, LineStrip, Points };

    // RGBA is packed in byte order for GL_UNSIGNED_BYTE vertex/texture uploads.
    constexpr std::uint32_t Rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
        return (r & 255) | ((g & 255) << 8) | ((b & 255) << 16) | ((a & 255) << 24);
    }
    struct Vertex {
        float x, y, u, v;
        std::uint32_t color;
    };
    struct Image {
        unsigned name = 0;
        int width = 0, height = 0;
        bool indexed = true;
    };

    Viewport FitViewport(Size canvas, Size output);
    POINT MapMouse(POINT client, Size canvas, Size output, bool curved = false);
    POINT MapCursor(POINT canvasPoint, Size canvas, Size output, bool curved = false);

    namespace Device {
        bool Create(HWND window, Size canvas);
        void Destroy();
        bool Ready();
        Size CanvasSize();
        bool ResizeCanvas(Size canvas);
        const char* LastError();
        void Fail(const char* message);
        Image UploadIndices(const std::uint8_t* pixels, Size size);
        Image UploadRgba(const std::uint32_t* pixels, Size size);
        void DeleteImage(Image image);
        bool BeginFrame();
        void Clear(std::uint8_t red = 0, std::uint8_t green = 0, std::uint8_t blue = 0);
        bool Draw(Primitive primitive, const Vertex* vertices, std::size_t count,
            Image image, const std::uint32_t* palette, Blend blend,
            std::uint8_t alpha = 255, bool colorKey = true, bool linearSampling = false);
        // Finish draws to the window's back buffer; Present is the sole swap.
        bool Finish(Filter filter, float gamma = 1.0f);
        bool Present();
        bool ReadCanvas(std::vector<std::uint8_t>& rgba);
        bool ReadOutput(std::vector<std::uint8_t>& rgba, Size& size);
        void SetVsync(bool enabled);
#ifdef CHARON_OPENGL_TEST_CONTEXT
        void FailDestroyActivationForTests(bool fail);
#endif
#ifdef CHARON_OPENGL_TEST_CLOCK
        void SetLcdTimeForTests(double seconds);
#endif
    }
}
