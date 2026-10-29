// Synthetic GPU properties, not original-game rendering equivalence.
#define NOMINMAX
#include "headers/opengl_device.h"
#include <GL/gl.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <random>
#include <limits>
#include <string>
#include <thread>

#ifdef NDEBUG
#error Build GPU checks with assertions enabled.
#endif

using namespace CharonGL;

static void Require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "%s: %s\n", operation, Device::LastError());
        std::exit(1);
    }
}
static bool Pixel(const std::vector<std::uint8_t>& pixels, Size size, int x, int y,
    int r, int g, int b, int tolerance = 0) {
    const auto at = (std::size_t(y) * size.width + x) * 4;
    return std::abs(int(pixels[at])-r) <= tolerance &&
        std::abs(int(pixels[at+1])-g) <= tolerance && std::abs(int(pixels[at+2])-b) <= tolerance;
}
static void Quad(Image image, const std::uint32_t* palette, std::uint32_t color,
    Blend blend = Blend::Replace, std::uint8_t alpha = 255, bool key = true, bool linear = false) {
    const auto size = Device::CanvasSize();
    const Vertex vertices[] = {{0,0,0,0,color}, {0,float(size.height),0,1,color},
        {float(size.width),float(size.height),1,1,color}, {float(size.width),0,1,0,color}};
    Require(Device::Draw(Primitive::TriangleFan,vertices,4,image,palette,blend,alpha,key,linear),"draw quad");
}

