#include "bos_cache_fs.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "file_cache.h"

BEGIN_FS_NAMESPACE
BosCacheFsClient::BosCacheFsClient(const std::string &ak, const std::string &sk,
        const std::string endpoint, const std::string &bucket,
        const std::string &cache_dir) : BosFsClient(ak, sk, endpoint, bucket) {
    m_cache_dir = cache_dir;
    m_cache_manager = new FileCacheManager;
}
int BosCacheFsClient::Getattr(const std::string &path, struct stat *attr) {
    RecursiveLockGuard guard(&m_lock);
    FileCache *file_cache = m_cache_manager->GetFileCache(path);
    if (file_cache == NULL) {
        return BosFsClient::Getattr(path, attr);
    }

    int ret = file_cache->Stat(attr);
    file_cache->Release();

    return ret;
}

int BosCacheFsClient::Mkdir(const std::string &path, mode_t mode) {
    return BosFsClient::Mkdir(path, mode);
}

int BosCacheFsClient::Rmdir(const std::string &path) {
    return BosFsClient::Rmdir(path);
}

int BosCacheFsClient::Unlink(const std::string &path) {
    return BosFsClient::Unlink(path);
}

int BosCacheFsClient::Chmod(const std::string &path, mode_t mode) {
    return BosFsClient::Chmod(path, mode);
}

int BosCacheFsClient::Chown(const std::string &path, uid_t uid, gid_t gid) {
    return BosFsClient::Chown(path, uid, gid);
}

BosFileHandler *BosCacheFsClient::Open(const std::string &path, int flags) {
    RecursiveLockGuard guard(&m_lock);
    if ((flags & O_ACCMODE) == O_RDONLY) {
        BosCacheFileHandler *file_handler = dynamic_cast<BosCacheFileHandler *>(
                BosFsClient::Open(path, flags));
        if (file_handler == NULL || file_handler->GetError() != 0) {
            return file_handler;
        }

        /*
        FileCache *file_cache = m_cache_manager->GetFileCache(path);
        file_handler->SetFileCache(file_cache);
        file_cache->AddOpenRefCnt();
        */

        return file_handler;
    }

    BosCacheFileHandler *file_handler = dynamic_cast<BosCacheFileHandler *>(
            BosFsClient::Open(path, flags));
    if (file_handler == NULL || file_handler->GetError() != 0) {
        return file_handler;
    }

    // open for write is not permitted
    if ((flags & O_APPEND) == O_APPEND) {
        file_handler->SetError(-EPERM);
        return file_handler;
    }

    FileCache *file_cache = file_handler->GetFileCache();
    if (file_cache == NULL) {
        file_cache = m_cache_manager->Create(path, flags, 0);
    }

    file_handler->SetFileCache(file_cache);

    return file_handler;
}

BosFileHandler *BosCacheFsClient::Create(const std::string &path, mode_t mode) {
    RecursiveLockGuard guard(&m_lock);
    FileCache *file_cache = m_cache_manager->GetFileCache(path);

    if (file_cache != NULL) {
        file_cache->Release();
        BosCacheFileHandler *file_handler = dynamic_cast<BosCacheFileHandler *>(
                CreateFileHandler(path, 0));
        file_handler->SetError(-EPERM);
        return file_handler;
    }

    BosCacheFileHandler *file_handler = dynamic_cast<BosCacheFileHandler *>(
            BosFsClient::Create(path, mode));
    if (file_handler == NULL || file_handler->GetError() != 0) {
        return file_handler;
    }

    file_cache = m_cache_manager->Create(path, O_CREAT | O_RDWR , mode);
    file_handler->SetFileCache(file_cache);
    // file_cache->AddOpenRefCnt();

    return file_handler;
}

ssize_t BosCacheFsClient::Read(BosFileHandler *handler, void *buf, off_t offset, size_t count) {
    BosCacheFileHandler *cache_file_handler = dynamic_cast<BosCacheFileHandler *>(handler);
    assert(cache_file_handler != NULL);

    FileCache *file_cache = cache_file_handler->GetFileCache();
    if (file_cache == NULL) {
        return BosFsClient::Read(handler, buf, offset, count);
    }

    off_t chunk_size = kCachePageSize;
    for (off_t read_count = 0, chunk_start = offset % kCachePageSize; read_count < count;) {
        size_t chunk_read_count = chunk_size - chunk_start;
        int ret = file_cache->Read(reinterpret_cast<char *>(buf) + read_count,
                offset + read_count, chunk_size - chunk_start);
        if (ret < 0) {
            return ret;
        }

        if (ret == 0) {
            ret = BosFsClient::Read(handler, reinterpret_cast<char *>(buf) + read_count,
                    offset + read_count, chunk_size - chunk_start);
            if (ret < 0) {
                handler->SetError(ret);
                return ret;
            }
        }

        read_count += chunk_read_count;
    }

    return count;
}

