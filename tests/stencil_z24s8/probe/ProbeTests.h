// Included by the existing stencil_z24s8 host harness; no second framework.
#include "../../../examples/z24s8_stencil_probe/ProbeCore.h"
TEST(ProbeModelMatchesRecordedFirstFixture) {
    using namespace stencil_probe; Model m=MakeModel();
    CHECK(m.expected[0][0]==0x11 && m.expected[0][1]==0x56 && m.expected[0][2]==0x34 && m.expected[0][3]==0x12);
    CHECK(m.source[0].size()==517 && m.expected[0].size()==836);
    size_t differences=0;
    for(size_t i=0;i<PackedBytes;++i) {
        CHECK(i%4==0 || (m.expected[0][i]==m.expected[1][i] && m.expected[0][i]==m.expected[2][i]));
        differences+=m.expected[0][i]!=m.expected[1][i];
    }
    CHECK(differences==15); CHECK(m.expected[1]!=m.expected[2]);
}
TEST(ProbeReferenceRejectsWrongWByteAndOutOfRectangleWrites) {
    using namespace stencil_probe; auto m=MakeModel();
    CHECK(Compare(Phase::Control,m.expected[0].data(),PackedBytes,m).passed());
    for(auto phase:{Phase::Update1,Phase::Update2}) {
        auto packed=m.expected[static_cast<unsigned>(phase)];
        CHECK(Compare(phase,packed.data(),packed.size(),m).passed());
        packed[((Target.y*Width)+Target.x)*4+3]^=0xff;
        CHECK(Compare(phase,packed.data(),packed.size(),m).depthByteMismatches==1);
        packed[0]^=0x20; CHECK(Compare(phase,packed.data(),packed.size(),m).outsideByteMismatches==1);
    }
    CHECK(!Compare(Phase::Control,nullptr,0,m).passed());
    CHECK(!Compare(Phase::Control,m.expected[0].data(),PackedBytes-1,m).passed());
}
TEST(ProbePaddedReadbackAndGuardBounds) {
    using namespace stencil_probe; auto m=MakeModel();
    std::array<uint8_t,BufferBytes> allocation; allocation.fill(Guard);
    CHECK(PackRows(m.expected[0],allocation.data()+BufferOffset,PaddedPackedBytes));
    Packed actual{}; CHECK(UnpackRows(allocation.data()+BufferOffset,PaddedPackedBytes,actual));
    CHECK(actual==m.expected[0]); CHECK(ReadbackGuardsIntact(allocation.data(),allocation.size()));
    CHECK(!PackRows(actual,allocation.data(),PaddedPackedBytes-1));
    CHECK(!UnpackRows(allocation.data(),PaddedPackedBytes-1,actual));
    CHECK(!ReadbackGuardsIntact(nullptr,BufferBytes));
    allocation[0]^=1; CHECK(!ReadbackGuardsIntact(allocation.data(),allocation.size()));
    allocation[0]^=1; allocation[BufferOffset+Width*4]^=1;
    CHECK(!ReadbackGuardsIntact(allocation.data(),allocation.size()));
}
TEST(ProbeUnsupportedConfigurationsAreRejected) {
    using namespace stencil_probe; Config c; CHECK(ValidateConfig(c));
    c.format=DkImageFormat_S8; CHECK(!ValidateConfig(c)); c=Config{};
    c.type=DkImageType_3D; CHECK(!ValidateConfig(c)); c=Config{};
    c.compressed=true; CHECK(!ValidateConfig(c)); c=Config{};
    c.mips=2; CHECK(!ValidateConfig(c)); c=Config{};
    c.samples=2; CHECK(!ValidateConfig(c)); c=Config{};
    c.layers=2; CHECK(!ValidateConfig(c)); c=Config{};
    c.rowPitch=0; CHECK(!ValidateConfig(c)); c=Config{};
    c.target.x=UINT32_MAX; CHECK(!ValidateConfig(c)); c=Config{};
    c.target.depth=2; CHECK(!ValidateConfig(c));
}
TEST(ProbeCompletionGateEnforcesOrderingAndLifetimes) {
    using namespace stencil_probe; CompletionGate g;
    CHECK(!g.canRead() && g.canDestroy()); CHECK(!g.complete(DkResult_Success,false));
    CHECK(!g.begin(1));
    for(unsigned step=0;step<Steps;++step) {
        CHECK(g.begin(step)); CHECK(!g.canDestroy() && !g.canRead()); CHECK(!g.begin(step));
        CHECK(!g.advance()); CHECK(g.complete(DkResult_Success,false)); CHECK(g.canRead() && g.canDestroy());
        CHECK(g.advance()); CHECK(!g.canRead());
    }
    CHECK(g.nextStep()==Steps && !g.begin(Steps));
}
TEST(ProbeFailedCompletionNeverAuthorizesReadOrCleanup) {
    using namespace stencil_probe;
    for(auto result:{DkResult_Timeout,DkResult_Fail}) {
        CompletionGate g; CHECK(g.begin(0)); CHECK(!g.complete(result,false));
        CHECK(!g.canRead() && !g.canDestroy() && !g.advance() && !g.begin(1));
    }
    CompletionGate g; CHECK(g.begin(0)); CHECK(!g.complete(DkResult_Success,true));
    CHECK(!g.canRead() && !g.canDestroy());
}
