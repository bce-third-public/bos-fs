#ifndef BAIDU_BCE_BOS_FS_BOS_FS_H_
#define BAIDU_BCE_BOS_FS_BOS_FS_H_
#include <error.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>
#include <sstream>
#include <vector>

#include "common_def.h"
#include "refable.h"

#include "bos/api.h"

#include "lock.h"

namespace bce {
namespace bos {
class Client;
}
}

BEGIN_FS_NAMESPACE
class FileCache;
class BosFileHandler : public Refable {
public:
    BosFileHandler(const std::string &filename, int type) {
        m_filename = filename;
        m_type = type;
        memset(&m_attr, 0, sizeof(m_attr));
        m_next_index = 1;
        m_error = 0;
        m_dirty = false;
        m_closed = false;
        m_write_size = 0;
        m_exist_in_bos = true;
    }

    virtual ~BosFileHandler() {
        for (std::map<int, std::string *>::iterator it = m_read_page_cache.begin();
                it != m_read_page_cache.end(); it++) {
            if (it->second != NULL) {
                delete it->second;
            }
        }
    }

    void SetUploadId(const std::string &upload_id) {
        m_upload_id = upload_id;
    }

    const std::string &GetUploadId() const {
        return m_upload_id;
    }

    void AddPartInfo(int index, const std::string &part_info) {
        m_parts_info[index] = part_info;
    }

    const std::map<int, std::string> &GetPartsInfo() const {
        return m_parts_info;
    }

    struct stat *Getattr() {
        return &m_attr;
    }

    std::string *GetWriteBuffer() {
        return &m_write_buffer;
    }

    int Index() {
        return m_next_index;
    }

    int NextIndex() {
        m_next_index++;
        return m_next_index;
    }

    void SetError(int error) {
        m_error = error;
    }

    int GetError() const {
        return m_error;
    }

    const std::string &GetFileName() const {
        return m_filename;
    }

    int GetFlags() const {
        return m_type;
    }

    int GetMode() const {
        return m_attr.st_mode;
    }

    bool IsDirty() const {
        return m_dirty;
    }

    void SetDirty(bool dirty) {
        m_dirty = dirty;
    }

    bool IsClosed() const {
        return m_closed;
    }

    bool Close() {
        m_closed = true;
    }

    std::string *GetReadPageCache(int index) {
        std::map<int, std::string *>::iterator it = m_read_page_cache.find(index);
        if (it != m_read_page_cache.end()) {
            return it->second;
        }

        std::string *cache = new std::string;
        m_read_page_cache[index] = cache;
        return cache;
    }

    std::string *SetReadPageCache(int index, std::string *cache) {
    }

    void Lock() {
        m_lock.Lock();
    }

    void UnLock() {
        m_lock.UnLock();
    }

    size_t GetWriteSize() const {
        return m_write_size;
    }

    void IncWriteSize(size_t inc) {
        m_write_size += inc;
    }

    bool ExistInBos() const {
        return m_exist_in_bos;
    }

    void SetExistInBos(bool exist) {
        m_exist_in_bos = exist;
    }

private:
    int m_type;
    std::string m_filename;
    int m_next_index;

    struct stat m_attr;

    std::string m_upload_id;
    std::map<int, std::string> m_parts_info;
    std::string m_write_buffer;
    bool m_dirty;

    int m_error;
    bool m_closed;

    std::map<int, std::string *> m_read_page_cache;
    RecursiveLock m_lock;

    size_t m_write_size;
    bool m_exist_in_bos;
};

struct FileStat {
    int mode;
    int type;
    int64_t create_time;
    uint64_t size;
};

class BosDirHandler {
public:
    BosDirHandler(const std::string &path) {
        m_path = path;
        m_error = 0;
    }

    void SetError(int error) {
        m_error = error;
    }

    int GetError() const {
        return m_error;
    }

    void AddListInfo(const std::vector<std::string> &files,
            const std::vector<std::string> &dirs) {
        for (uint32_t i = 0; i < files.size(); i++) {
            m_files.push_back(files[i]);
        }

        for (uint32_t i = 0; i < dirs.size(); i++) {
            m_dirs.push_back(dirs[i]);
        }
    }

