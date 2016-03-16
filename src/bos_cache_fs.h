#ifndef BAIDU_BCE_BOS_FS_BOS_CACHE_FS_H_
#define BAIDU_BCE_BOS_FS_BOS_CACHE_FS_H_
#include <sys/types.h>
#include "common_def.h"
#include "bos_fs.h"
#include "lock.h"

BEGIN_FS_NAMESPACE
class BosCacheFileHandler : public BosFileHandler {
public:
    BosCacheFileHandler(const std::string &filename, int type, const std::string &cache_dir)
                : BosFileHandler(filename, type) {
        m_cache_dir = cache_dir;
        m_file_cache = NULL;
    }

    BosCacheFileHandler(const BosFileHandler &base_handler, const std::string &cache_dir)               : BosFileHandler(base_handler) {
        m_cache_dir = cache_dir;
    }

    void SetFileCache(FileCache *file_cache) {
        m_file_cache = file_cache;
    }

    FileCache *GetFileCache() {
        return m_file_cache;
    }

private:
    std::string m_cache_dir;
    FileCache *m_file_cache;
};

class FileCacheManager;

class BosCacheFsClient : public BosFsClient {
public:
    BosCacheFsClient(const std::string &ak, const std::string &sk,
            const std::string endpoint, const std::string &bucket,
            const std::string &cache_dir);

    virtual ~BosCacheFsClient() {
    }

    virtual int Getattr(const std::string &path, struct stat *attr);
    virtual int Mkdir(const std::string &path, mode_t mode);
    virtual int Unlink(const std::string &path);
    virtual int Rmdir(const std::string &path);

    virtual int Chmod(const std::string &path, mode_t mode);
    virtual int Chown(const std::string &path, uid_t uid, gid_t gid);

    virtual BosFileHandler *Open(const std::string &path, int flags);
    virtual BosFileHandler *Create(const std::string &path, mode_t mode);
    virtual ssize_t Read(BosFileHandler *handler, void *buf, off_t offset, size_t count);
    virtual ssize_t Write(BosFileHandler *handler, const void *buf, off_t offset, size_t count);
    virtual int Flush(BosFileHandler *handler);
    virtual int Close(BosFileHandler *handler);

    virtual BosDirHandler *OpenDir(const std::string &path);
    virtual int ReadDir(BosDirHandler *handler, std::string * filename, struct stat *attr);
    virtual int CloseDir(BosDirHandler *handler);

    virtual int Truncate(const std::string &path, off_t off);

protected:
    virtual void DoTrigger();

private:
    std::string m_cache_dir;
    FileCacheManager *m_cache_manager;

    int Sync(BosCacheFileHandler *handler, bool force);

    virtual BosFileHandler *CreateFileHandler(const std::string &path, int flags) {
        BosFileHandler * handler = new BosCacheFileHandler(path, flags, m_cache_dir);
        handler->AddRef();
        return handler;
    }

    void OnFileHandlerTrigger(BosFileHandler *handler);
    RecursiveLock m_lock;
};
END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_FS_BOS_CACHE_FS_H_

