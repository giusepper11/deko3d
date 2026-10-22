// Original local correctness probe; project LICENSE applies.
#include "ProbeCore.h"
#include <cstring>

namespace stencil_probe {
bool ValidateConfig(Config const& c) {
    return c.width==Width && c.height==Height && c.rowPitch==PackedPitch &&
        c.stencilPitch==StencilPitch && c.format==DkImageFormat_Z24S8 &&
        c.type==DkImageType_2D && c.mips==1 && c.layers==1 && c.samples==1 && !c.compressed &&
        c.target.x==Target.x && c.target.y==Target.y && c.target.z==0 &&
        c.target.width==Target.width && c.target.height==Target.height && c.target.depth==1;
}
Model MakeModel() {
    Model m{};
    for(size_t i=0;i<size_t(Width)*Height;++i) {
        const uint32_t depth=(0x123456+uint32_t(i)*0x010307)&0xffffff;
        m.expected[0][4*i]=uint8_t(i*37+0x11);
        for(unsigned byte=0;byte<3;++byte) m.expected[0][4*i+1+byte]=uint8_t(depth>>(byte*8));
    }
    for(unsigned update=0;update<2;++update) {
        m.expected[update+1]=m.expected[0]; m.source[update].fill(Poison);
        const uint8_t mask=update==0 ? 0xff : 0x5a;
        for(uint32_t y=0;y<Target.height;++y) for(uint32_t x=0;x<Target.width;++x) {
            const size_t offset=((y+Target.y)*Width+x+Target.x)*4;
            const uint8_t value=m.expected[0][offset]^mask;
            m.source[update][y*StencilPitch+x]=value; m.expected[update+1][offset]=value;
        }
    }
    return m;
}
Comparison Compare(Phase phase,uint8_t const* actual,size_t bytes,Model const& m) {
    Comparison result{}; const unsigned state=static_cast<unsigned>(phase);
    if(!actual || bytes!=PackedBytes || state>=m.expected.size()) return result;
    result.sizeValid=true;
    for(uint32_t y=0;y<Height;++y) for(uint32_t x=0;x<Width;++x) {
        const bool target=state!=0 && x>=Target.x && x<Target.x+Target.width && y>=Target.y && y<Target.y+Target.height;
        for(unsigned byte=0;byte<4;++byte) {
            const size_t offset=(y*Width+x)*4+byte;
            if(actual[offset]==m.expected[state][offset]) continue;
            if(byte==0) ++result.stencilMismatches; else ++result.depthByteMismatches;
            if(!target) ++result.outsideByteMismatches;
        }
    }
    return result;
}
bool PackRows(Packed const& packed,uint8_t* dst,size_t bytes) {
    if(!dst || bytes<PaddedPackedBytes) return false;
    for(uint32_t y=0;y<Height;++y) std::memcpy(dst+y*PackedPitch,packed.data()+y*Width*4,Width*4);
    return true;
}
bool UnpackRows(uint8_t const* src,size_t bytes,Packed& packed) {
    if(!src || bytes<PaddedPackedBytes) return false;
    for(uint32_t y=0;y<Height;++y) std::memcpy(packed.data()+y*Width*4,src+y*PackedPitch,Width*4);
    return true;
}
bool ReadbackGuardsIntact(uint8_t const* src,size_t bytes) {
    if(!src || bytes!=BufferBytes) return false;
    for(size_t i=0;i<bytes;++i) {
        const bool logical=i>=BufferOffset && i<BufferOffset+PaddedPackedBytes &&
            (i-BufferOffset)%PackedPitch<Width*4;
        if(!logical && src[i]!=Guard) return false;
    }
    return true;
}
bool CompletionGate::begin(unsigned step) {
    if(inFlight_ || completed_ || step!=next_ || step>=Steps) return false;
    inFlight_=true; return true;
}
bool CompletionGate::complete(DkResult result,bool queueError) {
    if(!inFlight_ || result!=DkResult_Success || queueError) return false;
    inFlight_=false; completed_=true; return true;
}
bool CompletionGate::advance() {
    if(!canRead()) return false;
    completed_=false; ++next_; return true;
}
}
