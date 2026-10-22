// Preparation artifact only. Deployment/execution requires separate explicit approval.
// Raw SDK handles intentionally have no RAII destructor: uncertain GPU completion
// must never trigger deko's unbounded queue destruction or early memory unmapping.
#include <switch.h>
#include <deko3d.h>
#include <deko3d_aspect_copy.h>
#include "ProbeCore.h"
#include "TerminalResult.h"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PROBE_SOURCE_ID
#error "Build must identify its source"
#endif
#ifndef PROTOTYPE_LIBRARY_SHA256
#error "Build must identify the actual linked experimental library"
#endif
using namespace stencil_probe;

namespace {
struct Output {
    char directory[256]{};
    FILE* log=nullptr;
    ExecutionStage stage=ExecutionStage::HostSetup;
    unsigned verifiedSubmissions=0;
    bool initialize() {
        for(auto path:{"sdmc:/switch","sdmc:/switch/z24s8-stencil-probe-results"})
            if(mkdir(path,0700)!=0 && errno!=EEXIST) return false;
        int n=std::snprintf(directory,sizeof(directory),"sdmc:/switch/z24s8-stencil-probe-results/run-%016llx",
                            static_cast<unsigned long long>(armGetSystemTick()));
        if(n<0 || size_t(n)>=sizeof(directory) || mkdir(directory,0700)!=0) return false;
        char path[320]; std::snprintf(path,sizeof(path),"%s/progress.log",directory);
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600); if(fd<0) return false;
        log=fdopen(fd,"w"); if(!log) { close(fd); return false; }
        return true;
    }
    bool line(char const* format,...) {
        if(!log) return false;
        va_list args;va_start(args,format); const int n=std::vfprintf(log,format,args);va_end(args);
        return n>=0 && std::fputc('\n',log)!=EOF && std::fflush(log)==0 && fsync(fileno(log))==0;
    }
    bool blob(char const* name,void const* data,size_t bytes) {
        char path[384];int n=std::snprintf(path,sizeof(path),"%s/%s",directory,name);
        if(n<0 || size_t(n)>=sizeof(path)) return false;
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0) return false;
        auto p=static_cast<uint8_t const*>(data);size_t written=0;
        while(written<bytes) { ssize_t r=write(fd,p+written,bytes-written);if(r<=0) { close(fd);return false; } written+=size_t(r); }
        bool ok=fsync(fd)==0; if(close(fd)!=0) ok=false; return ok;
    }
    bool summary(bool passed,unsigned completed,char const* reason) {
        char text[512];int n=FormatTerminal(text,sizeof(text),passed,completed,verifiedSubmissions,stage,reason);
        return n>0 && PublishTerminal(directory,text,size_t(n));
    }
};
struct Probe {
    Output output;
    Model model=MakeModel();
    CompletionGate gate;
    DkDevice device=nullptr;
    DkQueue queue=nullptr;
    DkCmdBuf commands=nullptr;
    DkMemBlock imageMemory=nullptr,commandMemory=nullptr,unrelated=nullptr;
    std::array<DkMemBlock,3> uploads{};
    std::array<DkMemBlock,Steps> readbacks{};
    std::array<std::array<uint8_t,BufferBytes>,3> uploadExpected{};
    DkImage image{};
    DkImageView view{};
    DkFence fence{}; // Retained for the whole process, never an empty-fence wait.
    uint32_t imageAlignment=0,imageSize=0,imageAllocation=0;
    bool ioError=false;
    [[noreturn]] void failStop(char const* reason) {
        output.line("FAIL_STOP reason=%s in_flight=%d next_step=%u; NO READBACK OR MANUAL GPU CLEANUP",
                    reason,!gate.canDestroy(),gate.nextStep());
        output.summary(false,gate.nextStep(),reason);
        // Process termination is fail-stop, NOT a claim of safe GPU-hang recovery.
        // Do not unwind GPU owners, waitIdle, unmap allocations or recycle memory.
        svcExitProcess();
        __builtin_unreachable();
    }
    static void debug(void* data,char const* context,DkResult result,char const* message) {
        auto& p=*static_cast<Probe*>(data);
        p.ioError |= !p.output.line("DEKO context=%s result=%d message=%s",context?context:"",int(result),message?message:"");
        if(result!=DkResult_Success) p.failStop("deko_callback_failure");
    }
    static void exhausted(void* data,DkCmdBuf,size_t) { static_cast<Probe*>(data)->failStop("command_memory_exhausted"); }
    static void cpuBarrier() { __asm__ __volatile__("dsb sy" ::: "memory"); }
    DkMemBlock allocate(uint32_t bytes,uint32_t flags) {
        DkMemBlockMaker maker;dkMemBlockMakerDefaults(&maker,device,bytes);maker.flags=flags;
        return dkMemBlockCreate(&maker);
    }
    static uint8_t* cpu(DkMemBlock block) { return static_cast<uint8_t*>(dkMemBlockGetCpuAddr(block)); }
    static DkGpuAddr gpu(DkMemBlock block) { return dkMemBlockGetGpuAddr(block)+BufferOffset; }
    bool initialize() {
        if(!ValidateConfig(Config{})) return false;
        if(!output.line("IN_PROGRESS probe_source=%s prototype=cf53748dd02f007758e1518d8c49610eef117671 base=373bfd43d8b2d805fa2eb7d406dbb823032df958",
                        PROBE_SOURCE_ID) || !output.line("library_sha256=%s timeout_ns=%lld cycles=%u steps=%u",
                        PROTOTYPE_LIBRARY_SHA256,static_cast<long long>(FenceTimeoutNs),Cycles,Steps)) return false;
        for(unsigned i=0;i<3;++i) {
            char name[64];std::snprintf(name,sizeof(name),"expected-%u-packed.bin",i);
            if(!output.blob(name,model.expected[i].data(),PackedBytes)) return false;
        }
        for(unsigned i=0;i<2;++i) {
            char name[64];std::snprintf(name,sizeof(name),"stencil-source-%u.bin",i+1);
            if(!output.blob(name,model.source[i].data(),StencilBytes)) return false;
        }
        DkDeviceMaker maker;dkDeviceMakerDefaults(&maker);maker.userData=this;maker.cbDebug=debug;
        device=dkDeviceCreate(&maker);if(!device) return false;
        output.stage=ExecutionStage::DeviceCreated;
        DkImageLayoutMaker layoutMaker;dkImageLayoutMakerDefaults(&layoutMaker,device);
        layoutMaker.type=DkImageType_2D;layoutMaker.format=DkImageFormat_Z24S8;
        layoutMaker.dimensions[0]=Width;layoutMaker.dimensions[1]=Height;layoutMaker.dimensions[2]=1;
        layoutMaker.flags=0;layoutMaker.mipLevels=1;layoutMaker.msMode=DkMsMode_1x;
        DkImageLayout layout;dkImageLayoutInitialize(&layout,&layoutMaker);
        const uint64_t size=dkImageLayoutGetSize(&layout);imageAlignment=dkImageLayoutGetAlignment(&layout);
        if(!size || size>1048576 || imageAlignment<DK_MEMBLOCK_ALIGNMENT || imageAlignment>1048576 ||
           (imageAlignment&(imageAlignment-1)) || size%imageAlignment) return false;
        imageSize=uint32_t(size);imageAllocation=imageSize+2*imageAlignment;
        imageMemory=allocate(imageAllocation,DkMemBlockFlags_Image|DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
        if(!imageMemory || !cpu(imageMemory)) return false;
        // CPU accesses only guard pages, never the image body or a generic GPU alias.
        std::memset(cpu(imageMemory),Guard,imageAlignment);
        std::memset(cpu(imageMemory)+imageAlignment+imageSize,Guard,imageAlignment);
        dkImageInitialize(&image,&layout,imageMemory,imageAlignment);dkImageViewDefaults(&view,&image);
        for(unsigned i=0;i<uploads.size();++i) {
            uploads[i]=allocate(BufferBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached);
            if(!uploads[i] || !cpu(uploads[i])) return false;
            auto& bytes=uploadExpected[i];bytes.fill(Guard);
            if(i==0) { std::memset(bytes.data()+BufferOffset,Poison,PaddedPackedBytes);
                if(!PackRows(model.expected[0],bytes.data()+BufferOffset,PaddedPackedBytes)) return false; }
            else std::memcpy(bytes.data()+BufferOffset,model.source[i-1].data(),StencilBytes);
            std::memcpy(cpu(uploads[i]),bytes.data(),bytes.size());
            char name[64];std::snprintf(name,sizeof(name),"upload-allocation-%u-before.bin",i);
            if(!output.blob(name,bytes.data(),bytes.size())) return false;
        }
        for(auto& block:readbacks) {
            block=allocate(BufferBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached);
            if(!block || !cpu(block)) return false;
            std::memset(cpu(block),Guard,BufferBytes);
        }
        unrelated=allocate(BufferBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuUncached);
        commandMemory=allocate(CommandBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
        if(!unrelated || !commandMemory || !cpu(unrelated) || !cpu(commandMemory)) return false;
        std::memset(cpu(unrelated),UnrelatedGuard,BufferBytes);std::memset(cpu(commandMemory),0,CommandBytes);
        DkCmdBufMaker commandMaker;dkCmdBufMakerDefaults(&commandMaker,device);
        commandMaker.userData=this;commandMaker.cbAddMem=exhausted;
        commands=dkCmdBufCreate(&commandMaker);if(!commands) return false;
        if(!output.line("ALLOC image_bytes=%u alignment=%u guarded_image_bytes=%u upload_bytes=%u readback_bytes=%u command_bytes=%u unrelated_bytes=%u",
                        imageSize,imageAlignment,imageAllocation,unsigned(uploads.size()*BufferBytes),unsigned(readbacks.size()*BufferBytes),CommandBytes,BufferBytes)) return false;
        DkQueueMaker queueMaker;dkQueueMakerDefaults(&queueMaker,device);
        // Transfer-only queue: engines are bound, but no shaders, scratch or Zcull allocation.
        queueMaker.flags=DkQueueFlags_DisableZcull|DkQueueFlags_LowPrio;
        queueMaker.commandMemorySize=DK_QUEUE_MIN_CMDMEM_SIZE;
        queueMaker.flushThreshold=DK_QUEUE_MIN_CMDMEM_SIZE/8;
        if(!output.line("CREATE_QUEUE transfer_only=1; all allocations retained until process termination")) return false;
        output.stage=ExecutionStage::QueueCreationAttempted;
        queue=dkQueueCreate(&queueMaker);
        if(queue) output.stage=ExecutionStage::QueueCreated;
        return queue && !dkQueueIsInErrorState(queue);
    }
    static void barrier(DkCmdBuf cmd) {
        // No graphics/depth operations or Zcull context exist in this transfer-only probe.
        dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_L2Cache|DkInvalidateFlags_Image);
    }
    bool runStep(unsigned step) {
        const unsigned state=step%PhasesPerCycle; const Phase phase=static_cast<Phase>(state);
        if(!output.line("BEGIN cycle=%u phase=%u step=%u",step/PhasesPerCycle,state,step)) return false;
        if(dkQueueIsInErrorState(queue)) failStop("queue_error_before_submission");
        dkCmdBufAddMemory(commands,commandMemory,step*CommandSlice,CommandSlice);
        barrier(commands);
        if(state==0) {
            DkCopyBuf src{gpu(uploads[0]),PackedPitch,uint32_t(PaddedPackedBytes)};
            const DkImageRect rect{0,0,0,Width,Height,1};
            dkCmdBufCopyBufferToImage(commands,&src,&view,&rect,0);
        } else {
            DkCopyBuf src{gpu(uploads[state]),StencilPitch,StencilPitch*Target.height};
            const DkResult result=dkCmdBufCopyBufferToImageStencilZ24S8Experimental(commands,&src,StencilBytes,&view,&Target);
            if(!output.line("EXPERIMENT encode_result=%d remap=0x03006660 src_components=1 dst_components=4",int(result))) return false;
            if(result!=DkResult_Success) return false;
        }
        barrier(commands);
        DkCopyBuf destination{gpu(readbacks[step]),PackedPitch,uint32_t(PaddedPackedBytes)};
        const DkImageRect full{0,0,0,Width,Height,1};
        // Independent existing full-packed readback, flags0: CE remapping disabled.
        dkCmdBufCopyImageToBuffer(commands,&view,&full,&destination,0);
        barrier(commands);
        DkCmdList list=dkCmdBufFinishList(commands);
        char name[96];std::snprintf(name,sizeof(name),"step-%u-command-slice.bin",step);
        if(!output.blob(name,cpu(commandMemory)+step*CommandSlice,CommandSlice)) return false;
        cpuBarrier();
        if(!gate.begin(step)) return false;
        output.stage=ExecutionStage::ProbeSubmissionAttempted;
        // Signal exactly this submission; keep the fence object and all storage alive.
        dkQueueSubmitCommands(queue,list);dkQueueSignalFence(queue,&fence,true);dkQueueFlush(queue);
        const DkResult wait=dkFenceWait(&fence,FenceTimeoutNs);
        const bool error=dkQueueIsInErrorState(queue);
        ioError |= !output.line("COMPLETION wait=%d queue_error=%d",int(wait),error);
        if(!gate.complete(wait,error)) failStop("unverified_gpu_completion");
        ++output.verifiedSubmissions;output.stage=ExecutionStage::VerifiedCompletion;
        cpuBarrier(); // CPU-uncached readback; no unsupported invalidate-only API assumed.
        Packed actual{};
        if(!gate.canRead() || !UnpackRows(cpu(readbacks[step])+BufferOffset,PaddedPackedBytes,actual)) return false;
        auto comparison=Compare(phase,actual.data(),actual.size(),model);
        const bool rbGuard=ReadbackGuardsIntact(cpu(readbacks[step]),BufferBytes);
        bool inputs=true,imageGuards=true,unrelatedGuard=true;
        for(unsigned i=0;i<uploads.size();++i) inputs &= std::memcmp(cpu(uploads[i]),uploadExpected[i].data(),BufferBytes)==0;
        for(uint32_t i=0;i<imageAlignment;++i)
            imageGuards &= cpu(imageMemory)[i]==Guard && cpu(imageMemory)[imageAlignment+imageSize+i]==Guard;
        for(uint32_t i=0;i<BufferBytes;++i) unrelatedGuard &= cpu(unrelated)[i]==UnrelatedGuard;
        std::snprintf(name,sizeof(name),"step-%u-actual-packed.bin",step);
        ioError |= !output.blob(name,actual.data(),actual.size());
        std::snprintf(name,sizeof(name),"step-%u-readback-allocation.bin",step);
        ioError |= !output.blob(name,cpu(readbacks[step]),BufferBytes);
        std::snprintf(name,sizeof(name),"step-%u-image-prefix-guard.bin",step);
        ioError |= !output.blob(name,cpu(imageMemory),imageAlignment);
        std::snprintf(name,sizeof(name),"step-%u-image-suffix-guard.bin",step);
        ioError |= !output.blob(name,cpu(imageMemory)+imageAlignment+imageSize,imageAlignment);
        ioError |= !output.line("COMPARE cycle=%u phase=%u stencil_mismatch=%zu depth_byte_mismatch=%zu outside_byte_mismatch=%zu readback_guards=%d input_allocations=%d image_guards=%d unrelated_guard=%d",
            step/PhasesPerCycle,state,comparison.stencilMismatches,comparison.depthByteMismatches,
            comparison.outsideByteMismatches,rbGuard,inputs,imageGuards,unrelatedGuard);
        if(!comparison.passed() || !rbGuard || !inputs || !imageGuards || !unrelatedGuard || ioError) return false;
        return gate.advance(); // Control mismatch stops before ANY experimental submission.
    }
};
}
int main() {
    // No console framebuffer, game, sockets, clock changes, stress loop or GPU owner RAII.
    static Probe probe;
    if(!probe.output.initialize()) return 1;
    bool passed=probe.initialize();
    for(unsigned step=0;passed && step<Steps;++step) passed=probe.runStep(step);
    if(!probe.gate.canDestroy()) probe.failStop("in_flight_at_exit");
    passed=passed && probe.gate.nextStep()==Steps;
    passed=probe.output.line("END comparisons=%s completed_steps=%u; retain GPU handles until process exit, never call unbounded waitIdle cleanup",
                            passed ? "PASS":"FAIL",probe.gate.nextStep()) && passed;
    if(probe.output.log && std::fclose(probe.output.log)!=0) passed=false;
    probe.output.log=nullptr;
    const bool finalWritten=probe.output.summary(passed,probe.gate.nextStep(),passed?"all_comparisons_passed":"initialization_comparison_or_io_failure");
    // Process-owned resources are deliberately retained through termination. All normal
    // paths here follow verified completion; timeout/error used failStop without reads.
    return passed && finalWritten ? 0:1;
}
