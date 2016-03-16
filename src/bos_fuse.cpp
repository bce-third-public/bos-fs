#include "common_def.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <string>
#include <time.h>

#include "bos/model/request/put_object_request.h"
#include "bos/model/response/put_object_response.h"

#include "bos/model/request/head_object_request.h"
#include "bos/model/response/head_object_response.h"

#include "bos/model/request/list_objects_request.h"
#include "bos/model/response/list_objects_response.h"

#include "bos/model/request/get_object_request.h"
#include "bos/model/response/get_object_response.h"

#include "util/memory_stream.h"

#include "fuse/fuse.h"

#include "fs_config.h"
#include "opendir_id.h"
#include "file_cache.h"
#include "bos_fuse.h"

#include "bos_fs.h"

BEGIN_FS_NAMESPACE

BosFsClient *g_fs_client = NULL;

typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
                const struct stat *stbuf, off_t off);


int Getattr(const char *path, struct stat *attr) {
    int ret = g_fs_client->Getattr(path, attr);
    return ret;
}

int Readlink(const char *, char *, size_t) {
    return ENOSYS;
}

int Mkdir(const char *path, mode_t mode) {
    return g_fs_client->Mkdir(path, mode);
}

int Mknod(const char *path, mode_t mode, dev_t) {
    if (S_ISREG(mode)) {
        fuse_file_info fi;
        fi.flags = O_CREAT | O_EXCL;
        return Create(path, mode, &fi);
    } else if (S_ISDIR(mode)) {
        return Mkdir(path, mode);
    }

    return -EPERM;
}

int Unlink(const char *path) {
    return g_fs_client->Unlink(path);
}

int Rmdir(const char *dir) {
    return g_fs_client->Rmdir(dir);
}

int Rename(const char *, const char *) {
    return 0;
}

int Chmod(const char *, mode_t) {
    return 0;
}

int Chown(const char *, uid_t, gid_t) {
    return 0;
}

int Truncate(const char *path, off_t off) {
    return g_fs_client->Truncate(path, off);
}

int Open(const char *path, struct fuse_file_info *fi) {
    int flags = fi->flags;
    if (flags & O_ACCMODE == O_RDONLY) {
        return -ENOSYS;
    }
    BosFileHandler *file_handler = g_fs_client->Open(path, flags);
    if (file_handler->GetError() != 0) {
        int ret = file_handler->GetError();
        delete file_handler;

        return ret;
    }

    fi->fh = (uint64_t) file_handler;
    return 0;
}

int Read(const char *path, char *buffer, size_t length, off_t offset, struct fuse_file_info *fi) {
    BosFileHandler *file_handler = (BosFileHandler *)fi->fh;
    int real_length = g_fs_client->Read(file_handler, buffer, offset, length);
    return real_length;
}

int Write(const char *path, const char *buffer, size_t length, off_t offset, struct fuse_file_info *fi) {
    BosFileHandler *file_handler = (BosFileHandler *)fi->fh;
    int real_length = g_fs_client->Write(file_handler, buffer, offset, length);
    return real_length;
}

int StatFs(const char *, struct statvfs *stat) {
    printf("StatFs\n");
    return 0;
}

int Flush(const char *path, struct fuse_file_info *fi) {
    BosFileHandler *file_handler = (BosFileHandler *)fi->fh;
    return g_fs_client->Flush(file_handler);
}

int Release(const char *, struct fuse_file_info *fi) {
    BosFileHandler *file_handler = (BosFileHandler *)fi->fh;
    if (file_handler != NULL) {
        g_fs_client->Close(file_handler);
    }
    return 0;
}

int Fsync(const char *, int, struct fuse_file_info *fi) {
    return 0;
}

int OpenDir(const char *path, struct fuse_file_info *fi) {
    BosDirHandler *dir_handler = g_fs_client->OpenDir(path);
    if (dir_handler->GetError() != 0) {
        // delete dir_handler;
        int ret = dir_handler->GetError();
        delete dir_handler;
        return ret;
    }

    fi->fh = (uint64_t)dir_handler;
    return 0;
}

int ReadDir(const char *path, void *buffer, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi) {
    BosDirHandler *dir_handler = (BosDirHandler *)fi->fh;
    std::string filename;
    struct stat attr;

    while (g_fs_client->ReadDir(dir_handler, &filename, &attr)) {
        filler(buffer, filename.c_str(), &attr, 0);
        filename = "";
        memset(&attr, 0, sizeof(attr));
    }

    return 0;
}

int ReleaseDir(const char *, struct fuse_file_info *fi) {
    BosDirHandler *dir_handler = (BosDirHandler *)fi->fh;
    fi->fh = 0;

    g_fs_client->CloseDir(dir_handler);

    return 0;
}

int FsyncDir(const char *, int, struct fuse_file_info *fi) {
    return 0;
}

int Access(const char *, int) {
    return 0;
}

int Create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    BosFileHandler *file_handler = g_fs_client->Create(path, mode);
    if (file_handler->GetError() != 0) {
        int ret = file_handler->GetError();
        delete file_handler;
        return ret;
    }

    fi->fh = (uint64_t) file_handler;
    return 0;
}
END_FS_NAMESPACE
