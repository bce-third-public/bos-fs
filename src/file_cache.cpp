#include "file_cache.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>

#include <sstream>

#include "bos/model/request/init_multi_upload_request.h"
#include "bos/model/response/init_multi_upload_response.h"

#include "bos/model/request/upload_part_request.h"
#include "bos/model/response/upload_part_response.h"

#include "bos/model/request/complete_multipart_upload_request.h"
#include "bos/model/response/complete_multipart_upload_response.h"

#include "bos/model/request/put_object_request.h"
#include "bos/model/response/put_object_response.h"

#include "bos/model/request/get_object_request.h"
#include "bos/model/response/get_object_response.h"

#include "bos/model/request/head_object_request.h"
#include "bos/model/response/head_object_response.h"

#include "fs_config.h"

BEGIN_FS_NAMESPACE
int FileCache::Init() {
    if ((m_flags & O_ACCMODE) == O_RDONLY) {
    } else {
        int ret = mkdir(m_cache_dir.c_str(), 0711);
        if (ret == 0 || errno == EEXIST) {
            return 0;
        }

        return -EEXIST;
    }
    return 0;
}

int FileCache::GetDirtyPage(void *handler, std::map<int, PageCache *> *dirty_pages) {
    // RecursiveLockGuard guard(&m_lock);
    for (size_t i = 0; i < m_pages.size(); i++) {
        PageCache *page = m_pages[i];
        if (GetPageDirtyState(handler, i) == true) {
            page->AddRef();
            dirty_pages->insert(std::pair<int, PageCache *>(i + 1, page));
        }
    }

    return 0;
}

FileCache::FileCache(const std::string &cache_root, const std::string &path, int flags, int mode, int type) {
    m_path = path;
    m_flags = flags;
    m_mode = mode;
    m_type = type;
    m_file_size = 0;
    // m_open_ref_cnt = 0;

    std::stringstream cache_dir_ss;
    cache_dir_ss << cache_root << "/";

    for (size_t index = 0; index < path.size(); index++) {
        if (path.c_str()[index] == '/') {
            cache_dir_ss << "#";
        } else {
            cache_dir_ss << path.c_str()[index];
        }
    }
    m_cache_dir = cache_dir_ss.str();
    m_mtime = time(NULL);
}

FileCache::~FileCache() {
    for (uint32_t i = 0; i < m_pages.size(); i++) {
        if (m_pages[i] != NULL) {
            m_pages[i]->Release();
            m_pages[i] = NULL;
        }
    }

    struct stat st;
    if (!stat(m_cache_dir.c_str(), &st)) {
        if (st.st_mode & S_IFDIR) {
            rmdir(m_cache_dir.c_str());
        }
    }
}

int FileCache::Read(char *buffer, int64_t length, int64_t offset) {
    int64_t total_length = length;
    int start_page_index = offset / kCachePageSize;
    int end_page_index = (offset + length) / kCachePageSize;

    for (int page_index = start_page_index; page_index <= end_page_index; page_index++) {
        PageCache *page_cache = GetPage(page_index);
        if (page_cache == NULL) {
            return 0;
        }

        int page_length = (page_index + 1) * kCachePageSize - offset;
        if (page_length > length) {
            page_length = length;
        }

        int ret = page_cache->Read(buffer, page_length, offset - page_index * kCachePageSize);
        if (ret <= 0) {
            return ret;
        }

        buffer = buffer + page_length;
        offset = offset + page_length;
        length = length - page_length;
    }

    return total_length;
}

int FileCache::Write(void *handler, const char *buffer, int64_t length, int64_t offset) {
    int64_t total_length = length;
    int start_page_index = offset / kCachePageSize;
    int end_page_index = (offset + length) / kCachePageSize;

    for (int page_index = start_page_index; page_index <= end_page_index; page_index++) {
        PageCache *page_cache = GetPage(page_index);
        if (page_cache == NULL) {
            page_cache = CreatePage(page_index);
            if (page_cache == NULL) {
                return -EIO;
            }
        }

        int page_length = (page_index + 1) * kCachePageSize - offset;
        if (page_length > length) {
            page_length = length;
        }

        int ret = page_cache->Write(buffer, page_length, offset - page_index * kCachePageSize);
        if (ret < 0) {
            return ret;
        }

        buffer = buffer + page_length;
        offset = offset + page_length;
        length = length - page_length;

        page_cache->SetDirty(true);
        SetPageDirtyState(handler, page_index, true);
    }

    if (m_file_size < length + offset) {
        m_file_size = length + offset;
    }

    m_mtime = time(NULL);

    return total_length;
}

