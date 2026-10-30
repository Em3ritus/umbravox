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
    }
    void invalidate() { valid_ = false; }
    uint16_t* rows(int y) { return back_ + y*w_; }
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
    }
private:
    template<class Sink> void emit(uieng::Rect region, Sink& send) {
        const int y0=std::max(region.y,compareY0_), y1=std::min(region.y+region.h,compareY1_);
        if(y1<=y0)return;
        region.y=y0;region.h=y1-y0;
        const auto r=changed(region); if(!r.w)return;
        send(r,back_,w_);
        for(int y=r.y;y<r.y+r.h;++y)
            std::memcpy(front_+y*w_+r.x,back_+y*w_+r.x,r.w*sizeof(uint16_t));
    }
    int compareY0_=0,compareY1_=0;
    uint16_t *front_=nullptr,*back_=nullptr; int w_=0,h_=0; bool valid_=false;
};
}
