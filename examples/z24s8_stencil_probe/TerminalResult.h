// Narrow terminal-report publication for this probe, shared with host fault tests.
#pragma once
#include "ProbeCore.h"
#include <sys/types.h>
namespace stencil_probe {
enum class ExecutionStage { HostSetup,DeviceCreated,QueueCreationAttempted,QueueCreated,ProbeSubmissionAttempted,VerifiedCompletion };
struct PublicationOps {
    bool (*absent)(char const*);
    int (*openExclusive)(char const*);
    ssize_t (*write)(int,void const*,size_t);
    int (*sync)(int);
    int (*close)(int);
    int (*publish)(char const*,char const*);
};
extern PublicationOps const NativePublicationOps;
// Private, exclusively created run directory; no concurrent publisher is allowed.
// Only result.json is terminal. Failed result.pending files remain incomplete evidence.
bool PublishTerminal(char const* directory,void const* data,size_t bytes,
                     PublicationOps const& operations=NativePublicationOps);
int FormatTerminal(char* destination,size_t capacity,bool passed,unsigned passedSteps,
                   unsigned verifiedSubmissions,ExecutionStage stage,char const* reason);
}
