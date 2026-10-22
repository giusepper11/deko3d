// Host POSIX fault injection around the exact terminal-publisher used in the NRO.
#include "../../../examples/z24s8_stencil_probe/TerminalResult.h"
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <unistd.h>
namespace publication_test {
using namespace stencil_probe;
struct Fixture {
    static inline Fixture* current=nullptr;
    std::string directory;
    std::vector<std::string> calls;
    std::string fault;
    unsigned writes=0;
    Fixture() {
        CHECK(std::getenv("TMPDIR")); char pattern[1024];
        CHECK(std::snprintf(pattern,sizeof(pattern),"%s/z24s8-publish-XXXXXX",std::getenv("TMPDIR"))<int(sizeof(pattern)));
        char* path=mkdtemp(pattern);CHECK(path);directory=path;current=this;
    }
    static int open(char const* path) {
        auto& f=*current;f.calls.push_back("open");
        if(f.fault=="open") { errno=EIO;return -1; } return NativePublicationOps.openExclusive(path);
    }
    static ssize_t write(int fd,void const* data,size_t bytes) {
        auto& f=*current;f.calls.push_back("write");++f.writes;
        if(f.fault=="write" || (f.fault=="partial" && f.writes>1)) { errno=EIO;return -1; }
        return NativePublicationOps.write(fd,data,f.fault=="partial" ? bytes/2 : bytes);
    }
    static int sync(int fd) {
        auto& f=*current;f.calls.push_back("sync");
        if(f.fault=="sync") { errno=EIO;return -1; } return NativePublicationOps.sync(fd);
    }
    static int close(int fd) {
        auto& f=*current;f.calls.push_back("close"); const int result=NativePublicationOps.close(fd);
        if(f.fault=="close") { errno=EIO;return -1; } return result;
    }
    static int publish(char const* pending,char const* final) {
        auto& f=*current;f.calls.push_back("publish");
        if(f.fault=="publish") { errno=EIO;return -1; } return NativePublicationOps.publish(pending,final);
    }
    PublicationOps ops() { return {NativePublicationOps.absent,open,write,sync,close,publish}; }
    bool run() { static char const text[]="{\"status\":\"PASS\"}\n";return PublishTerminal(directory.c_str(),text,sizeof(text)-1,ops()); }
    bool finalAbsent() { return NativePublicationOps.absent((directory+"/result.json").c_str()); }
    bool pendingAbsent() { return NativePublicationOps.absent((directory+"/result.pending").c_str()); }
};
}
TEST(ProbePublicationSuccessOrdersSyncAndCloseBeforePublish) {
    publication_test::Fixture f;CHECK(f.run());CHECK(!f.finalAbsent() && f.pendingAbsent());
    CHECK(f.calls==std::vector<std::string>({"open","write","sync","close","publish"}));
    std::ifstream input(f.directory+"/result.json");
    std::string data((std::istreambuf_iterator<char>(input)),{});CHECK(data=="{\"status\":\"PASS\"}\n");
}
TEST(ProbePublicationOpenFailureCannotLeaveTerminalPass) {
    publication_test::Fixture f;f.fault="open";CHECK(!f.run());CHECK(f.finalAbsent() && f.pendingAbsent());
}
TEST(ProbePublicationWriteFailureCannotLeaveTerminalPass) {
    publication_test::Fixture f;f.fault="write";CHECK(!f.run());CHECK(f.finalAbsent() && !f.pendingAbsent());
}
TEST(ProbePublicationPartialWriteRetainsOnlyPendingEvidence) {
    publication_test::Fixture f;f.fault="partial";CHECK(!f.run());CHECK(f.finalAbsent() && !f.pendingAbsent());
}
TEST(ProbePublicationSyncFailureCannotPublishCompletePassBytes) {
    publication_test::Fixture f;f.fault="sync";CHECK(!f.run());CHECK(f.finalAbsent() && !f.pendingAbsent());
    CHECK(std::find(f.calls.begin(),f.calls.end(),"publish")==f.calls.end());
}
TEST(ProbePublicationCloseFailureCannotPublishCompletePassBytes) {
    publication_test::Fixture f;f.fault="close";CHECK(!f.run());CHECK(f.finalAbsent() && !f.pendingAbsent());
    CHECK(std::find(f.calls.begin(),f.calls.end(),"publish")==f.calls.end());
}
TEST(ProbePublicationRenameFailureLeavesNoFinalPass) {
    publication_test::Fixture f;f.fault="publish";CHECK(!f.run());CHECK(f.finalAbsent() && !f.pendingAbsent());
}
TEST(ProbePublicationNeverOverwritesExistingTerminalResult) {
    using namespace stencil_probe;publication_test::Fixture f;
    int fd=NativePublicationOps.openExclusive((f.directory+"/result.json").c_str());CHECK(fd>=0);
    CHECK(NativePublicationOps.write(fd,"previous",8)==8);CHECK(NativePublicationOps.close(fd)==0);
    CHECK(!f.run() && f.calls.empty());
    std::ifstream input(f.directory+"/result.json");std::string data((std::istreambuf_iterator<char>(input)),{});CHECK(data=="previous");
}
TEST(ProbeTerminalMetadataDoesNotClaimGpuExecutionBeforeSubmission) {
    using namespace stencil_probe;char text[1024];
    CHECK(FormatTerminal(text,sizeof(text),false,0,0,ExecutionStage::HostSetup,"allocation_failure")>0);
    CHECK(std::string(text).find("\"probe_submission_attempted\":false")!=std::string::npos);
    CHECK(std::string(text).find("\"execution_stage\":\"host_setup\"")!=std::string::npos);
    CHECK(std::string(text).find("gpu_execution")==std::string::npos);
    CHECK(FormatTerminal(text,sizeof(text),true,0,0,ExecutionStage::HostSetup,"bad_pass")==0);
    CHECK(FormatTerminal(text,sizeof(text),true,Steps,Steps,ExecutionStage::VerifiedCompletion,"all_comparisons_passed")>0);
    CHECK(std::string(text).find("\"status\":\"PASS\"")!=std::string::npos);
}
