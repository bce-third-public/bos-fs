#define FUSE_USE_VERSION 26

#include "common_def.h"
#include "fuse.h"
#include "bos_fuse.h"
#include "bos_fs.h"
#include "bos_cache_fs.h"
#include "log.h"

#include <sys/time.h>

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "bos/api.h"

#include "fs_config.h"
#include "file_cache.h"

#include <gflags/gflags.h>

BEGIN_FS_NAMESPACE
extern BosFsClient *g_fs_client;
struct fuse_operations oper;
int InitOper(fuse_operations *oper) {
    memset(oper, 0, sizeof(oper));
    oper->getattr = fs_ns::Getattr;
    oper->readlink = fs_ns::Readlink;
    oper->mknod = fs_ns::Mknod;
    oper->mkdir = fs_ns::Mkdir;
    oper->unlink = fs_ns::Unlink;
    oper->rmdir = fs_ns::Rmdir;
    oper->rename = fs_ns::Rename;
    oper->chmod = fs_ns::Chmod;
    // oper->chown = fs_ns::Chown;
    oper->truncate = fs_ns::Truncate;
    oper->open = fs_ns::Open;
    oper->read = fs_ns::Read;
    oper->write = fs_ns::Write;
    oper->statfs = fs_ns::StatFs;
    oper->flush = fs_ns::Flush;
    oper->release = fs_ns::Release;
    oper->opendir = fs_ns::OpenDir;
    oper->readdir = fs_ns::ReadDir;
    oper->releasedir = fs_ns::ReleaseDir;
    // oper->access = fs_ns::Access;
    oper->create = fs_ns::Create;
}
END_FS_NAMESPACE

int main(int argc, char *argv[])
{
    // truerseCommandLineFlagsgoogle::AllowCommandLineReparsing();
    struct fuse *fuse;
    char *mountpoint;
    int multithreaded;
    int res;

    fs_ns::InitOper(&fs_ns::oper);

    {
        int flags_argc = 2;
        char conf_file[] = "--flagfile=./conf/bos_mnt.conf";
        char **flags_argv = new char *[2];
        flags_argv[0] = argv[0];
        flags_argv[1] = conf_file;
        google::ParseCommandLineFlags(&flags_argc, &flags_argv, false);
        fs_ns::g_fs_config = fs_ns::FsConfig();
        google::InitGoogleLogging(argv[0]);
    }
    fs_ns::InitSdkLog();

    fuse = fuse_setup(argc, argv, &fs_ns::oper, sizeof(fs_ns::oper), &mountpoint,
                 &multithreaded, NULL);
    if (fuse == NULL) {
        printf("setup fail\n");
        return 1;
    }
    ::bce::bos::Client::GlobalInit();
    if (fs_ns::g_fs_config.UseCache()) {
        fs_ns::g_fs_client = new fs_ns::BosCacheFsClient(fs_ns::g_fs_config.GetAk(),
            fs_ns::g_fs_config.GetSk(),
            fs_ns::g_fs_config.GetEndpoint(),
            fs_ns::g_fs_config.GetBucket(),
            fs_ns::g_fs_config.GetCacheDir());
    } else {
        fs_ns::g_fs_client = new fs_ns::BosFsClient(fs_ns::g_fs_config.GetAk(),
            fs_ns::g_fs_config.GetSk(),
            fs_ns::g_fs_config.GetEndpoint(),
            fs_ns::g_fs_config.GetBucket());
    }

    printf("begin main loog, multithreaded:%d\n", multithreaded);

    if (multithreaded)
        res = fuse_loop_mt(fuse);
    else
        res = fuse_loop(fuse);
    printf("after main loog, res:%d\n", res);

    fuse_teardown(fuse, mountpoint);
    if (res == -1)
        return 1;

    return 0;
}

