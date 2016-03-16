#ifndef BAIDU_BCE_BOS_FILE_CACHE_H_
#define BAIDU_BCE_BOS_FILE_CACHE_H_
#include "common_def.h"

#include <assert.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <map>
#include <string>
#include <vector>

#include "refable.h"
#include "lock.h"

BEGIN_FS_NAMESPACE
class PageCache : public Refable {
public:
    PageCache(const std::string &dir, int index);
    virtual ~PageCache();

    int Write(const char *buffer, int length, int offset);

    int Read(char *buffer, int length, int offset);

    uint64_t GetLastModify() const {
        return 0;
    }

    int32_t GetSize() const {
        return m_size;
    }

    int GetContent(std::string *buffer);

    void SetDirty(bool dirty) {
        m_dirty = dirty;
    }

    bool IsDirty() const {
        return m_dirty;
    }

private:
    std::string m_dir;
    int m_index;
    int m_cache_fd;
    int m_size;
    std::string m_cache_filename;
    bool m_dirty;
    time_t m_last_modify;

};

class FileCache : public Refable {
public:
    FileCache(const std::string &cache_root, const std::string &path, int flags, int mode, int type);
    virtual ~FileCache();

    int Init();

    int Write(void *handler, const char *buffer, int64_t length, int64_t offset);
    int Read(char *buffer, int64_t length, int64_t offset);

    int Stat(struct stat *stat) {
        stat->st_mode= m_mode | S_IFREG;
        stat->st_size = m_file_size;
        if (m_mtime != 0) {
            stat->st_mtime = m_mtime;
        } else {
            stat->st_mtime = time(NULL);
        }
        return 0;
    }

    int Close();

    int GetDirtyPage(void *handler, std::map<int, PageCache *> *dirty_pages);

    void Clear();

    void SetPageDirtyState(void *handler, int index, bool dirty) {
        m_page_dirty_state[handler][index] = dirty;
    }

    bool GetPageDirtyState(void *handler, int index) {
        return m_page_dirty_state[handler][index];
    }

private:
    static const int kCachePageSize = 1024 * 1024 * 5;
    std::string m_path;
    std::string m_cache_dir;
    int m_mode;
    int m_type;
    int64_t m_file_size;
    int m_flags;
    time_t m_mtime;

    std::vector<PageCache *> m_pages;

    PageCache *GetPage(int index);
    PageCache *CreatePage(int index);
    // int m_open_ref_cnt;
    std::map<void *, std::map<int, bool> > m_page_dirty_state;
};

class FileCacheManager {
public:
    FileCacheManager() {
    }
    ~FileCacheManager() {
    }

    int Init();
    FileCache *GetFileCache(const std::string &path);
    FileCache *Create(const std::string &path, int flags, int mode);
    void Close(const std::string &path) {
        RecursiveLockGuard guard(&m_lock);
        std::map<std::string, FileCache *>::iterator it = m_file_caches.find(path);
        if (it != m_file_caches.end()) {
            FileCache *file_cache = it->second;
            m_file_caches.erase(it);
            file_cache->Release();
        }
    }

    void DoTrigger() {
        RecursiveLockGuard guard(&m_lock);
        for (std::map<std::string, FileCache *>::iterator it = m_file_caches.begin();
                it != m_file_caches.end();) {
            FileCache *file_cache = it->second;
            if (file_cache->GetRefCount() == 1) {
                m_file_caches.erase(it++);
                file_cache->Release();
            } else {
                it++;
            }
        }
    }

private:
    std::map<std::string, FileCache *> m_file_caches;
    pthread_t m_cache_clean_thread;
    static void *CleanWorker(void *);

    void DoClean();
    RecursiveLock m_lock;
};

extern FileCacheManager g_cache_manager;
END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_FILE_CACHE_H_
