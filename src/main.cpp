#include <cstdio>

#include "sqlite3.h"
#include "renderdoc_replay.h"

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

int main(int argc, char **argv) {
    printf("[MAIN] FrameDB\n");

    sqlite3 *sql = nullptr;
    int res = sqlite3_open(":memory:", &sql);
    printf("[MAIN] opening SQLite3 in-memory DB - res = %i\n", res);
    res = sqlite3_close(sql);
    printf("[MAIN] closing SQLite3 in-memory DB - res = %i\n", res);
    
    ICaptureFile *file = RENDERDOC_OpenCaptureFile();
    ResultDetails out = file->OpenFile("test", "rdc", NULL);
    printf("[MAIN] opening capture file - code = %i\n", out.code);
    rdcstr error = out.Message();
    printf("\t 'error: %s'\n", error.c_str());

    return 0;
}