static double Channel(const std::vector<std::uint8_t>& pixels, Size size, int x, int y, int channel) {
    x=std::clamp(x,0,size.width-1); y=std::clamp(y,0,size.height-1);
    return pixels[(std::size_t(y)*size.width+x)*4+channel]/255.0;
}
static std::uint8_t Quantize(double value) {
    return std::uint8_t(std::lround(std::clamp(value,0.0,1.0)*255));
}
// Capture hardware RGBA8 bilinear quantization independently of the CAS shader.
static std::vector<std::uint8_t> CaptureBilinear(Size output, float gamma=1.0f) {
    const Size source=Device::CanvasSize();
    std::vector<std::uint8_t> original,scaled;
    Require(Device::ReadCanvas(original),"snapshot bilinear source canvas");
    std::vector<std::uint32_t> packed(original.size()/4);
    // Preserve the scene texture's bottom-up lattice and fullscreen triangle diagonal.
    for (int y=0;y<source.height;++y) for (int x=0;x<source.width;++x) {
        const auto at=(std::size_t(source.height-1-y)*source.width+x)*4;
        packed[std::size_t(y)*source.width+x]=Rgba(original[at],original[at+1],original[at+2],original[at+3]);
    }
    const Image image=Device::UploadRgba(packed.data(),source);
    Require(image.name!=0,"upload bilinear source snapshot");
    const auto drawSnapshot=[&](bool linear) {
        const auto size=Device::CanvasSize();
        const auto white=Rgba(255,255,255);
        const Vertex vertices[]={{0,float(size.height),0,0,white},{0,0,0,1,white},
            {float(size.width),0,1,1,white},{float(size.width),float(size.height),1,0,white}};
        Require(Device::Draw(Primitive::TriangleFan,vertices,4,image,nullptr,Blend::Replace,255,false,linear),
            "draw bilinear source snapshot");
    };
    const auto view=FitViewport(source,output);
    Require(Device::ResizeCanvas({view.width,view.height}),"resize bilinear reference target");
    drawSnapshot(true);
    Require(Device::ReadCanvas(scaled),"read bilinear reference target");
    Require(Device::ResizeCanvas(source),"restore bilinear source canvas size");
    drawSnapshot(false);
    Device::DeleteImage(image);
    std::vector<std::uint8_t> result(std::size_t(output.width)*output.height*4,0);
    for (std::size_t i=3;i<result.size();i+=4) result[i]=255;
    for (int y=0;y<view.height;++y) for (int x=0;x<view.width;++x) for (int c=0;c<3;++c) {
        const auto value=scaled[(std::size_t(y)*view.width+x)*4+c];
        result[(std::size_t(y+view.y)*output.width+x+view.x)*4+c]=gamma==1.0f
            ? value : Quantize(std::pow(value/255.0,1.0/gamma));
    }
    return result;
}
static bool PixelsClose(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size()!=b.size()) return false;
    for (std::size_t i=0;i<a.size();++i) if (std::abs(int(a[i])-int(b[i]))>1) return false;
    return true;
}
static double JincReference(const std::vector<std::uint8_t>& source, Size size, double x, double y, int channel) {
    // Model the reference shader's bottom-up texture lattice, including half-texel ties.
    y=size.height-y;
    const auto sample=[&](int px,int py) { return Channel(source,size,px,size.height-1-py,channel); };
    const auto latticeFloor=[](double value) {
        const double lattice=value-0.5,nearest=std::round(lattice);
        return int(std::floor(std::abs(lattice-nearest)<0.0001 ? nearest : lattice));
    };
    const int baseX=latticeFloor(x),baseY=latticeFloor(y);
    const double pi=3.14159265358979323846,wa=pi*0.44,wb=pi*0.82;
    double sum=0,weightSum=0;
    for (int row=-1;row<=2;++row) for (int col=-1;col<=2;++col) {
        const double distance=std::hypot(x-(baseX+col+0.5),y-(baseY+row+0.5));
        const double weight=distance==0 ? wa*wb : std::sin(distance*wa)*std::sin(distance*wb)/(distance*distance);
        sum+=weight*sample(baseX+col,baseY+row); weightSum+=weight;
    }
    double low=1,high=0;
    for (int row=0;row<2;++row) for (int col=0;col<2;++col) {
        const double value=sample(baseX+col,baseY+row);
        low=std::min(low,value); high=std::max(high,value);
    }
    const double value=sum/weightSum;
    return 0.2*value+0.8*std::clamp(value,low,high);
}
static double CasReference(const std::vector<std::uint8_t>& scaled, Size output, Viewport view, int x, int y, int channel) {
    const auto sample=[&](int px,int py) {
        const double value=Channel(scaled,output,view.x+std::clamp(px,0,view.width-1),view.y+std::clamp(py,0,view.height-1),channel);
        return value*value;
    };
    const double b=sample(x,y-1),d=sample(x-1,y),e=sample(x,y),f=sample(x+1,y),h=sample(x,y+1);
    const double low=std::min({b,d,e,f,h}),high=std::max({b,d,e,f,h});
    const double amplitude=std::sqrt(std::clamp(std::min(low,1-high)/std::max(high,1e-6),0.0,1.0));
    const double weight=-amplitude/7.4;
    return std::sqrt(std::clamp((e+weight*(b+d+f+h))/(1+4*weight),0.0,1.0));
}
static void CheckReconstructionFilters(HWND window) {
    std::vector<std::uint8_t> darkPeak(3*3*4,64);
    darkPeak[16]=80;
    // Linear-light CAS maps this dark peak to 90; sharpening nonlinear RGB yields 95.
    assert(Quantize(CasReference(darkPeak,{3,3},{0,0,3,3},1,1,0))==90);
    std::mt19937 random(0x4a494e43);
    unsigned cases=0,jincChanges=0,casChanges=0;
    for (Size size : {Size{1,1},Size{1,7},Size{7,1},Size{7,9},Size{8,8},Size{31,23}}) {
        const Size target{size.width*2+5,size.height*2+3};
        assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
        Require(Device::ResizeCanvas(size),"resize reconstruction fixture");
        for (unsigned sample=0;sample<8;++sample) {
            std::vector<std::uint32_t> source(std::size_t(size.width)*size.height);
            for (int y=0;y<size.height;++y) for (int x=0;x<size.width;++x) {
                std::uint32_t color=0;
                switch(sample) {
                case 0: color=Rgba(0,0,0); break;
                case 1: color=Rgba(0,128,192); break;
                case 2: color=(x==size.width/2 && y==size.height/2) ? Rgba(192,160,128) : Rgba(64,80,96); break;
                case 3: color=(x==y) ? Rgba(200,180,150) : Rgba(40,70,90); break;
                case 4: color=(x+y)%2 ? Rgba(160,140,120) : Rgba(80,60,40); break;
                case 5: color=Rgba(unsigned(x*255/size.width),unsigned(y*255/size.height),128); break;
                default: color=Rgba(random()%256,random()%256,random()%256); break;
                }
                source[std::size_t(y)*size.width+x]=color;
            }
            std::vector<std::uint8_t> input(source.size()*4);
            for (std::size_t i=0;i<source.size();++i) for (unsigned c=0;c<4;++c) input[i*4+c]=std::uint8_t(source[i]>>(8*c));
            const Image image=Device::UploadRgba(source.data(),size); Require(image.name!=0,"upload reconstruction fixture");
            Quad(image,nullptr,Rgba(255,255,255),Blend::Replace,255,false);
            Size output=target;
            const auto baseline=CaptureBilinear(output);
            const auto view=FitViewport(size,output);
            for (Filter filter : {Filter::Jinc2,Filter::Cas}) {
                Require(Device::Finish(Filter::Xbrz),"switch intermediate target from xBRZ");
                Require(Device::Finish(filter),"execute reconstruction shader");
                std::vector<std::uint8_t> actual;
                Require(Device::ReadOutput(actual,output),"read reconstruction result");
                std::vector<std::uint8_t> expected(actual.size(),0);
                for (std::size_t i=3;i<expected.size();i+=4) expected[i]=255;
                for (int y=0;y<view.height;++y) for (int x=0;x<view.width;++x) for (int c=0;c<3;++c) {
                    const double value=filter==Filter::Jinc2
                        ? JincReference(input,size,(x+0.5)*size.width/view.width,(y+0.5)*size.height/view.height,c)
                        : CasReference(baseline,output,view,x,y,c);
                    expected[(std::size_t(y+view.y)*output.width+x+view.x)*4+c]=Quantize(value);
                }
                if (!PixelsClose(actual,expected)) {
                    unsigned worst=0; std::size_t at=0;
                    for (std::size_t i=0;i<actual.size();++i) { const auto error=unsigned(std::abs(int(actual[i])-int(expected[i]))); if(error>worst){worst=error;at=i;} }
                    std::fprintf(stderr,"Filter %d size %dx%d sample %u: difference %u at byte %zu (GPU %u, CPU %u)\n",int(filter),size.width,size.height,sample,worst,at,unsigned(actual[at]),unsigned(expected[at]));
                    std::exit(1);
                }
                expected[0]^=0x80; assert(!PixelsClose(actual,expected));
                if (filter==Filter::Jinc2) jincChanges+=!PixelsClose(actual,baseline);
                else casChanges+=!PixelsClose(actual,baseline);
                ++cases;
            }
            Device::DeleteImage(image);
        }
    }
    assert(jincChanges>0 && casChanges>0);
    std::printf("Jinc2/CAS: %u GPU/reference cases passed; %u/%u distinct from bilinear.\n",cases,jincChanges,casChanges);
}