    int ParseListInfo() {
        for (uint32_t i = 0; i < m_files.size(); i++) {
            std::vector<std::string> pieces;
            Split(m_files[i], '#', &pieces);

            if (pieces.size() != 4) {
                continue;
            }

            FileStat stat;
            stat.mode = strtoll(pieces[1].c_str(), NULL, 10);
            stat.type = strtoll(pieces[2].c_str(), NULL, 10);
            stat.create_time = strtoll(pieces[3].c_str(), NULL, 10);

            m_file_stats[pieces[0]] = stat;
        }

        m_iter = m_file_stats.begin();

        return 0;
    }

    void AddItem(const std::string &file, const struct stat &attr) {
        FileStat stat;
        stat.mode = attr.st_mode;
        if (attr.st_mode & S_IFDIR) {
            stat.type = 0;
        } else {
            stat.type = 1;
        }
        stat.create_time = attr.st_mtime;

        m_file_stats[file] = stat;
    }

    int Item(std::string *file, FileStat *stat) {
        if (m_iter == m_file_stats.end()) {
            return -1;
        }

        *file = m_iter->first;
        *stat = m_iter->second;

        return 0;
    }

    void Next() {
        if (m_iter != m_file_stats.end()) {
            ++m_iter;
        }
    }

    const std::string &GetPath() const {
        return m_path;
    }

private:
    std::string m_path;
    std::vector<std::string> m_files;
    std::vector<std::string> m_dirs;
    std::map<std::string, FileStat> m_file_stats;
    std::map<std::string, FileStat>::iterator m_iter;

    void Split(const std::string &str, char delim, std::vector<std::string> *pieces) {
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, delim)) {
            pieces->push_back(item);
        }
    }

    int m_error;
};

class BosFsClient {
public:
    BosFsClient(const std::string &ak, const std::string &sk, const std::string endpoint, const std::string &bucket);

    virtual ~BosFsClient() {
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
    virtual int WriteBlock(BosFileHandler *handler, int index, const std::string &content);

    virtual int Flush(BosFileHandler *handler);
    virtual int Close(BosFileHandler *handler);

    virtual BosDirHandler *OpenDir(const std::string &path);
    virtual int ReadDir(BosDirHandler *handler, std::string * filename, struct stat *attr);
    virtual int CloseDir(BosDirHandler *handler);

    virtual int Truncate(const std::string &path, off_t off);

    /*
    BosFileHandler *GetOpenFileHandler(const std::string &path) {
        std::map<std::string, BosFileHandler *>::iterator it = m_open_files.find(path);
        if (it != m_open_files.end()) {
            return it->second;
        }

        return NULL;
    }
    */

protected:
    std::multimap<std::string, BosFileHandler *> m_open_files;
    virtual void DoTrigger();

    RecursiveLock *GetFileLock(const std::string &filename);
    void ReleaseFileLock(const std::string &filename, RecursiveLock *lock);

private:
    std::string m_bucket;
    // ::bce::bos::Client *m_bos_client;
    ::bce::bos::ClientOptions m_client_options;
    std::string m_ak;
    std::string m_sk;

    size_t UploadPart(BosFileHandler *handle, bool clean);

    virtual BosFileHandler *CreateFileHandler(const std::string &path, int flags) {
        BosFileHandler *handler = new BosFileHandler(path, flags);
        handler->AddRef();
        return handler;
    }

    pthread_t m_trigger_thread;
    static void *TriggerWorker(void *);
    virtual void OnFileHandlerTrigger(BosFileHandler *) {
        return;
    }

    RecursiveLock m_lock;

    struct FileLock {
        FileLock() {
            ref = 0;
            lock = new RecursiveLock();
        }

        ~FileLock() {
            delete lock;
        }

        int ref;
        RecursiveLock *lock;
    };

    std::map<std::string, FileLock *> m_file_lock_manager;

    int m_read_page_size;
};
END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_FS_BOS_FS_H_

