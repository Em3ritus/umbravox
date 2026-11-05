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
    // Stage ONLY the columns [x0,x0+bw) of rows [y0,y0+count): `pixels` holds just those columns, packed.
    // The rest of the back frame keeps what it had, so this is valid only after a presented frame.
    void stageWindow(int y0, const uint16_t* pixels, int count, int x0, int bw) {
        for (int y=y0; y<y0+count; ++y) {
            const uint16_t* src=pixels+(y-y0)*bw;
            const uint16_t* old=front_+y*w_+x0;
            int lo=0,hi=bw;
            if (valid_) {
                if (std::memcmp(old,src,bw*sizeof(uint16_t))==0) hi=0;
                else {
                    while(lo<hi && old[lo]==src[lo]) ++lo;
                    while(hi>lo && old[hi-1]==src[hi-1]) --hi;
                }
            }
            if (h_ <= MaxRows) { left_[y]=x0+lo; right_[y]=hi?x0+hi:0; }
            std::memcpy(back_+y*w_+x0,src,bw*sizeof(uint16_t));
        }
        if (h_ <= MaxRows) staged_=true;
    }
    // Stage one dirty REGION (columns [x0,x0+bw) of rows [y0,y0+count), packed in `pixels`). Unlike
    // stageWindow, each region keeps its own changed bounding box, so a frame that touches a count tile, the
    // radar and the spectrum sends three small rectangles instead of one that spans them. Valid only after
    // a presented frame. The rest of the back frame keeps what it had.
    // `id` names the region (its index in the engine's list). Without it, regions sharing a left edge and width are
    // merged, so two bands at the top and bottom of the screen would grow into one full-height rectangle.
    void stageRegion(int y0, const uint16_t* pixels, int count, int x0, int bw, int id = -1) {
        Reg* r = nullptr;
        for (auto& q : regs_) if (q.used && q.x0 == x0 && q.bw == bw && q.id == id) { r = &q; break; }
        if (!r) for (auto& q : regs_) if (!q.used) { q = Reg{}; q.used = true; q.id = id; q.x0 = x0; q.bw = bw; q.lo = x0 + bw; q.hi = x0; q.top = h_; q.bottom = 0; r = &q; break; }
        if (!r) r = &regs_[MaxReg - 1];                       // more regions than slots cannot happen (the engine caps at 12)
        for (int y = y0; y < y0 + count; ++y) {
            const uint16_t* src = pixels + (y - y0) * bw;
            const uint16_t* old = front_ + y * w_ + x0;
            int lo = 0, hi = bw;
            if (valid_) {
                if (std::memcmp(old, src, bw * sizeof(uint16_t)) == 0) hi = 0;
                else { while (lo < hi && old[lo] == src[lo]) ++lo; while (hi > lo && old[hi-1] == src[hi-1]) --hi; }
            }
            if (hi > lo) {
                if (x0 + lo < r->lo) r->lo = x0 + lo;
                if (x0 + hi > r->hi) r->hi = x0 + hi;
                if (y < r->top) r->top = y;
                if (y + 1 > r->bottom) r->bottom = y + 1;
            }
            std::memcpy(back_ + y * w_ + x0, src, bw * sizeof(uint16_t));
        }
        staged_ = true;
    }
    bool valid() const { return valid_; }
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
            for(int row=0;row<m.rows;++row) for(int col=0;col<m.cols;++col) {
                const auto c=m.cells[row*m.cols+col];
                const int x0=col?c.x:0, y0=row?c.y:44;
                const int x1=col==m.cols-1?w_:m.cells[row*m.cols+col+1].x;
                const int y1=row==m.rows-1?h_-44:m.cells[(row+1)*m.cols+col].y;
                emit({x0,y0,x1-x0,y1-y0},send);
            }
        } else emit({0,44,w_,h_-88},send);
        emit({0,h_-44,w_,44},send);
        for (auto& q : regs_) {                                   // staged regions: one rectangle each
            if (!q.used) continue;
            if (q.hi > q.lo && q.bottom > q.top) {
                const uieng::Rect r{q.lo, q.top, q.hi - q.lo, q.bottom - q.top};
                send(r, back_, w_);
                for (int y = r.y; y < r.y + r.h; ++y) std::memcpy(front_ + y*w_ + r.x, back_ + y*w_ + r.x, r.w * sizeof(uint16_t));
            }
            q.used = false;
        }
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
    struct Reg { int x0=0, bw=0, lo=0, hi=0, top=0, bottom=0, id=-1; bool used=false; };
    static constexpr int MaxReg = 12;
    Reg regs_[MaxReg];
    void clearStaged() {
        staged_=false;
        for (auto& q : regs_) q.used = false;
        for(int y=0;y<std::min(h_,MaxRows);++y){left_[y]=w_;right_[y]=0;}
    }
    int compareY0_=0,compareY1_=0;
    uint16_t *front_=nullptr,*back_=nullptr; int w_=0,h_=0; bool valid_=false;
};
}