static std::vector<std::uint8_t> CrtReference(const std::vector<std::uint8_t>& input,
    Size source, Size output, double gamma, double curvature=0, bool legacyTransfer=false) {
    std::vector<std::uint8_t> expected(std::size_t(output.width)*output.height*4,0);
    for (std::size_t i=3;i<expected.size();i+=4) expected[i]=255;
    const auto view=FitViewport(source,output);
    const double pi=3.14159265358979323846;
    for (int y=0;y<view.height;++y) for (int x=0;x<view.width;++x) {
        const double vx=2*(x+0.5)/view.width-1,vy=2*(y+0.5)/view.height-1;
        const double stretchX=1+curvature*vy*vy,stretchY=1+curvature*vx*vx;
        const double qx=vx*stretchX,qy=vy*stretchY;
        const double coverage=std::clamp(std::min((1-std::abs(qx))*0.5*view.width/stretchX,
            (1-std::abs(qy))*0.5*view.height/stretchY)+0.5,0.0,1.0);
        if (coverage==0) continue;
        const double sourceU=(qx+1)*0.5,sourceV=(qy+1)*0.5;
        const double sx=sourceU*source.width-0.5,sy=sourceV*source.height-0.5;
        const double scale=view.height/(source.height*stretchY);
        const double fade=std::clamp((scale-1.0)/0.5,0.0,1.0),visibility=fade*fade*(3-2*fade);
        const double footprint=pi/scale,attenuation=std::sin(footprint)/footprint;
        const int left=int(std::floor(sx)),top=int(std::floor(sy));
        const double fx=sx-left,fy=sy-top;
        const auto reconstruct=[&](int dx,int dy,int c) {
            double result=0;
            for (int row=0;row<2;++row) for (int col=0;col<2;++col) {
                const double value=Channel(input,source,left+dx+col,top+dy+row,c);
                const double linear=legacyTransfer ? value*value : std::pow(value,2.2);
                result+=linear*(col ? fx : 1-fx)*(row ? fy : 1-fy);
            }
            return result;
        };
        const double glowThreshold=legacyTransfer ? 0.12 : std::pow(0.12,1.1);
        double color[3]{},glow[3]{};
        for (int c=0;c<3;++c) {
            const double middle=reconstruct(0,0,c),l=reconstruct(-1,0,c),r=reconstruct(1,0,c);
            const double above=reconstruct(0,-1,c),below=reconstruct(0,1,c);
            color[c]=0.72*middle+0.14*(l+r);
            glow[c]=0.25*(std::max(l-glowThreshold,0.0)+std::max(r-glowThreshold,0.0)+
                std::max(above-glowThreshold,0.0)+std::max(below-glowThreshold,0.0));
        }
        const double brightness=color[0]*0.2126+color[1]*0.7152+color[2]*0.0722;
        const double depth=(0.82-0.32*brightness)*visibility;
        const double beam=(1-0.5*depth*(1+attenuation*std::cos(2*pi*(sourceV*source.height-0.25))))/(1-0.40*depth);
        const double vignette=1-0.10*(vx*vx+vy*vy);
        const int favored=(x+((view.height-1-y)/2)%2)%3;
        for (int c=0;c<3;++c) {
            const double mask=favored==c ? 1.50 : 0.50;
            const double linear=(1.35*color[c]*beam*mask+0.18*glow[c])*vignette;
            const double mapped=legacyTransfer ? linear : std::min(linear,0.75)+
                0.25*(1-std::exp(-std::max(linear-0.75,0.0)/0.25));
            const double encoded=std::pow(std::clamp(mapped,0.0,1.0),legacyTransfer ? 0.5 : 1.0/2.2);
            expected[(std::size_t(y+view.y)*output.width+x+view.x)*4+c]=Quantize(std::pow(encoded,1/gamma)*coverage);
        }
    }
    return expected;
}