PageCache *FileCache::GetPage(int index) {
    if (index < m_pages.size()) {
        if (m_pages[index] != NULL) {
            return m_pages[index];
        }

        // file content remove from cache, we can not write it
        return NULL;
    }

    return NULL;
}

PageCache *FileCache::CreatePage(int index) {
    if (index >= m_pages.size()) {
        m_pages.resize(index + 1);
    }

    PageCache *page = new PageCache(m_cache_dir, index);
    page->AddRef();
    m_pages[index] = page;

    return page;
}

int FileCache::Close() {
    return 0;
}

void FileCache::Clear() {
    for (uint32_t i = 0; i < m_pages.size(); i++) {
        m_pages[i]->Release();
    }

    m_pages.clear();
    m_file_size = 0;
    m_mtime = time(NULL);

}

PageCache::PageCache(const std::string &dir, int index) {
    m_dir = dir;
    m_index = index;

    std::stringstream filename;
    filename << dir << "/" << index;
    m_cache_filename = filename.str();

    m_cache_fd = open(m_cache_filename.c_str(), O_CREAT | O_RDWR | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (m_cache_fd <= 0) {
    }
    m_size = 0;
    m_dirty = false;
    m_last_modify = 0;
}

PageCache::~PageCache() {
    if (m_cache_fd > 0) {
        close(m_cache_fd);
        m_cache_fd = 0;
    }

    if (access(m_cache_filename.c_str(), F_OK) == 0) {
        unlink(m_cache_filename.c_str());
    }
}

int PageCache::Write(const char *buffer, int length, int offset) {
    int ret = pwrite(m_cache_fd, buffer, length, offset);
    if (ret < 0) {
        return ret;
    }

    if (ret + offset > m_size) {
        m_size = m_size + ret;
    }


    return ret;
}

int PageCache::Read(char *buffer, int length, int offset) {

    int ret = pread(m_cache_fd, buffer, length, offset);
    if (ret < 0) {
        return ret;
    }


    return ret;
}

int PageCache::GetContent(std::string *buffer) {
    buffer->resize(m_size);
    int to_be_read = m_size;
    int total_read_count = 0;

    while (to_be_read > 0) {
        int read_cnt = pread(m_cache_fd, const_cast<char *>(buffer->data() + total_read_count),
                m_size - total_read_count, total_read_count);
        if (read_cnt < 0) {
            return -1;
        }

        to_be_read -= read_cnt;
        total_read_count += read_cnt;
    }

    return 0;
}

int FileCacheManager::Init() {
    return pthread_create(&m_cache_clean_thread, NULL, CleanWorker, this);
}

void* FileCacheManager::CleanWorker(void *args) {
    FileCacheManager *file_cache_manager = reinterpret_cast<FileCacheManager *>(args);
    file_cache_manager->DoClean();

    return (void *)NULL;
}

void FileCacheManager::DoClean() {
    while (true) {
        {
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

        usleep(100);
    }
}

FileCache *FileCacheManager::GetFileCache(const std::string &path) {
    RecursiveLockGuard guard(&m_lock);
    std::map<std::string, FileCache *>::iterator it = m_file_caches.find(path);

    if (it != m_file_caches.end()) {
        it->second->AddRef();
        return it->second;
    }

    return NULL;
}

FileCache *FileCacheManager::Create(const std::string &path, int flags, int mode) {
    RecursiveLockGuard guard(&m_lock);
    FileCache *file_cache = GetFileCache(path);

    if (file_cache != NULL) {
        // file_cache->AddRef();
        return file_cache;
    }

    file_cache = new FileCache(g_fs_config.GetCacheDir(), path, flags, mode, 0);
    if (file_cache->Init()) {
        delete file_cache;
        return NULL;
    }

    // add ref for manager
    file_cache->AddRef();

    // add ref for caller;
    file_cache->AddRef();
    m_file_caches[path] = file_cache;

    return file_cache;
}
END_FS_NAMESPACE
