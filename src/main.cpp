#include <cstdio>

#include "sqlite3.h"
#include "renderdoc_replay.h"

// renderdoc.dll's DllMain looks for this exported symbol in the host process. Without it the DLL
// assumes it was injected into a game, installs its D3D12/DXGI capture hooks, and never flips into
// replay mode -- the replay device then gets built on top of those hooks and dies partway through
// ReadLogInitialisation. Must be at global scope so it lands in the exe's export table.
REPLAY_PROGRAM_MARKER()

template <>
rdcstr DoStringise(const uint32_t &el)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", el);
    return rdcstr(buf);
}

template <>
rdcstr DoStringise(const ResultCode &el)
{
    BEGIN_ENUM_STRINGISE(ResultCode);
    {
        STRINGISE_ENUM_CLASS(Succeeded);
        STRINGISE_ENUM_CLASS(UnknownError);
        STRINGISE_ENUM_CLASS(InternalError);
        STRINGISE_ENUM_CLASS(FileNotFound);
        STRINGISE_ENUM_CLASS(InjectionFailed);
        STRINGISE_ENUM_CLASS(IncompatibleProcess);
        STRINGISE_ENUM_CLASS(NetworkIOFailed);
        STRINGISE_ENUM_CLASS(NetworkRemoteBusy);
        STRINGISE_ENUM_CLASS(NetworkVersionMismatch);
        STRINGISE_ENUM_CLASS(FileIOFailed);
        STRINGISE_ENUM_CLASS(FileIncompatibleVersion);
        STRINGISE_ENUM_CLASS(FileCorrupted);
        STRINGISE_ENUM_CLASS(FileUnrecognised);
        STRINGISE_ENUM_CLASS(ImageUnsupported);
        STRINGISE_ENUM_CLASS(APIUnsupported);
        STRINGISE_ENUM_CLASS(APIInitFailed);
        STRINGISE_ENUM_CLASS(APIIncompatibleVersion);
        STRINGISE_ENUM_CLASS(APIHardwareUnsupported);
        STRINGISE_ENUM_CLASS(APIDataCorrupted);
        STRINGISE_ENUM_CLASS(APIReplayFailed);
        STRINGISE_ENUM_CLASS(JDWPFailure);
        STRINGISE_ENUM_CLASS(AndroidGrantPermissionsFailed);
        STRINGISE_ENUM_CLASS(AndroidABINotFound);
        STRINGISE_ENUM_CLASS(AndroidAPKFolderNotFound);
        STRINGISE_ENUM_CLASS(AndroidAPKInstallFailed);
        STRINGISE_ENUM_CLASS(AndroidAPKVerifyFailed);
        STRINGISE_ENUM_CLASS(RemoteServerConnectionLost);
        STRINGISE_ENUM_CLASS(OutOfMemory);
        STRINGISE_ENUM_CLASS(DeviceLost);
        STRINGISE_ENUM_CLASS(DataNotAvailable);
        STRINGISE_ENUM_CLASS(InvalidParameter);
        STRINGISE_ENUM_CLASS(CompressionFailed);
        STRINGISE_ENUM_CLASS(AndroidLayerConfFailed);
    }
    END_ENUM_STRINGISE();
}

static const char *FDB_SCHEMA_SQL =
#include "fdb_schema.inc"
;

int main(int argc, char **argv) {
    printf("[MAIN] FrameDB\n");

    sqlite3 *sql = nullptr;
    int res = sqlite3_open(":memory:", &sql);
    printf("[MAIN] opening SQLite3 in-memory DB - res = %i\n", res);
    res = sqlite3_close(sql);
    printf("--- SCRIPT---\n%s\n---\n", FDB_SCHEMA_SQL);
    printf("[MAIN] closing SQLite3 in-memory DB - res = %i\n", res);
    
    // Before InitialiseReplay, so the replay-app sanity checks it logs land in the file.
    RENDERDOC_SetDebugLogFile("framedb.log");

    GlobalEnvironment env;
    rdcarray<rdcstr> args = { "-windowed", "-resx=1920", "resy=1080" };
    RENDERDOC_InitialiseReplay(env, args);

    ICaptureFile *file = RENDERDOC_OpenCaptureFile();
    ResultDetails out = file->OpenFile("./data/test_capture_UE56.rdc", "rdc", NULL);
    printf("[MAIN] opening capture file - code = %i\n", out.code);
    rdcstr error = out.Message();
    printf("\t 'error: %s'\n", error.c_str());
    
    /*
    TODO Create a RenderDoc capture to test extracting info out of it
    - Walks `IReplayController::GetRootActions()` depth-first, maintaining a marker stack from
      actions flagged `ActionFlags::PushMarker` to build `marker_path`.
    - For each draw/dispatch action: `SetFrameEvent(eventId, false)`, then `GetPipelineState()` —
      the API-agnostic `PipeState`, not `GetD3D12PipelineState()` — and pull the PSO `ResourceId`,
      per-stage shader `ResourceId`s, `GetOutputTargets()`, `GetDepthTarget()`.
    - Dumps one line per action to a file.
    */
    rdcstr driver_name = file->DriverName();
    printf("[MAIN] driver name = %s\n", driver_name.c_str());
    ReplaySupport support = file->LocalReplaySupport();
    printf("[MAIN] replay support = %i\n", support);

    ReplayOptions opts;
    rdcpair<ResultDetails, IReplayController *> result = file->OpenCapture(opts, nullptr);
    if(!result.first.OK())
    {
        // On failure the controller is null, so this has to come before any use of it.
        printf("[MAIN] opening capture failed - code = %i\n", result.first.code);
        printf("\t 'error: %s'\n", result.first.Message().c_str());
        file->Shutdown();
        RENDERDOC_ShutdownReplay();
        return 1;
    }

    IReplayController *replay = result.second;
    const rdcarray<ActionDescription> &roots = replay->GetRootActions();
    rdcarray<const ActionDescription *> stack;
    for (int i = roots.size() - 1; i >= 0; i--) {
        stack.push_back(&roots[i]);
    }

    while(!stack.empty()) {
        const ActionDescription *a = stack.back();
        stack.pop_back();

        printf("\tFound action - [%i][%#x] `%s`\n", a->eventId, a->flags, a->customName.c_str());
        for (int i = a->children.size() - 1; i >= 0; i--) {
            stack.push_back(&a->children[i]);
        }
    }

    FrameDescription frame = replay->GetFrameInfo();
    FrameStatistics s = frame.stats;
    printf("[MAIN] current frame draw calls count = %i\n", s.draws.calls);

    replay->Shutdown();
    file->Shutdown();
    file = nullptr;

    RENDERDOC_ShutdownReplay();
    return 0;
}