static void CheckCrtCase(HWND window, Size size, Size target, unsigned sample, float gamma=1.0f, Filter filter=Filter::Crt) {
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(size),"resize CRT fixture");
    std::vector<std::uint32_t> source(std::size_t(size.width)*size.height);
    for (int y=0;y<size.height;++y) for (int x=0;x<size.width;++x) {
        const auto color=sample==0 ? Rgba(0,0,0) : sample==1 ? Rgba(128,128,128)
            : sample==3 ? ((x==size.width/2 && y==size.height/2) ? Rgba(255,255,255) : Rgba(0,0,0))
            : sample==4 ? ((x==0 || y==0 || x==size.width-1 || y==size.height-1) ? Rgba(255,32,16)
                : x==size.width/4 ? Rgba(0,192,0) : Rgba(0,0,0))
            : (x+y)%3==0 ? Rgba(255,0,64) : (x+y)%3==1 ? Rgba(0,224,32) : Rgba(16,32,255);
        source[std::size_t(y)*size.width+x]=color;
    }
    std::vector<std::uint8_t> input(source.size()*4);
    for (std::size_t i=0;i<source.size();++i) for (unsigned c=0;c<4;++c) input[i*4+c]=std::uint8_t(source[i]>>(8*c));
    const Image image=Device::UploadRgba(source.data(),size); Require(image.name!=0,"upload CRT fixture");
    Quad(image,nullptr,Rgba(255,255,255),Blend::Replace,255,false);
    std::vector<std::uint8_t> canvas;
    Require(Device::ReadCanvas(canvas),"read CRT source canvas");
    assert(canvas==input);
    Size output=target;
    const auto baseline=CaptureBilinear(output,gamma);
    std::vector<std::uint8_t> actual,repeated;
    Require(Device::Finish(Filter::Xbrz,gamma),"switch CRT fixture through xBRZ");
    Require(Device::Finish(Filter::Cas,gamma),"switch CRT fixture through CAS");
    Require(Device::Finish(filter,gamma),"finish CRT fixture");
    Require(Device::ReadOutput(actual,output),"read CRT result");
    auto expected=CrtReference(input,size,output,gamma,filter==Filter::CrtCurved ? 0.05 : 0);
    if (!PixelsClose(actual,expected)) {
        unsigned worst=0; std::size_t at=0;
        for (std::size_t i=0;i<actual.size();++i) { const auto error=unsigned(std::abs(int(actual[i])-int(expected[i]))); if(error>worst){worst=error;at=i;} }
        std::fprintf(stderr,"CRT filter %d %dx%d -> %dx%d sample %u gamma %.1f: difference %u at byte %zu (GPU %u, CPU %u)\n",
            int(filter),size.width,size.height,output.width,output.height,sample,gamma,worst,at,unsigned(actual[at]),unsigned(expected[at]));
        std::exit(1);
    }
    expected[0]^=0x80; assert(!PixelsClose(actual,expected)); // Independent-reference negative control.
    const auto view=FitViewport(size,output);
    for (int y=0;y<output.height;++y) for (int x=0;x<output.width;++x) {
        const auto at=(std::size_t(y)*output.width+x)*4;
        assert(actual[at+3]==255);
        if (sample==0 || x<view.x || x>=view.x+view.width || y<view.y || y>=view.y+view.height)
            assert(Pixel(actual,output,x,y,0,0,0));
    }
    if (sample==1 && gamma==1.0f && filter==Filter::Crt) {
        assert(!PixelsClose(actual,baseline));
        const int x=view.x+view.width/2,y=view.y+view.height/2;
        const auto at=(std::size_t(y)*output.width+x)*4;
        // A gray field exposes both the physical RGB aperture and vertical beam modulation.
        const int favored=(x-view.x+((view.height-1-(y-view.y))/2)%2)%3;
        assert(actual[at+favored]>actual[at+(favored+1)%3]+20);
        if (view.height==size.height*2 || view.height==size.height*3) {
            const auto rowMean=[&](int row) {
                double sum=0;
                for (int col=x-1;col<=x+1;++col) for (int c=0;c<3;++c)
                    sum+=actual[(std::size_t(row)*output.width+col)*4+c];
                return sum/9;
            };
            assert(std::abs(rowMean(y)-rowMean(y+1))>2);
        }
    }
    if (sample==3 && filter==Filter::Crt && view.width==size.width && view.height==size.height) {
        // This vertical black neighbor receives halation, but no horizontal beam reconstruction.
        const auto at=(std::size_t(view.y+size.height/2+1)*output.width+view.x+size.width/2)*4;
        assert(actual[at]>40 && actual[at+1]>40 && actual[at+2]>40);
    }
    Require(Device::Finish(filter,gamma),"repeat identical CRT frame");
    Require(Device::ReadOutput(repeated,output),"read repeated CRT frame");
    assert(actual==repeated);
    Require(Device::Finish(Filter::Xbrz),"switch away from CRT to xBRZ");
    Require(Device::Finish(Filter::Cas),"switch away from CRT to CAS");
    Require(Device::Finish(filter,gamma),"restore CRT after intermediate reuse");
    Require(Device::ReadOutput(repeated,output),"read restored CRT frame");
    assert(actual==repeated);
    if (filter==Filter::CrtCurved) {
        if (view.width>=80 && view.height>=60) {
            for (int x : {view.x,view.x+view.width-1}) for (int y : {view.y,view.y+view.height-1})
                assert(Pixel(actual,output,x,y,0,0,0));
        }
        Require(Device::Finish(Filter::Crt,gamma),"reset curvature for flat CRT");
        Require(Device::ReadOutput(repeated,output),"read flat CRT after curved CRT");
        assert(PixelsClose(repeated,CrtReference(input,size,output,gamma)));
        if (sample==4) {
            // An interior vertical green line bows toward the center, independently of black corners.
            const auto centroid=[&](const std::vector<std::uint8_t>& frame) {
                double weight=0,moment=0;
                for (int y=view.height/8-2;y<=view.height/8+2;++y)
                    for (int x=view.width/4-10;x<=view.width/4+10;++x) {
                        const double green=Channel(frame,output,view.x+x,view.y+y,1);
                        weight+=green; moment+=x*green;
                    }
                assert(weight>0);
                return moment/weight;
            };
            assert(centroid(actual)>centroid(repeated)+1.0);
        }
        Require(Device::Finish(Filter::CrtCurved,gamma),"restore curvature after flat CRT");
        Require(Device::ReadOutput(repeated,output),"read restored curved CRT");
        assert(actual==repeated);
        Require(Device::Finish(Filter::Crt,gamma),"reset curvature again for flat CRT");
        Require(Device::ReadOutput(repeated,output),"read second flat CRT reset");
        assert(PixelsClose(repeated,CrtReference(input,size,output,gamma)));
    }
    Require(Device::ReadCanvas(canvas),"check CRT preserves logical canvas");
    assert(canvas==input);
    Device::DeleteImage(image);
}

