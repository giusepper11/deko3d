// Local research tests: packed-byte model and Maxwell command encoding, not GPU execution.
#include <deko3d.h>
#include "maxwell/stencil_z24s8.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace dk::detail;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #x); } while (0)
using Test = std::pair<const char*, std::function<void()>>;
std::vector<Test>& tests() { static std::vector<Test> value; return value; }
#define TEST(name) void name(); const bool reg_##name = (tests().push_back({#name, name}), true); void name()

struct Fixture {
    DkImage opaque{};
    DkImageView view{};
    DkCopyBuf source{0x123450000ULL, 256, 0};
    DkImageRect rect{3, 2, 0, 5, 3, 1};
    StencilZ24S8Image image{DkImageType_2D, DkImageFormat_Z24S8, 0, 19, 11,
                          1, 0, 0, 1, 0, true, 0x234560000ULL, 2048};
    StencilZ24S8Copy plan{};
    uint64_t size = 517;
    Fixture() { dkImageViewDefaults(&view, &opaque); }
    DkResult prepare() { return PrepareStencilZ24S8Copy(image, &source, size, &view, &rect, plan); }
    auto encode() { CHECK(prepare() == DkResult_Success); return EncodeStencilZ24S8Copy(plan); }
};
struct Method { uint32_t reg, value; };
template <uint32_t N> std::vector<Method> decode(const maxwell::CmdList<N>& commands) {
    std::vector<Method> out;
    for (uint32_t i=0; i<N;) {
        uint32_t header=commands.raw[i++].i;
        CHECK(((header>>13)&7)==4); // Copy subchannel, not 3D/2D.
        uint32_t mode=header>>29, reg=header&0x1fff, count=(header>>16)&0x1fff;
        if (mode==4) { out.push_back({reg,count}); continue; }
        CHECK(mode==1 && count && count<=N-i);
        for (uint32_t j=0;j<count;++j) out.push_back({reg+j,commands.raw[i++].i});
    }
    return out;
}
uint32_t value(const std::vector<Method>& cmds,uint32_t reg) {
    auto it=std::find_if(cmds.begin(),cmds.end(),[&](Method m){return m.reg==reg;});
    CHECK(it!=cmds.end()); return it->value;
}
TEST(ExactCommandWordsAndOrdering) {
    Fixture f; auto encoded=f.encode(); CHECK(encoded.getSize()==19);
    // Independent literal Maxwell pushbuffer golden: one-byte components, Src1/Dst4,
    // X<-SrcX, Y/Z/W NoWrite; origin X is texels here, NOT X*4.
    const uint32_t expected[]={
        0x200181c2,0x03006660,
        0x200681c3,0x00001010,32,11,1,0,0x00020003,
        0x20088100,1,0x23450000,2,0x34560000,256,0,5,3,
        0x868680c0};
    for (uint32_t i=0;i<encoded.getSize();++i) CHECK(encoded.raw[i].i==expected[i]);
    auto methods=decode(encoded); CHECK(methods.front().reg==0x1c2); CHECK(methods.back().reg==0xc0);
}
TEST(ComponentMappingAndCounts) {
    Fixture f; auto m=decode(f.encode()); uint32_t r=value(m,0x1c2);
    CHECK((r&7)==0); CHECK(((r>>4)&7)==6); CHECK(((r>>8)&7)==6); CHECK(((r>>12)&7)==6);
    CHECK(((r>>16)&3)==0); CHECK(((r>>20)&3)==0); CHECK(((r>>24)&3)==3);
    // Negative counterfactual: W selection from PIPE_FORMAT_Z24_UNORM_S8_UINT is NOT deko Z24S8.
    CHECK(r!=0x03000666);
}
TEST(LaunchFlagsPreserveRmw) {
    Fixture f; uint32_t launch=value(decode(f.encode()),0xc0);
    CHECK(launch==0x686); CHECK((launch&3)==2); CHECK(launch&(1u<<2));
    CHECK(launch&(1u<<7)); CHECK(!(launch&(1u<<8))); CHECK(launch&(1u<<9));
    CHECK(launch&(1u<<10)); CHECK(!(launch&(1u<<11))); // FORCE_RMW_DISABLE remains false.
}
TEST(DestinationGeometryIsRemapElementUnits) {
    Fixture f; auto m=decode(f.encode());
    CHECK(value(m,0x1c4)==32); CHECK(value(m,0x1c5)==11);
    CHECK(value(m,0x1c6)==1); CHECK(value(m,0x1c7)==0);
    CHECK(value(m,0x1c8)==(2u<<16|3u)); CHECK(value(m,0x106)==5); CHECK(value(m,0x107)==3);
}
TEST(FinalRowDoesNotRequirePadding) {
    Fixture f; CHECK(f.prepare()==DkResult_Success); CHECK(f.plan.requiredSourceBytes==517);
    --f.size; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(ExplicitImagePitchAndSourceOffsetInAddress) {
    Fixture f; f.source.addr+=256; f.source.imageHeight=768;
    CHECK(f.prepare()==DkResult_Success); CHECK(f.plan.source==f.source.addr);
    CHECK(f.plan.sourceImagePitch==768); CHECK(f.plan.rowPitch==256);
    auto m=decode(EncodeStencilZ24S8Copy(f.plan)); CHECK(value(m,0x101)==0x23450100);
}
TEST(MinimalOneTexelAndRepeatedModelPreserveDepth) {
    Fixture f; f.rect={0,0,0,1,1,1}; f.size=1;
    CHECK(f.prepare()==DkResult_Success); CHECK(f.plan.requiredSourceBytes==1);
    std::array<uint8_t,4> packed={0x11,0x56,0x34,0x12};
    uint32_t remap=value(decode(EncodeStencilZ24S8Copy(f.plan)),0x1c2);
    for (uint8_t next : {uint8_t(0x20),uint8_t(0xe1),uint8_t(0)}) {
        for (unsigned c=0;c<4;++c) {
            unsigned selector=(remap>>(4*c))&7;
            if (selector==0) packed[c]=next;
            else CHECK(selector==6); // reference NoWrite retains prior byte
        }
        CHECK(packed[0]==next); CHECK(packed[1]==0x56); CHECK(packed[2]==0x34); CHECK(packed[3]==0x12);
    }
}
TEST(MultirowModelKeepsDepthAndUntouchedTexels) {
    Fixture f; auto encoded=f.encode(); auto m=decode(encoded);
    uint32_t r=value(m,0x1c2), pitch=value(m,0x104), w=value(m,0x106),h=value(m,0x107);
    std::vector<uint8_t> source(f.size,0xdd), packed(19*11*4);
    for (size_t i=0;i<packed.size();++i) packed[i]=uint8_t(i*37+11);
    auto before=packed;
    for (uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) source[y*pitch+x]=uint8_t(71+y*9+x);
    for (uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) for(unsigned c=0;c<4;++c) {
        unsigned selector=(r>>(4*c))&7; size_t dst=((y+2)*19+x+3)*4+c;
        if(selector==0) packed[dst]=source[y*pitch+x]; else CHECK(selector==6);
    }
    for(uint32_t y=0;y<11;++y) for(uint32_t x=0;x<19;++x) for(unsigned c=0;c<4;++c) {
        size_t i=(y*19+x)*4+c;
        bool changed=c==0 && y>=2 && y<5 && x>=3 && x<8;
        CHECK(packed[i]==(changed ? source[(y-2)*pitch+x-3] : before[i]));
    }
    // This is a linear logical-texel model. It does NOT emulate hardware block-linear swizzling.
}
TEST(RejectNullParameters) {
    Fixture f;
    CHECK(PrepareStencilZ24S8Copy(f.image,nullptr,f.size,&f.view,&f.rect,f.plan)==DkResult_BadInput);
    CHECK(PrepareStencilZ24S8Copy(f.image,&f.source,f.size,nullptr,&f.rect,f.plan)==DkResult_BadInput);
    CHECK(PrepareStencilZ24S8Copy(f.image,&f.source,f.size,&f.view,nullptr,f.plan)==DkResult_BadInput);
    f.view.pImage=nullptr; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectOtherFormatsAndReinterpretation) {
    for(auto format : {DkImageFormat_Z16,DkImageFormat_S8,DkImageFormat_ZF32_X24S8,DkImageFormat_RGBA8_Unorm,DkImageFormat_None}) {
        Fixture f; f.image.format=format; CHECK(f.prepare()==DkResult_NotImplemented);
    }
    Fixture f; f.view.format=DkImageFormat_RGBA8_Unorm; CHECK(f.prepare()==DkResult_NotImplemented);
    f.view.format=DkImageFormat_Z24S8; CHECK(f.prepare()==DkResult_Success);
}
TEST(RejectTypesAndViewOverrides) {
    for(auto type : {DkImageType_3D,DkImageType_2DArray,DkImageType_Cubemap,DkImageType_2DMS}) {
        Fixture f; f.image.type=type; CHECK(f.prepare()==DkResult_NotImplemented);
    }
    Fixture f; f.view.type=DkImageType_2DArray; CHECK(f.prepare()==DkResult_NotImplemented);
}
TEST(RejectMipsLayersAndSamples) {
    Fixture f; f.image.mipLevels=2; CHECK(f.prepare()==DkResult_NotImplemented);
    f=Fixture{}; f.view.pImage=&f.opaque; f.view.mipLevelOffset=1; CHECK(f.prepare()==DkResult_NotImplemented);
    f.view.mipLevelOffset=0; f.view.layerOffset=1; CHECK(f.prepare()==DkResult_NotImplemented);
    f.view.layerOffset=0; f.view.layerCount=2; CHECK(f.prepare()==DkResult_NotImplemented);
    f.view.layerCount=0; f.image.samplesLog2=1; CHECK(f.prepare()==DkResult_NotImplemented);
}
TEST(RejectLinearCompressedAndWrongKind) {
    for(uint32_t flag : {uint32_t(DkImageFlags_PitchLinear),uint32_t(DkImageFlags_HwCompression)}) {
        Fixture f; f.image.flags=flag; CHECK(f.prepare()==DkResult_NotImplemented);
    }
    Fixture f; f.image.uncompressedZ24S8Kind=false; CHECK(f.prepare()==DkResult_NotImplemented);
}
TEST(RejectUnsupportedTiling) {
    Fixture f; f.image.tileW=1; CHECK(f.prepare()==DkResult_NotImplemented);
    f.image.tileW=0; f.image.tileD=1; CHECK(f.prepare()==DkResult_NotImplemented);
    f.image.tileD=0; f.image.tileH=6; CHECK(f.prepare()==DkResult_NotImplemented);
}
TEST(RejectBadSourcePitchAndImagePitch) {
    Fixture f; f.source.rowLength=0; CHECK(f.prepare()==DkResult_BadInput);
    f.source.rowLength=255; CHECK(f.prepare()==DkResult_MisalignedSize);
    f.source.rowLength=256; f.rect.width=257; f.image.width=512; CHECK(f.prepare()==DkResult_BadInput);
    f.rect.width=5; f.source.imageHeight=769; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectInvalidRegionsWithoutOverflow) {
    for(auto r : {DkImageRect{0,0,0,0,1,1},DkImageRect{0,0,0,1,0,1},DkImageRect{0,0,1,1,1,1},
                  DkImageRect{0,0,0,1,1,2},DkImageRect{18,0,0,2,1,1},DkImageRect{0,10,0,1,2,1},
                  DkImageRect{UINT32_MAX,0,0,2,1,1}}) {
        Fixture f; f.rect=r; CHECK(f.prepare()==DkResult_BadInput);
    }
}
TEST(RejectAddressAndAllocationOverflow) {
    Fixture f; f.source.addr=DK_GPU_ADDR_INVALID; CHECK(f.prepare()==DkResult_BadInput);
    f.source.addr=0x123450001ULL; CHECK(f.prepare()==DkResult_MisalignedData);
    f.source.addr=(1ULL<<40)-256; CHECK(f.prepare()==DkResult_BadInput);
    f.source.addr=0x123450000ULL; f.size=UINT64_MAX; CHECK(f.prepare()==DkResult_BadInput);
    f.size=517; f.image.address=0x234560001ULL; CHECK(f.prepare()==DkResult_MisalignedData);
    f.image.address=0x234560000ULL; f.image.storageSize=1; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectOversizeDimensions) {
    Fixture f; f.image.width=0; CHECK(f.prepare()==DkResult_BadInput);
    f.image.width=16385; CHECK(f.prepare()==DkResult_BadInput);
    f.image.width=19; f.image.height=16385; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(AllowedTileHeightsAndNoInheritedRemapState) {
    for(uint32_t tileH=0;tileH<=5;++tileH) {
        Fixture f; f.image.tileH=tileH; f.image.storageSize=65536;
        auto a=f.encode(); auto b=f.encode(); CHECK(value(decode(a),0x1c3)==(0x1000u|(tileH<<4)));
        CHECK(a.raw[0].i==b.raw[0].i && a.raw[1].i==b.raw[1].i);
    }
}
TEST(RejectOverlapButAllowExactlyAdjacentRanges) {
    Fixture f; f.source.addr=f.image.address; CHECK(f.prepare()==DkResult_BadInput);
    f.source.addr=f.image.address+f.image.storageSize; CHECK(f.prepare()==DkResult_Success);
    f.source.addr=f.image.address-256; f.rect={0,0,0,1,1,1}; f.size=256;
    CHECK(f.prepare()==DkResult_Success);
    f.size=257; CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectImagePitchAboveUint32WithoutNarrowing) {
    Fixture f; f.source.addr=0x1000000000ULL; f.source.rowLength=0xffffff00U;
    f.size=uint64_t(f.source.rowLength)*f.rect.height;
    CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectDestinationAddressRangeOverflow) {
    Fixture f; f.image.address=(1ULL<<40)-512;
    CHECK(f.prepare()==DkResult_BadInput);
}
TEST(RejectExplicitMultipleMipLevelsInView) {
    Fixture f; f.view.mipLevelCount=2;
    CHECK(f.prepare()==DkResult_NotImplemented);
}
TEST(InvalidPlanLeavesPreviousPlanUnchanged) {
    Fixture f; CHECK(f.prepare()==DkResult_Success); auto previous=f.plan;
    f.rect.x=UINT32_MAX; CHECK(f.prepare()==DkResult_BadInput);
    CHECK(f.plan.source==previous.source && f.plan.destination==previous.destination);
    CHECK(f.plan.width==previous.width && f.plan.x==previous.x);
    CHECK(f.plan.requiredSourceBytes==previous.requiredSourceBytes);
}
#include "probe/ProbeTests.h"
#include "probe/PublicationTests.h"
int main(int argc, char** argv) {
    const bool probeOnly=argc==2 && std::string(argv[1])=="--probe-only";
    unsigned failed=0,executed=0;
    for(auto& test:tests()) {
        if (probeOnly && std::string(test.first).rfind("Probe",0)!=0) continue;
        ++executed;
        try { test.second(); std::printf("PASS %s\n",test.first); }
        catch(const std::exception& e) { ++failed; std::printf("FAIL %s: %s\n",test.first,e.what()); }
    }
    std::printf("%u tests, %u failures; host/model only, no GPU execution\n",executed,failed);
    return failed ? 1 : 0;
}
