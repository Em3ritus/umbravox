#pragma once
#include "layout_engine.h"
#include <cstdint>
#include <cstring>
namespace framepresent {
// Front mirrors what was sent; back is fully composed before any presentation.
// No backlight manipulation. Exact pixel comparison preserves unchanged chrome.
class Frame {
public:
    void begin(uint16_t* front, uint16_t* back, int w, int h) {
        front_ = front; back_ = back; w_ = w; h_ = h; valid_ = false;
        std::memset(back_, 0, w_*h_*sizeof(uint16_t));
        clearStaged();
    }
    void invalidate() { valid_ = false; }
    uint16_t* rows(int y) { return back_ + y*w_; }
    // Compare SRAM composition with the visible mirror once, while staging.
    // Retain row extents so presentation never rescans two PSRAM frames.
    void stage(int y0, const uint16_t* pixels, int count) {
        if (h_ <= MaxRows) {
            staged_ = true;
            for (int y=y0; y<y0+count; ++y) {
                const uint16_t* src=pixels+(y-y0)*w_;
                const uint16_t* old=front_+y*w_;
                int lo=0,hi=w_;
                if (valid_) {
                    if (std::memcmp(old,src,w_*sizeof(uint16_t))==0) hi=0;
                    else {
                        while(lo<hi && old[lo]==src[lo]) ++lo;
                        while(hi>lo && old[hi-1]==src[hi-1]) --hi;
                    }
                }
                left_[y]=lo;right_[y]=hi;
            }
        }
        std::memcpy(back_+y0*w_,pixels,w_*count*sizeof(uint16_t));
    }
    uieng::Rect changed(uieng::Rect r) const {
        if (!valid_) return r;
        int x0 = r.x+r.w, y0 = r.y+r.h, x1 = r.x, y1 = r.y;
        for (int y=r.y; y<r.y+r.h; ++y) for(int x=r.x; x<r.x+r.w; ++x)
            if (front_[y*w_+x] != back_[y*w_+x]) {
                if(x<x0)x0=x; if(x>=x1)x1=x+1;
                if(y<y0)y0=y; if(y>=y1)y1=y+1;
            }
        return x1>x0 && y1>y0 ? uieng::Rect{x0,y0,x1-x0,y1-y0} : uieng::Rect{};
    }
    template<class Sink> void present(bool menu, Sink&& send, int y0=0, int y1=-1) {
        compareY0_ = valid_ ? y0 : 0;
        compareY1_ = valid_ && y1>=0 ? y1 : h_;
        // The whole frame is ready. No application/GPS pass between regions.
        emit({0,0,w_,44},send);
        if (menu) {
            const auto m = uieng::menu(w_,h_);
            for(int row=0;row<3;++row) for(int col=0;col<3;++col) {
                const auto c=m.cells[row*3+col];
                const int x0=col?c.x:0, y0=row?c.y:44;
                const int x1=col==2?w_:c.x+c.w;
                const int y1=row==2?h_-44:c.y+c.h;
                emit({x0,y0,x1-x0,y1-y0},send);
            }
        } else emit({0,44,w_,h_-88},send);
        emit({0,h_-44,w_,44},send);
        valid_=true;
        clearStaged();
    }
private:
    template<class Sink> void emit(uieng::Rect region, Sink& send) {
        const int y0=std::max(region.y,compareY0_), y1=std::min(region.y+region.h,compareY1_);
        if(y1<=y0)return;
        region.y=y0;region.h=y1-y0;
        uieng::Rect r;
        if (!valid_ || !staged_) r=changed(region);
        else {
            int lo=region.x+region.w,hi=region.x,top=region.y+region.h,bottom=region.y;
            for(int y=region.y;y<region.y+region.h;++y) {
                const int l=std::max(region.x,(int)left_[y]);
                const int h=std::min(region.x+region.w,(int)right_[y]);
                if(h<=l)continue;
                lo=std::min(lo,l);hi=std::max(hi,h);top=std::min(top,y);bottom=y+1;
            }
            if(hi>lo && bottom>top)r={lo,top,hi-lo,bottom-top};
        }
        if(!r.w)return;
        send(r,back_,w_);
        for(int y=r.y;y<r.y+r.h;++y)
            std::memcpy(front_+y*w_+r.x,back_+y*w_+r.x,r.w*sizeof(uint16_t));
    }
    static constexpr int MaxRows=320;
    uint16_t left_[MaxRows]{},right_[MaxRows]{};
    bool staged_=false;
    void clearStaged() {
        staged_=false;
        for(int y=0;y<std::min(h_,MaxRows);++y){left_[y]=w_;right_[y]=0;}
    }
    int compareY0_=0,compareY1_=0;
    uint16_t *front_=nullptr,*back_=nullptr; int w_=0,h_=0; bool valid_=false;
};
}