static void CheckCrtAtGameResolution(HWND window) {
    const Size source{800,600},target{1440,1080};
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(source),"resize CRT game-resolution fixture");
    for (auto gray : std::array<std::uint8_t,3>{64,96,128}) {
        Device::Clear(gray,gray,gray);
        Size output=target;
        const auto baseline=CaptureBilinear(output);
        std::vector<std::uint8_t> actual;
        Require(Device::Finish(Filter::Crt),"finish game-resolution CRT fixture");
        Require(Device::ReadOutput(actual,output),"read game-resolution CRT fixture");
        assert(output.width==target.width && output.height==target.height);
        assert(!PixelsClose(actual,baseline));
        std::vector<std::uint8_t> input(std::size_t(source.width)*source.height*4,gray);
        for (std::size_t i=3;i<input.size();i+=4) input[i]=255;
        const auto legacy=CrtReference(input,source,output,1.0,0,true);
        double darkest=255,brightest=0,total=0,control=0,legacyTotal=0;
        std::array<double,3> channelTotals{};
        unsigned minimumSpread=255;
        // Eighteen rows cover two full scan periods at 1.8x; 18 columns cover RGB triplets.
        for (int y=output.height/2-9;y<output.height/2+9;++y) {
            double sum=0;
            for (int x=output.width/2-9;x<output.width/2+9;++x) {
                const auto at=(std::size_t(y)*output.width+x)*4;
                const auto low=std::min({actual[at],actual[at+1],actual[at+2]});
                const auto high=std::max({actual[at],actual[at+1],actual[at+2]});
                minimumSpread=std::min(minimumSpread,unsigned(high-low));
                if (gray==128) assert(high-low>40);
                for (int c=0;c<3;++c) {
                    sum+=actual[at+c]; control+=baseline[at+c]; legacyTotal+=legacy[at+c];
                    channelTotals[c]+=actual[at+c];
                }
            }
            total+=sum;
            darkest=std::min(darkest,sum/54); brightest=std::max(brightest,sum/54);
        }
        const double mean=total/(18*18*3),controlMean=control/(18*18*3),legacyMean=legacyTotal/(18*18*3);
        assert(std::abs(mean-controlMean)/controlMean<0.12);
        assert(std::abs(mean-legacyMean)/legacyMean<0.03);
        assert((*std::max_element(channelTotals.begin(),channelTotals.end())-
            *std::min_element(channelTotals.begin(),channelTotals.end()))/(18*18)<=1.0);
        assert(brightest-darkest>20);
        std::printf("CRT gray %u at 800x600 -> 1440x1080: mean %.1f (bilinear %.1f, legacy %.1f), scanline swing %.1f, minimum RGB spread %u.\n",
            unsigned(gray),mean,controlMean,legacyMean,brightest-darkest,minimumSpread);
    }
}

static void CheckCrtTransferRamps(HWND window) {
    const Size source{16,12},target{48,36};
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(source),"resize CRT transfer ramp fixture");
    for (Filter filter : {Filter::Crt,Filter::CrtCurved}) {
        const double curvature=filter==Filter::CrtCurved ? 0.05 : 0;
        std::array<unsigned,3> previous{};
        std::array<unsigned,3> highlights{};
        const int x=target.width/2,y=target.height/2;
        const int favored=(x+((target.height-1-y)/2)%2)%3;
        const auto at=(std::size_t(y)*target.width+x)*4;
        for (unsigned gray=0;gray<=255;++gray) {
            Device::Clear(std::uint8_t(gray),std::uint8_t(gray),std::uint8_t(gray));
            Require(Device::Finish(filter),"finish CRT transfer ramp");
            Size output=target;
            std::vector<std::uint8_t> actual;
            Require(Device::ReadOutput(actual,output),"read CRT transfer ramp");
            assert(output.width==target.width && output.height==target.height);
            for (int c=0;c<3;++c) {
                const unsigned value=actual[at+c];
                if (gray==0) assert(value==0);
                assert(value>=previous[c] && value-previous[c]<=3);
                if (gray==1) assert(value>0);
                previous[c]=value;
            }
            if (gray==224 || gray==240 || gray==255) {
                std::vector<std::uint8_t> input(std::size_t(source.width)*source.height*4,std::uint8_t(gray));
                for (std::size_t i=3;i<input.size();i+=4) input[i]=255;
                assert(PixelsClose(actual,CrtReference(input,source,output,1.0,curvature)));
                const auto legacy=CrtReference(input,source,output,1.0,curvature,true);
                // Compare the same phosphor at the same position: the old transfer has a white plateau.
                assert(legacy[at+favored]==255);
                highlights[gray==224 ? 0 : gray==240 ? 1 : 2]=actual[at+favored];
            }
        }
        assert(highlights[1]>=highlights[0]+2 && highlights[2]>=highlights[1]+2);
        assert(highlights[2]<255);
        std::printf("CRT filter %d: monotonic 256-tone RGB ramp; former clipped highlights 224/240/255 retain %u/%u/%u.\n",
            int(filter),highlights[0],highlights[1],highlights[2]);
    }
}

static void CheckCrtFilter(HWND window) {
    // Native, downscaled, fractional, integer scales and both letterbox orientations.
    for (Size target : {Size{7,5},Size{4,3},Size{11,8},Size{14,10},Size{21,15},Size{31,15},Size{21,23}})
        for (unsigned sample=0;sample<3;++sample) CheckCrtCase(window,{7,5},target,sample);
    CheckCrtCase(window,{1,7},{9,21},2);
    CheckCrtCase(window,{7,1},{21,9},2);
    for (Size size : {Size{640,480},Size{800,600}}) CheckCrtCase(window,size,{80,60},2);
    // The independent reference applies gamma once, after reconstruction and CRT effects.
    CheckCrtCase(window,{7,5},{21,15},1,0.5f);
    CheckCrtCase(window,{7,5},{21,15},1,2.0f);
    CheckCrtCase(window,{9,7},{9,7},3);
    // A bounded curved set exercises the full frame, including antialiased screen edges.
    for (Size target : {Size{7,5},Size{14,10},Size{11,8},Size{31,15},Size{21,23}})
        CheckCrtCase(window,{7,5},target,2,1.0f,Filter::CrtCurved);
    CheckCrtCase(window,{7,5},{4,3},0,1.0f,Filter::CrtCurved);
    CheckCrtCase(window,{41,31},{164,124},1,1.0f,Filter::CrtCurved);
    CheckCrtCase(window,{65,49},{260,196},4,1.0f,Filter::CrtCurved);
    CheckCrtCase(window,{7,5},{21,15},1,0.5f,Filter::CrtCurved);
    CheckCrtCase(window,{7,5},{21,15},1,2.0f,Filter::CrtCurved);
    CheckCrtAtGameResolution(window);
    CheckCrtTransferRamps(window);
    std::puts("CRT: 28 flat and 10 curved GPU/reference cases, 2 transfer ramps and 6 highlight references passed, including geometry, brightness, gamma and switching.");
}

