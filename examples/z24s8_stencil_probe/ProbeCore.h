// Original local correctness probe; project LICENSE applies. No GPU execution here.
#pragma once
#include <deko3d.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace stencil_probe {
constexpr uint32_t Width=19, Height=11, PackedPitch=256, StencilPitch=256;
constexpr DkImageRect Target{3,2,0,5,3,1};
constexpr size_t PackedBytes=size_t(Width)*Height*4;
constexpr size_t PaddedPackedBytes=size_t(PackedPitch)*Height;
constexpr size_t StencilBytes=size_t(StencilPitch)*(Target.height-1)+Target.width;
constexpr unsigned Cycles=2, PhasesPerCycle=3, Steps=Cycles*PhasesPerCycle;
constexpr uint32_t BufferBytes=4096, BufferOffset=256, CommandSlice=16384;
constexpr uint32_t CommandBytes=Steps*CommandSlice;
constexpr int64_t FenceTimeoutNs=5000000000LL;
constexpr uint8_t Poison=0xcd, Guard=0xa5, UnrelatedGuard=0xb6;
enum class Phase : unsigned { Control=0, Update1=1, Update2=2 };
struct Config {
    uint32_t width=Width,height=Height,rowPitch=PackedPitch,stencilPitch=StencilPitch;
    DkImageFormat format=DkImageFormat_Z24S8;
    DkImageType type=DkImageType_2D;
    uint32_t mips=1,layers=1,samples=1;
    bool compressed=false;
    DkImageRect target=Target;
};
using Packed=std::array<uint8_t,PackedBytes>;
using Stencil=std::array<uint8_t,StencilBytes>;
struct Model { std::array<Packed,3> expected; std::array<Stencil,2> source; };
struct Comparison {
    size_t stencilMismatches=0,depthByteMismatches=0,outsideByteMismatches=0;
    bool sizeValid=false;
    bool passed() const { return sizeValid && !stencilMismatches && !depthByteMismatches && !outsideByteMismatches; }
};
bool ValidateConfig(Config const& config);
Model MakeModel();
Comparison Compare(Phase phase,uint8_t const* actual,size_t bytes,Model const& model);
bool PackRows(Packed const& packed,uint8_t* destination,size_t bytes);
bool UnpackRows(uint8_t const* source,size_t bytes,Packed& packed);
bool ReadbackGuardsIntact(uint8_t const* allocation,size_t bytes);

// Shared lifecycle gate used by the real adapter and host failure-path tests.
// A failed wait/queue error never authorizes CPU readback or resource destruction.
class CompletionGate {
    unsigned next_=0;
    bool inFlight_=false,completed_=false;
public:
    bool begin(unsigned step);
    bool complete(DkResult waitResult,bool queueError);
    bool advance();
    bool canRead() const { return completed_ && !inFlight_; }
    bool canDestroy() const { return !inFlight_; }
    unsigned nextStep() const { return next_; }
};
}