ssize_t BosCacheFsClient::Write(BosFileHandler *handler, const void *buf, off_t offset, size_t count) {
    BosCacheFileHandler *cache_file_handler = dynamic_cast<BosCacheFileHandler *>(handler);
    assert(cache_file_handler != NULL);

    FileCache *file_cache = cache_file_handler->GetFileCache();
    if (file_cache == NULL) {
        handler->SetError(-EIO);
        return -EIO;
    }

    return file_cache->Write(handler, reinterpret_cast<const char *>(buf), count, offset);
}

int BosCacheFsClient::Flush(BosFileHandler *handler) {
    BosCacheFileHandler *cache_file_handler = dynamic_cast<BosCacheFileHandler *>(handler);
    assert(cache_file_handler != NULL);
    while (Sync(cache_file_handler, true) != 0) {
    }

    return 0;
}

int BosCacheFsClient::Close(BosFileHandler *handler) {
    BosCacheFileHandler *cache_file_handler = dynamic_cast<BosCacheFileHandler *>(handler);
    assert(cache_file_handler != NULL);

    if ((handler->GetFlags() & O_ACCMODE == O_RDWR)
            || ((handler->GetFlags() & O_ACCMODE) == O_WRONLY)) {
        Sync(cache_file_handler, true);
    }

    m_cache_manager->Close(handler->GetFileName());
    FileCache *file_cache = cache_file_handler->GetFileCache();
    cache_file_handler->SetFileCache(NULL);
    if (file_cache != NULL) {
        file_cache->Release();
    }

    return BosFsClient::Close(handler);

}

BosDirHandler *BosCacheFsClient::OpenDir(const std::string &path) {
    return BosFsClient::OpenDir(path);
}

int BosCacheFsClient::ReadDir(BosDirHandler *handler, std::string * filename, struct stat *attr) {
    return BosFsClient::ReadDir(handler, filename, attr);
}

int BosCacheFsClient::CloseDir(BosDirHandler *handler) {
    return BosFsClient::CloseDir(handler);
}

int BosCacheFsClient::Truncate(const std::string &path, off_t off) {
    FileCache *file_cache = m_cache_manager->GetFileCache(path);
    if (file_cache != NULL) {
        file_cache->Clear();
        file_cache->Release();
    }


    return BosFsClient::Truncate(path, off);
}

int BosCacheFsClient::Sync(BosCacheFileHandler *handler, bool force) {
    // read only
    FileCache *file_cache = handler->GetFileCache();
    if (file_cache == NULL) {
        return 0;
    }

    int sync_error = 0;


    std::map<int, PageCache *> dirty_pages;
    file_cache->GetDirtyPage(handler, &dirty_pages);

    for (std::map<int, PageCache *>::iterator it = dirty_pages.begin();
            it != dirty_pages.end(); it++) {
        int index = it->first;
        PageCache *page_cache = it->second;

        if (force == false && page_cache->GetSize() < kCachePageSize) {
            page_cache->Release();
            continue;
        }

        std::string content;
        page_cache->GetContent(&content);

        int ret = WriteBlock(handler, index, content);
        if (ret == 0) {
            page_cache->SetDirty(false);
        }
        page_cache->Release();

        if (ret != 0) {
            // return ret;
            handler->SetError(ret);
            sync_error = ret;
            continue;
        }
    }

    return sync_error;
}

void BosCacheFsClient::OnFileHandlerTrigger(BosFileHandler *handler) {
    BosCacheFileHandler *cache_hander = dynamic_cast<BosCacheFileHandler *>(handler);
    assert(cache_hander != NULL);
    Sync(cache_hander, false);
}
void BosCacheFsClient::DoTrigger() {
    BosFsClient::DoTrigger();

    m_cache_manager->DoTrigger();

}

END_FS_NAMESPACE