static void CaptureCrt(HWND window,const char* inputPath,const char* outputPath,Filter filter) {
    const Size source{800,600},target{1440,1080};
    std::vector<std::uint32_t> input(std::size_t(source.width)*source.height);
    FILE* file=nullptr;
    Require(fopen_s(&file,inputPath,"rb")==0 && file!=nullptr,"open CRT capture source");
    const auto bytes=input.size()*sizeof(input[0]);
    const auto count=std::fread(input.data(),1,bytes,file);
    const bool exact=count==bytes && std::fgetc(file)==EOF && !std::ferror(file);
    const bool closed=std::fclose(file)==0;
    Require(exact && closed,"read exact 800x600 top-down CRT source RGBA");
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(source),"resize CRT capture source");
    const Image image=Device::UploadRgba(input.data(),source);
    Require(image.name!=0,"upload CRT capture source");
    Quad(image,nullptr,Rgba(255,255,255),Blend::Replace,255,false);
    Require(Device::Finish(filter),"finish CRT capture");
    Size output{};
    std::vector<std::uint8_t> result;
    Require(Device::ReadOutput(result,output),"read CRT capture");
    assert(output.width==target.width && output.height==target.height);
    Require(fopen_s(&file,outputPath,"wb")==0 && file!=nullptr,"open CRT capture output");
    const auto written=std::fwrite(result.data(),1,result.size(),file);
    const bool outputClosed=std::fclose(file)==0;
    Require(written==result.size() && outputClosed,"write CRT capture RGBA");
    Device::DeleteImage(image);
    std::printf("CRT%s capture: %dx%d top-down RGBA.\n",filter==Filter::CrtCurved ? " Curved" : "",output.width,output.height);
}

