// Visual Studio 2022 / C++17, Windows SDK only.
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
namespace fs = std::filesystem;
using Mask = std::vector<unsigned char>;

struct GdiSession {
    ULONG_PTR token{};
    GdiSession() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("GDI+ startup failed");
    }
    ~GdiSession() { Gdiplus::GdiplusShutdown(token); }
};
struct Object {
    int area{}, x0{}, y0{}, x1{}, y1{};
    double cx{}, cy{}, elongation{}, angle{}, extent{}, radius{}, rms{};
};
struct Options {
    fs::path input = L"images/sample.jpg", output = L"output";
    int threshold = -1;
    double minAreaRatio = 0.0006;
    bool show = false;
};
void check(Gdiplus::Status status, const char* message) {
    if (status != Gdiplus::Ok) throw std::runtime_error(message);
}
CLSID pngEncoder() {
    UINT count = 0, bytes = 0;
    check(Gdiplus::GetImageEncodersSize(&count, &bytes), "Cannot query encoders");
    if (!bytes) throw std::runtime_error("No image encoders");
    std::vector<unsigned char> data(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());
    check(Gdiplus::GetImageEncoders(count, bytes, encoders), "Cannot list encoders");
    for (UINT i = 0; i < count; ++i)
        if (std::wstring(encoders[i].MimeType) == L"image/png") return encoders[i].Clsid;
    throw std::runtime_error("PNG encoder missing");
}
void savePng(Gdiplus::Bitmap& bitmap, const fs::path& path) {
    const auto encoder = pngEncoder();
    check(bitmap.Save(path.c_str(), &encoder, nullptr), "Cannot save PNG");
}
Mask grayscale(Gdiplus::Bitmap& image, int w, int h) {
    Gdiplus::Bitmap copy(w, h, PixelFormat32bppARGB);
    { Gdiplus::Graphics graphics(&copy);
      check(graphics.Clear(Gdiplus::Color(255,255,255,255)), "Cannot clear image");
      check(graphics.DrawImage(&image, 0, 0, w, h), "Cannot decode image"); }
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData data{};
    check(copy.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data), "Cannot read pixels");
    Mask gray(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const auto* row = static_cast<const unsigned char*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (int x = 0; x < w; ++x)
            gray[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(
                (29 * row[x * 4] + 150 * row[x * 4 + 1] + 77 * row[x * 4 + 2] + 128) >> 8);
    }
    check(copy.UnlockBits(&data), "Cannot unlock pixels");
    return gray;
}
void saveGray(const Mask& pixels, int w, int h, const fs::path& path, bool binary) {
    Gdiplus::Bitmap bitmap(w, h, PixelFormat32bppARGB);
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData data{};
    check(bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data), "Cannot write pixels");
    for (int y = 0; y < h; ++y) {
        auto* row = static_cast<unsigned char*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (int x = 0; x < w; ++x) {
            auto v = pixels[static_cast<size_t>(y) * w + x];
            if (binary) v = v ? 255 : 0;
            row[4*x] = row[4*x+1] = row[4*x+2] = v;
            row[4*x+3] = 255;
        }
    }
    check(bitmap.UnlockBits(&data), "Cannot unlock pixels");
    savePng(bitmap, path);
}
Mask median3(const Mask& input, int w, int h) {
    Mask output(input.size());
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        std::array<unsigned char, 9> a{};
        int k = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            a[k++] = input[static_cast<size_t>(std::clamp(y+dy,0,h-1))*w + std::clamp(x+dx,0,w-1)];
        std::nth_element(a.begin(), a.begin()+4, a.end());
        output[static_cast<size_t>(y)*w+x] = a[4];
    }
    return output;
}
int otsu(const Mask& gray) {
    std::array<uint64_t,256> hist{};
    for (auto v : gray) ++hist[v];
    double totalSum=0, leftSum=0, best=-1;
    uint64_t left=0;
    for (int i=0;i<256;++i) totalSum += i * static_cast<double>(hist[i]);
    int threshold=0;
    for (int t=0;t<255;++t) {
        left += hist[t]; leftSum += t * static_cast<double>(hist[t]);
        const auto right=gray.size()-left;
        if (!left || !right) continue;
        const double delta=leftSum/left-(totalSum-leftSum)/right;
        const double score=static_cast<double>(left)*right*delta*delta;
        if (score>best) { best=score; threshold=t; }
    }
    return threshold;
}
// 3x3 closing: smooth small gaps on hole boundaries.
Mask morphology(const Mask& input,int w,int h,bool dilate) {
    Mask output(input.size());
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        bool value=!dilate;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            const int nx=x+dx,ny=y+dy;
            const bool on=nx>=0&&nx<w&&ny>=0&&ny<h&&input[static_cast<size_t>(ny)*w+nx];
            if(dilate) value=value||on; else value=value&&on;
        }
        output[static_cast<size_t>(y)*w+x]=value;
    }
    return output;
}
// Algebraic least-squares circle fit in centroid-relative coordinates:
// 2*u*a + 2*v*b + c = u*u + v*v, then center=(origin+a,origin+b).
// Solve the symmetric 3x3 normal equations by pivoted Gaussian elimination.
bool fitCircle(const std::vector<int>& edge,int w,double originX,double originY,
               double& cx,double& cy,double& radius,double& rms) {
    if(edge.size()<12) return false;
    double matrix[3][4]{};
    for(int index:edge) {
        const double u=index%w-originX,v=index/w-originY;
        const double row[3]={2*u,2*v,1},z=u*u+v*v;
        for(int i=0;i<3;++i) {
            for(int j=0;j<3;++j) matrix[i][j]+=row[i]*row[j];
            matrix[i][3]+=row[i]*z;
        }
    }
    for(int col=0;col<3;++col) {
        int pivot=col;
        for(int row=col+1;row<3;++row)
            if(std::abs(matrix[row][col])>std::abs(matrix[pivot][col])) pivot=row;
        if(std::abs(matrix[pivot][col])<1e-9) return false;
        for(int j=0;j<4;++j) std::swap(matrix[col][j],matrix[pivot][j]);
        const double div=matrix[col][col];
        for(int j=col;j<4;++j) matrix[col][j]/=div;
        for(int row=0;row<3;++row) if(row!=col) {
            const double factor=matrix[row][col];
            for(int j=col;j<4;++j) matrix[row][j]-=factor*matrix[col][j];
        }
    }
    const double a=matrix[0][3],b=matrix[1][3],r2=matrix[2][3]+a*a+b*b;
    if(r2<=0) return false;
    cx=originX+a;cy=originY+b;radius=std::sqrt(r2);
    double error=0;
    for(int index:edge) {
        const double residual=std::hypot(index%w-cx,index/w-cy)-radius;
        error+=residual*residual;
    }
    rms=std::sqrt(error/edge.size());
    return std::isfinite(cx)&&std::isfinite(cy)&&std::isfinite(radius)&&std::isfinite(rms);
}
std::vector<Object> components(const Mask& mask,int w,int h,double minRatio, Mask& accepted) {
    Mask seen(mask.size(),0);
    std::vector<Object> objects;
    std::vector<int> queue;
    accepted.assign(mask.size(),0);
    const int minArea=std::max(20,static_cast<int>(w*static_cast<double>(h)*minRatio));
    for(int start=0;start<w*h;++start) {
        if(!mask[start]||seen[start]) continue;
        queue.clear(); queue.push_back(start); seen[start]=1;
        int x0=w,y0=h,x1=0,y1=0;
        double sx=0,sy=0,sxx=0,syy=0,sxy=0;
        for(size_t p=0;p<queue.size();++p) {
            int i=queue[p],x=i%w,y=i/w;
            x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
            sx+=x;sy+=y;sxx+=x*static_cast<double>(x);syy+=y*static_cast<double>(y);sxy+=x*static_cast<double>(y);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int nx=x+dx,ny=y+dy;
                if(nx<0||nx>=w||ny<0||ny>=h) continue;
                int j=ny*w+nx;
                if(mask[j]&&!seen[j]) { seen[j]=1;queue.push_back(j); }
            }
        }
        double area=static_cast<double>(queue.size()),cx=sx/area,cy=sy/area;
        double a=sxx/area-cx*cx,b=sxy/area-cx*cy,c=syy/area-cy*cy;
        double d=std::hypot(a-c,2*b);
        double elongation=std::sqrt(std::max(0.0,a+c+d)/std::max(1e-9,a+c-d));
        const double aspect=static_cast<double>(x1-x0+1)/(y1-y0+1);
        const double extent=area/((x1-x0+1)*static_cast<double>(y1-y0+1));
        // Reject truncated holes, frame, text, large regions and non-round regions.
        if(x0<=2||y0<=2||x1>=w-3||y1>=h-3||area<minArea||area>w*static_cast<double>(h)*0.05
            ||aspect<0.75||aspect>1.33||elongation>1.35||extent<0.65||extent>0.90) continue;
        std::vector<int> edge;
        for(int index:queue) {
            const int x=index%w,y=index/w;
            if(x>0&&x<w-1&&y>0&&y<h-1&&
                (!mask[index-1]||!mask[index+1]||!mask[index-w]||!mask[index+w])) edge.push_back(index);
        }
        double radius=0,rms=0;
        if(!fitCircle(edge,w,cx,cy,cx,cy,radius,rms)||rms>std::max(1.5,0.06*radius)) continue;
        objects.push_back({static_cast<int>(area),x0,y0,x1,y1,cx,cy,elongation,0,extent,radius,rms});
        for(int index:queue) accepted[index]=1;
    }
    // IDs increase from top to bottom, with x as a tiebreaker.
    std::sort(objects.begin(),objects.end(),[](const Object& a,const Object& b) {
        return a.cy==b.cy?a.cx<b.cx:a.cy<b.cy;
    });
    return objects;
}
void annotate(Gdiplus::Bitmap& image,int w,int h,const std::vector<Object>& objects,const fs::path& path) {
    Gdiplus::Bitmap result(w,h,PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&result);
        check(g.DrawImage(&image,0,0,w,h),"Cannot draw source");
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float scale=static_cast<float>(std::max(w,h))/720.0f;
        Gdiplus::Pen pen(Gdiplus::Color(255,0,190,80),std::max(1.0f,2*scale));
        Gdiplus::SolidBrush green(Gdiplus::Color(255,0,130,65)),white(Gdiplus::Color(255,255,255,255)),black(Gdiplus::Color(220,0,0,0));
        Gdiplus::Font font(L"Arial",std::max(12.0f,17*scale),Gdiplus::FontStyleBold,Gdiplus::UnitPixel);
        for(size_t i=0;i<objects.size();++i) {
            const auto& o=objects[i];
            g.DrawEllipse(&pen,static_cast<float>(o.cx-o.radius),static_cast<float>(o.cy-o.radius),
                static_cast<float>(2*o.radius),static_cast<float>(2*o.radius));
            Gdiplus::Pen cross(Gdiplus::Color(255,255,50,30),std::max(1.0f,2*scale));
            g.DrawLine(&cross,static_cast<float>(o.cx)-8*scale,static_cast<float>(o.cy),static_cast<float>(o.cx)+8*scale,static_cast<float>(o.cy));
            g.DrawLine(&cross,static_cast<float>(o.cx),static_cast<float>(o.cy)-8*scale,static_cast<float>(o.cx),static_cast<float>(o.cy)+8*scale);
            const float x=static_cast<float>(o.x0),y=std::max(30*scale,static_cast<float>(o.y0)-25*scale);
            g.FillRectangle(&green,x,y,35*scale,24*scale);
            g.DrawString(std::to_wstring(i+1).c_str(),-1,&font,Gdiplus::PointF(x+4*scale,y+2*scale),&white);
        }
        g.FillRectangle(&black,0.0f,0.0f,210*scale,30*scale);
        std::wstring title=L"Centers: "+std::to_wstring(objects.size());
        g.DrawString(title.c_str(),-1,&font,Gdiplus::PointF(8*scale,4*scale),&white);
    }
    savePng(result,path);
}
Options parse(int argc,wchar_t** argv) {
    Options o;
    if(argc>1) o.input=argv[1];
    int i=2;
    if(i<argc&&std::wstring(argv[i]).rfind(L"--",0)!=0) o.output=argv[i++];
    for(;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--show") o.show=true;
        else if(arg==L"--threshold"&&i+1<argc) {
            std::wstring v=argv[++i];size_t n=0;o.threshold=std::stoi(v,&n);
            if(n!=v.size()||o.threshold<0||o.threshold>255) throw std::runtime_error("threshold must be 0..255");
        } else if(arg==L"--min-area-ratio"&&i+1<argc) {
            std::wstring v=argv[++i];size_t n=0;o.minAreaRatio=std::stod(v,&n);
            if(n!=v.size()||!std::isfinite(o.minAreaRatio)||o.minAreaRatio<=0||o.minAreaRatio>=0.05)
                throw std::runtime_error("min-area-ratio must be between 0 and 0.05");
        } else throw std::runtime_error("Unknown/incomplete argument; use --help");
    }
    return o;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1&&(std::wstring(argv[1])==L"--help"||std::wstring(argv[1])==L"-h")) {
            std::cout<<"CircleCenterDetector [input.png] [output_directory] [--threshold 0..255] [--min-area-ratio 0.0006] [--show]\n";return 0;
        }
        Options o=parse(argc,argv);
        o.input=fs::absolute(o.input);o.output=fs::absolute(o.output);
        if(!fs::is_regular_file(o.input)) throw std::runtime_error("Input image does not exist; pass its path or run from the project directory");
        // Refuse to overwrite the source, even when input lives in the output folder.
        for(const auto* name:{L"01_gray.png",L"02_binary.png",L"03_mask.png",L"04_result.png",L"objects.csv",L"summary.json"})
            if(fs::weakly_canonical(o.input)==fs::weakly_canonical(o.output/name)) throw std::runtime_error("Output would overwrite input; choose another output directory");
        GdiSession session;
        Gdiplus::Bitmap image(o.input.c_str());
        check(image.GetLastStatus(),"Cannot load image (PNG/JPEG/BMP supported)");
        const auto uw=image.GetWidth(),uh=image.GetHeight();
        if(!uw||!uh||static_cast<uint64_t>(uw)*uh>50000000) throw std::runtime_error("Image dimensions unsupported (limit: 50 million pixels)");
        int w=static_cast<int>(uw),h=static_cast<int>(uh);
        Mask gray=median3(grayscale(image,w,h),w,h);
        const int otsuThreshold=otsu(gray);
        int threshold=o.threshold>=0?o.threshold:otsuThreshold;
        Mask binary(gray.size());
        for(size_t i=0;i<gray.size();++i) binary[i]=gray[i]<=threshold;
        Mask closed=morphology(morphology(binary,w,h,true),w,h,false),accepted;
        auto objects=components(closed,w,h,o.minAreaRatio,accepted);
        fs::create_directories(o.output);
        saveGray(gray,w,h,o.output/L"01_gray.png",false);
        saveGray(binary,w,h,o.output/L"02_binary.png",true);
        saveGray(accepted,w,h,o.output/L"03_mask.png",true);
        annotate(image,w,h,objects,o.output/L"04_result.png");
        std::ofstream csv(o.output/L"objects.csv");
        csv.exceptions(std::ios::failbit|std::ios::badbit);
        csv<<"id,x,y,width,height,area_px,center_x_px,center_y_px,radius_px,diameter_px,fit_rms_px\n"<<std::fixed<<std::setprecision(3);
        for(size_t i=0;i<objects.size();++i) { const auto& a=objects[i];
            csv<<i+1<<','<<a.x0<<','<<a.y0<<','<<a.x1-a.x0+1<<','<<a.y1-a.y0+1<<','<<a.area<<','<<a.cx<<','<<a.cy<<','<<a.radius<<','<<2*a.radius<<','<<a.rms<<'\n'; }
        csv.close();
        std::ofstream json(o.output/L"summary.json");json.exceptions(std::ios::failbit|std::ios::badbit);
        json<<"{\n  \"count\": "<<objects.size()<<",\n  \"width\": "<<w<<",\n  \"height\": "<<h<<",\n  \"threshold\": "<<threshold<<",\n  \"min_area_ratio\": "<<o.minAreaRatio<<"\n}\n";
        json.close();
        std::cout<<"Circle centers detected: "<<objects.size()<<"\nThreshold: "<<threshold<<"\n";
        std::wcout<<L"Results: "<<o.output.wstring()<<L"\n";
        if(o.show) ShellExecuteW(nullptr,L"open",(o.output/L"04_result.png").c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        return 0;
    } catch(const std::exception& e) { std::cerr<<"Error: "<<e.what()<<'\n';return 1; }
}
