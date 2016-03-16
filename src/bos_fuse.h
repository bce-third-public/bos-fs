#ifndef BAIDU_BCE_BOS_FS_BOS_FUSE_H_
#define BAIDU_BCE_BOS_FS_BOS_FUSE_H_
#include <sys/types.h>
#include "common_def.h"

#include "fuse/fuse.h"
#include "fuse/fuse_common.h"

BEGIN_FS_NAMESPACE
int Getattr(const char *, struct stat *);
int Readlink(const char *, char *, size_t);
int Mknod(const char *, mode_t, dev_t);
int Mkdir(const char *, mode_t);
int Unlink(const char *);
int Rmdir(const char *);
int Rename(const char *, const char *);
int Chmod(const char *, mode_t);
int Chown(const char *, uid_t, gid_t);
int Truncate(const char *, off_t);
int Open(const char *, struct fuse_file_info *);
int Read(const char *, char *, size_t, off_t, struct fuse_file_info *fi);
int Write(const char *, const char *, size_t, off_t, struct fuse_file_info *fi);
int StatFs(const char *, struct statvfs *);
int Flush(const char *, struct fuse_file_info *);
int Release(const char *, struct fuse_file_info *);
int Fsync(const char *, int, struct fuse_file_info *);
int RemoveXattr(const char *, const char *);
int OpenDir(const char *, struct fuse_file_info *);
int ReadDir(const char *, void *, fuse_fill_dir_t, off_t, struct fuse_file_info *fi);
int ReleaseDir(const char *, struct fuse_file_info *);
int FsyncDir(const char *, int, struct fuse_file_info *);
int Access(const char *, int);
int Create(const char *, mode_t, struct fuse_file_info *);

// const char *kFsMetaPrefix = "x-bce-fs-";
END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_FS_BOS_FUSE_H_