static void BenchmarkCrt(HWND window,Filter filter) {
    const Size source{800,600},target{1440,1080};
    assert(SetWindowPos(window,nullptr,0,0,target.width,target.height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));
    Require(Device::ResizeCanvas(source),"resize CRT benchmark source");
    std::vector<std::uint32_t> input(std::size_t(source.width)*source.height);
    for (int y=0;y<source.height;++y) for (int x=0;x<source.width;++x)
        input[std::size_t(y)*source.width+x]=Rgba(unsigned(x*255/799),unsigned(y*255/599),unsigned((x+y)*255/1398));
    const Image image=Device::UploadRgba(input.data(),source);
    Require(image.name!=0,"upload CRT benchmark source");
    Quad(image,nullptr,Rgba(255,255,255),Blend::Replace,255,false);
    for (unsigned i=0;i<5;++i) { Require(Device::Finish(filter),"warm up CRT benchmark"); glFinish(); }
    std::array<double,120> milliseconds{};
    for (double& elapsed : milliseconds) {
        const auto start=std::chrono::steady_clock::now();
        Require(Device::Finish(filter),"benchmark CRT"); glFinish();
        elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    double total=0;
    for (double elapsed : milliseconds) total+=elapsed;
    std::sort(milliseconds.begin(),milliseconds.end());
    std::printf("CRT%s 800x600 -> 1440x1080: mean %.3f ms, median %.3f ms, p95 %.3f ms.\n",
        filter==Filter::CrtCurved ? " Curved" : "",total/120,(milliseconds[59]+milliseconds[60])*0.5,milliseconds[113]);
    std::puts("Method: mixed RGB gradients, 5 warmups and 120 timed Finish + glFinish calls; excludes upload, readback and SwapBuffers.");
    Device::DeleteImage(image);
}

int main(int argc,char** argv) {
    const bool capture=(argc==4 || (argc==5 && std::string(argv[4])=="curved")) && std::string(argv[1])=="--capture";
    const bool benchmark=(argc==2 || (argc==3 && std::string(argv[2])=="curved")) && std::string(argv[1])=="--benchmark";
    if (argc!=1 && !capture && !benchmark) {
        std::fprintf(stderr,"Usage: %s [--capture INPUT_RGBA OUTPUT_RGBA [curved] | --benchmark [curved]]\n",argv[0]);
        return 2;
    }
    static_assert(sizeof(void*) == 4, "Exercise the game's 32-bit GL driver.");
    const Filter migrated[]={Filter::Nearest,Filter::Cas,Filter::Xbrz,Filter::Jinc2,
        Filter::Cas,Filter::Crt,Filter::CrtCurved};
    for (unsigned i=0;i<7;++i) assert(MigrateLegacyFilter(i)==migrated[i]);
    assert(MigrateLegacyFilter(7)==Filter::Nearest);
    assert(MigrateLegacyFilter((std::numeric_limits<unsigned>::max)())==
        migrated[(std::numeric_limits<unsigned>::max)()%7]);
    const Filter version1[]={Filter::Nearest,Filter::Cas,Filter::Xbrz,Filter::Jinc2,
        Filter::Crt,Filter::CrtCurved,Filter::Nearest};
    for (unsigned i=0;i<7;++i) assert(MigrateLegacyFilter(i,1)==version1[i]);
    assert(MigrateLegacyFilter(7,1)==Filter::Nearest);
    assert(MigrateLegacyFilter((std::numeric_limits<unsigned>::max)(),1)==
        version1[(std::numeric_limits<unsigned>::max)()%7]);
    auto view = FitViewport({800,600},{2560,1080});
    assert(view.x==560 && view.y==0 && view.width==1440 && view.height==1080);
    view = FitViewport({640,480},{600,800});
    assert(view.x==0 && view.y==175 && view.width==600 && view.height==450);
    assert(FitViewport({0,480},{600,800}).width==0);
    auto mouse=MapMouse({-100,500},{800,600},{2560,1080});
    assert(mouse.x==0 && mouse.y==277);
    mouse=MapMouse({3000,2000},{800,600},{2560,1080});
    assert(mouse.x==799 && mouse.y==599);

    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.style=CS_OWNDC; cls.lpfnWndProc=DefWindowProcW;
    cls.hInstance=instance; cls.lpszClassName=L"CharonNativeRendererTest";
    assert(RegisterClassW(&cls));
    HWND window=CreateWindowW(cls.lpszClassName,L"Charon renderer test",WS_POPUP,
        0,0,40,30,nullptr,nullptr,instance,nullptr);
    assert(window);
    Require(Device::Create(window,{4,4}),"create native device and compile shaders");
    Device::SetVsync(false);
    if (capture || benchmark) {
        const Filter filter=(capture ? argc==5 : argc==3) ? Filter::CrtCurved : Filter::Crt;
        std::printf("OpenGL renderer: %s; vendor: %s; version: %s\n",glGetString(GL_RENDERER),glGetString(GL_VENDOR),glGetString(GL_VERSION));
        if (capture) CaptureCrt(window,argv[2],argv[3],filter);
        else BenchmarkCrt(window,filter);
        Device::Destroy();
        DestroyWindow(window); UnregisterClassW(cls.lpszClassName,instance);
        return 0;
    }
    std::vector<std::uint8_t> pixels;
    std::array<std::uint32_t,256> palette{};
    palette.fill(Rgba(0,0,0));
    palette[1]=Rgba(255,0,0); palette[2]=Rgba(0,255,0); palette[3]=Rgba(0,0,255);
    const std::uint8_t indices[]={1,0,2,3};
    Image indexed=Device::UploadIndices(indices,{2,2});
    Require(indexed.name!=0,"upload indices");
    Device::Clear(10,20,30); Quad(indexed,palette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read palette result");
    assert(Pixel(pixels,{4,4},0,0,255,0,0));
    assert(Pixel(pixels,{4,4},3,0,10,20,30));
    assert(Pixel(pixels,{4,4},0,3,0,255,0));
    assert(Pixel(pixels,{4,4},3,3,0,0,255));
    assert(!Pixel(pixels,{4,4},0,0,0,255,0)); // Comparator negative control.

    // A palette is mutable game storage: the same pointer may hold new colors.
    palette[1]=Rgba(12,34,56);
    Quad(indexed,palette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read mutated same-pointer palette");
    assert(Pixel(pixels,{4,4},0,0,12,34,56));
    const auto copiedPalette=palette;
    Quad(indexed,copiedPalette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read equal-content different-pointer palette");
    assert(Pixel(pixels,{4,4},0,0,12,34,56));
    palette[1]=Rgba(255,0,0);

    const std::uint8_t edgePixels[]{1,0,1,0};
    const Image edge=Device::UploadIndices(edgePixels,{2,2}); Require(edge.name!=0,"upload tile-edge fixture");
    Device::Clear(10,20,30);
    Quad(edge,palette.data(),Rgba(255,255,255),Blend::Replace,255,true,true);
    Require(Device::ReadCanvas(pixels),"read bilinear tile-edge control");
    assert(Pixel(pixels,{4,4},1,1,191,0,0,1));
    assert(Pixel(pixels,{4,4},2,1,64,0,0,1)); // Filtering mixes black padding into an opaque dark border.
    Device::Clear(10,20,30); Quad(edge,palette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read point-sampled tile edge");
    assert(Pixel(pixels,{4,4},1,1,255,0,0));
    assert(Pixel(pixels,{4,4},2,1,10,20,30));
    Device::DeleteImage(edge);

    Device::Clear(10,20,30); Quad(indexed,palette.data(),Rgba(0,0,0));
    Require(Device::ReadCanvas(pixels),"read chromakey-before-lighting result");
    assert(Pixel(pixels,{4,4},0,0,0,0,0));
    assert(Pixel(pixels,{4,4},3,0,10,20,30));
    palette[0]=Rgba(80,120,160);
    Quad(indexed,palette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read nonblack palette-zero result");
    assert(Pixel(pixels,{4,4},3,0,80,120,160));

    Quad({},nullptr,Rgba(200,100,50));
    Require(Device::ReadCanvas(pixels),"read RGB tint");
    assert(Pixel(pixels,{4,4},1,1,200,100,50));
    Device::Clear(0,0,200); Quad({},nullptr,Rgba(200,0,0),Blend::Alpha,128);
    Require(Device::ReadCanvas(pixels),"read alpha blend");
    assert(Pixel(pixels,{4,4},1,1,100,0,100,1));
    Device::Clear(30,40,50); Quad({},nullptr,Rgba(50,60,70),Blend::Add);
    Require(Device::ReadCanvas(pixels),"read additive blend");
    assert(Pixel(pixels,{4,4},1,1,80,100,120));
    Device::Clear(200,100,50); Quad({},nullptr,Rgba(128,64,255),Blend::Multiply);
    Require(Device::ReadCanvas(pixels),"read multiply blend");
    assert(Pixel(pixels,{4,4},1,1,100,25,50,1));

    Quad({},nullptr,Rgba(64,128,192));
    Size gammaOutput{};
    for (Filter filter : {Filter::Nearest,Filter::Jinc2,Filter::Cas}) {
        Require(Device::Finish(filter,1.0f),"finish neutral gamma");
        Require(Device::ReadOutput(pixels,gammaOutput),"read neutral gamma");
        assert(Pixel(pixels,gammaOutput,20,15,64,128,192));
        Require(Device::Finish(filter,2.0f),"finish brighter gamma");
        Require(Device::ReadOutput(pixels,gammaOutput),"read brighter gamma");
        assert(Pixel(pixels,gammaOutput,20,15,128,181,221,1));
        Require(Device::Finish(filter,0.5f),"finish darker gamma");
        Require(Device::ReadOutput(pixels,gammaOutput),"read darker gamma");
        assert(Pixel(pixels,gammaOutput,20,15,16,64,145,1));
    }

    const std::uint32_t rgba[]={Rgba(255,0,0),Rgba(0,0,255),Rgba(255,0,0),Rgba(0,0,255)};
    Image color=Device::UploadRgba(rgba,{2,2}); Require(color.name!=0,"upload RGBA");
    Quad(color,nullptr,Rgba(255,255,255));
    Require(Device::Finish(Filter::Nearest),"finish nearest");
    Size output{}; std::vector<std::uint8_t> nearest,bilinear,xbrz,jinc2;
    Require(Device::ReadOutput(nearest,output),"read nearest output");
    assert(output.width==40 && output.height==30);
    assert(Pixel(nearest,output,0,15,0,0,0));
    assert(Pixel(nearest,output,8,15,255,0,0));
    assert(Pixel(nearest,output,31,15,0,0,255));
    bilinear=CaptureBilinear(output);
    assert(nearest!=bilinear);
    assert(bilinear[(15*output.width+20)*4]>0 && bilinear[(15*output.width+20)*4+2]>0);
    Require(Device::Finish(Filter::Xbrz),"finish xBRZ");
    Require(Device::ReadOutput(xbrz,output),"read xBRZ output");
    assert(Pixel(xbrz,output,0,15,0,0,0));
    assert(Pixel(xbrz,output,8,15,255,0,0));
    Require(Device::Finish(Filter::Jinc2),"finish Jinc2");
    Require(Device::ReadOutput(jinc2,output),"read Jinc2 output");
    assert(Pixel(jinc2,output,0,15,0,0,0));
    assert(Pixel(jinc2,output,8,15,255,0,0));
    assert(Pixel(jinc2,output,31,15,0,0,255));
    Require(Device::Present(),"present once");

    for (Size size : {Size{640,480},Size{800,600}}) {
        Require(Device::ResizeCanvas(size),"resize logical canvas");
        Device::Clear(80,100,120);
        Require(Device::ReadCanvas(pixels),"read resized canvas");
        assert(pixels.size()==std::size_t(size.width)*size.height*4);
        assert(Pixel(pixels,size,size.width-1,size.height-1,80,100,120));
        for (Filter filter : {Filter::Nearest,Filter::Cas,Filter::Xbrz,Filter::Jinc2}) {
            Require(Device::Finish(filter),"filter resized canvas");
            Require(Device::ReadOutput(pixels,output),"read filtered canvas");
            assert(Pixel(pixels,output,20,15,80,100,120,1));
        }
    }
    CheckReconstructionFilters(window);
    CheckCrtFilter(window);
    std::thread deleteThread([indexed] { Device::DeleteImage(indexed); }); deleteThread.join();
    Require(Device::BeginFrame(),"drain queued texture deletion");
    Device::DeleteImage(color); Device::Destroy();
    Require(Device::Create(window,{640,480}),"recreate context on existing pixel format");
    CheckCrtCase(window,{9,7},{31,23},2,2.0f);
    CheckCrtCase(window,{9,7},{31,23},2,2.0f,Filter::CrtCurved);
#ifdef CHARON_OPENGL_TEST_CONTEXT
    const HGLRC lostContext=wglGetCurrentContext();
    const HDC lostDc=wglGetCurrentDC();
    assert(wglMakeCurrent(nullptr,nullptr));
    Device::FailDestroyActivationForTests(true);
    Device::Destroy();
    Device::FailDestroyActivationForTests(false);
    assert(!Device::Ready());
    assert(!wglMakeCurrent(lostDc,lostContext)); // Destruction must delete even without activation.
    Require(Device::Create(window,{4,4}),"recreate after failed destroy activation");
    const std::uint8_t freshIndex=1;
    const auto fresh=Device::UploadIndices(&freshIndex,{1,1});
    Quad(fresh,palette.data(),Rgba(255,255,255));
    Require(Device::ReadCanvas(pixels),"read palette after context recreation");
    assert(Pixel(pixels,{4,4},0,0,255,0,0));
    Device::DeleteImage(fresh);
#endif
    Device::Destroy();
    DestroyWindow(window); UnregisterClassW(cls.lpszClassName,instance);
    std::puts("Native OpenGL GPU checks passed: palette, RGB, chromakey, blends, filters, resize and recreation.");
}
