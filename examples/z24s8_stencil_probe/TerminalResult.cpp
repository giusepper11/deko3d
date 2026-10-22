// Original local probe helper. Project LICENSE applies.
#include "TerminalResult.h"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace stencil_probe {
namespace {
bool absent(char const* path) { struct stat info{};return ::stat(path,&info)!=0 && errno==ENOENT; }
int openExclusive(char const* path) { return ::open(path,O_WRONLY|O_CREAT|O_EXCL,0600); }
char const* stageName(ExecutionStage stage) {
    switch(stage) {
    case ExecutionStage::HostSetup:return "host_setup";
    case ExecutionStage::DeviceCreated:return "device_created";
    case ExecutionStage::QueueCreationAttempted:return "queue_creation_attempted";
    case ExecutionStage::QueueCreated:return "queue_created";
    case ExecutionStage::ProbeSubmissionAttempted:return "probe_submission_attempted";
    case ExecutionStage::VerifiedCompletion:return "verified_probe_completion";
    }
    return nullptr;
}
}
PublicationOps const NativePublicationOps{absent,openExclusive,::write,::fsync,::close,::rename};

bool PublishTerminal(char const* directory,void const* data,size_t bytes,PublicationOps const& ops) {
    if(!directory || !directory[0] || !data || !bytes) return false;
    char pending[384],final[384];
    int a=std::snprintf(pending,sizeof(pending),"%s/result.pending",directory);
    int b=std::snprintf(final,sizeof(final),"%s/result.json",directory);
    if(a<0 || b<0 || size_t(a)>=sizeof(pending) || size_t(b)>=sizeof(final) || !ops.absent(final)) return false;
    int fd=ops.openExclusive(pending);if(fd<0) return false;
    auto p=static_cast<uint8_t const*>(data);size_t written=0;
    while(written<bytes) {
        const ssize_t n=ops.write(fd,p+written,bytes-written);
        if(n<=0 || size_t(n)>bytes-written) { ops.close(fd);return false; }
        written+=size_t(n);
    }
    if(ops.sync(fd)!=0) { ops.close(fd);return false; }
    if(ops.close(fd)!=0 || !ops.absent(final)) return false;
    // Last fallible operation: no failure after successful publication can turn
    // a reported failure into a valid-looking final PASS. The private fresh run
    // directory has exactly one publisher; this is not a concurrent-filesystem API.
    // Leave pending files on every failure. Never accept pending as terminal data.
    return ops.publish(pending,final)==0;
}
int FormatTerminal(char* dst,size_t capacity,bool passed,unsigned passedSteps,
                   unsigned verified,ExecutionStage stage,char const* reason) {
    char const* name=stageName(stage);
    const bool attempted=stage==ExecutionStage::ProbeSubmissionAttempted || stage==ExecutionStage::VerifiedCompletion;
    if(!dst || !capacity || !name || !reason || !reason[0] || passedSteps>verified || verified>Steps ||
       (verified && !attempted) || (passed && (passedSteps!=Steps || verified!=Steps || stage!=ExecutionStage::VerifiedCompletion))) return 0;
    for(auto p=reason;*p;++p) if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_')) return 0;
    const int n=std::snprintf(dst,capacity,
        "{\"schema_version\":2,\"status\":\"%s\",\"completed_steps\":%u,\"required_steps\":%u,\"verified_submissions\":%u,\"execution_stage\":\"%s\",\"probe_submission_attempted\":%s,\"reason\":\"%s\"}\n",
        passed?"PASS":"FAIL",passedSteps,Steps,verified,name,attempted?"true":"false",reason);
    return n>0 && size_t(n)<capacity ? n : 0;
}
}
