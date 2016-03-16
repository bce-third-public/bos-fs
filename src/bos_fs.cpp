#include "bos_fs.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <string>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "bos/api.h"

#include "bos/model/request/put_object_request.h"
#include "bos/model/response/put_object_response.h"

#include "bos/model/request/head_object_request.h"
#include "bos/model/response/head_object_response.h"

#include "bos/model/request/list_objects_request.h"
#include "bos/model/response/list_objects_response.h"

#include "bos/model/request/get_object_request.h"
#include "bos/model/response/get_object_response.h"

#include "bos/model/request/init_multi_upload_request.h"
#include "bos/model/response/init_multi_upload_response.h"
#include "bos/model/request/upload_part_request.h"
#include "bos/model/response/upload_part_response.h"
#include "bos/model/request/complete_multipart_upload_request.h"
#include "bos/model/response/complete_multipart_upload_response.h"

#include "bos/model/request/delete_object_request.h"
#include "bos/model/response/delete_object_response.h"

#include "bos/model/request/list_multipart_uploads_request.h"
#include "bos/model/response/list_multipart_uploads_response.h"

#include "bos/model/request/abort_multipart_upload_request.h"
#include "bos/model/response/abort_multipart_upload_response.h"

#include "util/memory_stream.h"

#include "fs_config.h"

BEGIN_FS_NAMESPACE
time_t GmtTimeToLocalTime(time_t gmt_time) {
    struct tm gmt_tm;
    struct tm local_tm;

    localtime_r(&gmt_time, &local_tm);
    gmtime_r(&gmt_time, &gmt_tm);

    int hour_diff = local_tm.tm_hour - gmt_tm.tm_hour;

    if (hour_diff < 0) {
        hour_diff += 24;
    }

    return gmt_time + hour_diff * 3600;
}

std::string NumberToString(uint64_t number) {
    const int MAX_BUFFER_SIZE = 128;
    char buffer[MAX_BUFFER_SIZE];

    snprintf(buffer, MAX_BUFFER_SIZE, "%llu", (unsigned long long)number);

    return std::string(buffer);
}

int BosResponseToPoxisError(int ret, ::bce::bos::BosResponse *response) {
    if (ret == 0) {
        LOG(INFO) << "http_code:" << response->GetHttpResponse()->GetHttpCode()
            << "bos_code:" << response->GetErrorCode();
        if (response->GetHttpResponse()->GetHttpCode() < 300
                && response->GetHttpResponse()->GetHttpCode() >= 200) {
            return 0;
        }

        if (response->GetHttpResponse()->GetHttpCode() == 404) {
            if (response->GetErrorCode() == "NoSuchBucket") {
                LOG(INFO) << "EIO: NoSuchBucket";
                return -EIO;
            }
            return -ENOENT;
        }

        if (response->GetHttpResponse()->GetHttpCode() == 403) {
            return -EACCES;
        }
    } else if (ret == ::bce::bos::kBosServiceError) {
        LOG(INFO) << "http_code:" << response->GetHttpResponse()->GetHttpCode()
           << ", bos_code:" << response->GetErrorCode();
        if (response->GetHttpResponse()->GetHttpCode() == 404) {
            if (response->GetErrorCode() == "NoSuchBucket") {
                LOG(INFO) << "EIO: NoSuchBucket";
                return -EIO;
            }
            return -ENOENT;
        }

        if (response->GetHttpResponse()->GetHttpCode() == 403) {
            return -EACCES;
        }
    }

    LOG(INFO) << "EIO: " << ret;
    return -EIO;

}

BosFsClient::BosFsClient(const std::string &ak, const std::string &sk, const std::string endpoint, const std::string &bucket) {
    // ::bce::bos::ClientOptions client_options;
    m_client_options.boss_host = endpoint;
    m_ak = ak;
    m_sk = sk;
    m_bucket = bucket;
    m_read_page_size = 1024 * 1024;

    assert(0 == pthread_create(&m_trigger_thread, NULL, TriggerWorker, this));
}

static void InitFileAttr(struct stat *attr) {
    attr->st_size = 0;
    attr->st_dev = 0;
    attr->st_ino = 0;
    attr->st_mode = 0777;
    attr->st_nlink = 1;
    attr->st_uid = g_fs_config.GetLocalUid();
    attr->st_gid = g_fs_config.GetLocalGid();
    attr->st_rdev = 0;
    attr->st_blksize = 1024 * 1024;
    attr->st_blocks = 0;
    attr->st_mtime = time(NULL);
}

int BosFsClient::Getattr(const std::string &path, struct stat *attr) {
    LOG(INFO) << "call getattr, path:" << path;
    RecursiveLockGuard guard(&m_lock);
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    std::map<std::string, BosFileHandler *>::iterator it = m_open_files.find(path);
    if (m_open_files.end() != it) {
        printf("get attr, handler:%p, size:%d\n", it->second, it->second->Getattr()->st_size);
        LOG(INFO) << "get attr from cache, path=" << path << ", size:" << it->second->Getattr()->st_size;
        memcpy(attr, it->second->Getattr(), sizeof(struct stat));
        return 0;
    }

    InitFileAttr(attr);

   if (strcmp("/", path.c_str()) == 0) {
       attr->st_mode= attr->st_mode | S_IFDIR;
       return 0;
   }

    ::bce::bos::HeadObjectRequest request(m_bucket, path);
    ::bce::bos::HeadObjectResponse response;

    int ret = bos_client.HeadObject(request, &response);
    ret = BosResponseToPoxisError(ret, &response);
    if (ret != 0) {
        return ret;
    }

    std::string mode_str;
    if (response.GetResponseHeader("x-bce-meta-fs-mode", &mode_str) == 0) {
        std::string file_type;

        if (response.GetResponseHeader("x-bce-meta-fs-file-type", &file_type) == 0
                && file_type == "dir") {
            attr->st_mode= atoi(mode_str.c_str()) | S_IFDIR;
        } else {
            attr->st_mode= atoi(mode_str.c_str()) | S_IFREG;
        }
    }

    uint64_t content_length = 0;
    if (response.GetLength(&content_length)) {
        return -EIO;
    }
    attr->st_size = content_length;

    std::string last_modify;
    if (response.GetResponseHeader("Last-Modified", &last_modify) == 0) {
        struct tm tm;
        strptime(last_modify.c_str(), "%a, %d %b %Y %H:%M:%S %z", &tm);
        attr->st_mtime = GmtTimeToLocalTime(mktime(&tm));
    }


    return 0;
}

int BosFsClient::Mkdir(const std::string &path, mode_t mode) {
    RecursiveLockGuard guard(&m_lock);
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    ::bce::bos::PutObjectRequest request(m_bucket, std::string(path), "");
    ::bce::bos::PutObjectRequest request_for_list(m_bucket, std::string(path) + "/", "");
    std::stringstream ss;
    ss << path << "#" << mode << "#" << 0 << "#" << time(NULL);
    ::bce::bos::PutObjectRequest mode_request(m_bucket, ss.str(), "");
    ::bce::bos::PutObjectResponse response;

    request.SetRequestHeader("x-bce-meta-fs-mode", NumberToString(mode));
    request.SetRequestHeader("x-bce-meta-fs-file-type", "dir");

    int ret = bos_client.PutObject(request_for_list, &response);
    ret = BosResponseToPoxisError(ret, &response);
    if (ret != 0) {
        return ret;
    }

    ret = bos_client.PutObject(mode_request, &response);
    ret = BosResponseToPoxisError(ret, &response);
    if (ret != 0) {
        return ret;
    }

    ret = bos_client.PutObject(request, &response);
    ret = BosResponseToPoxisError(ret, &response);

    return ret;
}

static int DeleteObject(::bce::bos::Client &bos_client, const std::string &bucket, const std::string &path) {
    ::bce::bos::DeleteObjectRequest request(bucket, std::string(path));
    ::bce::bos::DeleteObjectResponse response;

    int ret = bos_client.DeleteObject(request, &response);
    ret = BosResponseToPoxisError(ret, &response);
    if (ret != 0) {
        return ret;
    }

    return ret;
}

static int ListObject(::bce::bos::Client &bos_client, const std::string &bucket,
        const std::string &prefix,
        std::vector<std::string> *children) {
    ::bce::bos::ListObjectsRequest request(bucket);
    request.SetMaxKeys(1000);
    std::string next_marker = "";

    request.SetPrefix(prefix);

    while (true) {
        ::bce::bos::ListObjectsResponse  response;
        int ret = bos_client.ListObjects(request, &response);
        ret = BosResponseToPoxisError(ret, &response);
        if (ret != 0) {
            return ret;
        }

        std::vector< ::bce::bos::Content> content;
        response.GetContents(&content);

        for (uint32_t i = 0; i < content.size(); i++) {
            children->push_back(content[i].key);
        }

        bool is_truncated;
        response.GetIsTruncated(&is_truncated);
        if (!is_truncated) {
            break;
        }

        response.GetNextMarker(&next_marker);
    }

    return 0;
}

static int AbortMultipartUpload(::bce::bos::Client &bos_client, const std::string &bucket,
        const std::string &object, const std::string &upload_id) {
    ::bce::bos::AbortMultipartUploadRequest request(bucket, object, upload_id);
    ::bce::bos::AbortMultipartUploadResponse  response;
    int ret = bos_client.AbortMultipartUpload(request, &response);
    ret = BosResponseToPoxisError(ret, &response);

    return ret;
}

static int ListMultipartUpload(::bce::bos::Client &bos_client, const std::string &bucket,
        const std::string &object, std::vector<std::string> *upload_ids) {
    ::bce::bos::ListMultipartUploadsRequest request(bucket);
    request.AddMaxUploads(1000);
    std::string next_marker = "";
    std::string key(object.c_str() + 1);

    request.AddPrefix(object);

    while (true) {
        request.AddKeyMarker(next_marker);
        ::bce::bos::ListMultipartUploadsResponse  response;
        int ret = bos_client.ListMultipartUploads(request, &response);
        ret = BosResponseToPoxisError(ret, &response);
        if (ret != 0) {
            return ret;
        }

        std::vector< ::bce::bos::Upload> uploads;
        response.GetUploads(&uploads);

        for (uint32_t i = 0; i < uploads.size(); i++) {
            if (uploads[i].m_key != object) {
                break;
            }

            upload_ids->push_back(uploads[i].m_upload_id);
        }

        bool is_truncated;
        response.GetIsTruncated(&is_truncated);
        if (!is_truncated) {
            break;
        }

        response.GetNextKeyMarker(&next_marker);
    }
}

int BosFsClient::Unlink(const std::string &path) {
    // RecursiveLockGuard guard(&m_lock);
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);

    struct stat attr;
    int ret = Getattr(path, &attr);
    if (ret != 0) {
        return ret;
    }

    if (S_IFDIR & attr.st_mode) {
        return -EISDIR;
    }

    std::string object(path.c_str() + 1);
    // disable all writing
    std::vector<std::string> upload_ids;
    ret = ListMultipartUpload(bos_client, m_bucket, object, &upload_ids);
    if (ret != 0) {
        return ret;
    }

    for (uint32_t i = 0; i < upload_ids.size(); i++) {
        ret = AbortMultipartUpload(bos_client, m_bucket, object, upload_ids[i]);
        if (ret != 0) {
            return ret;
        }
    }

    ret = DeleteObject(bos_client, m_bucket, object);
    if (ret != 0) {
        return ret;
    }

    std::vector<std::string> flags_files;
    ret = ListObject(bos_client, m_bucket, object + "#", &flags_files);
    if (ret != 0) {
        return ret;
    }

    for (uint32_t i = 0; i < flags_files.size(); i++) {
        DeleteObject(bos_client, m_bucket, flags_files[i]);
    }

    return 0;
}

int BosFsClient::Rmdir(const std::string &path) {
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);

    struct stat attr;
    int ret = Getattr(path, &attr);
    if (ret != 0) {
        return ret;
    }

    if (S_IFREG & attr.st_mode) {
        return -ENOTDIR;
    }

    std::string object(path.c_str() + 1);
    ret = DeleteObject(bos_client, m_bucket, object + "/");
    if (ret != 0) {
        return ret;
    }

    ret = DeleteObject(bos_client, m_bucket, object);
    if (ret != 0) {
        return ret;
    }

    std::vector<std::string> flags_files;
    ret = ListObject(bos_client, m_bucket, object + "#", &flags_files);
    if (ret != 0) {
        return ret;
    }

    for (uint32_t i = 0; i < flags_files.size(); i++) {
        DeleteObject(bos_client, m_bucket, flags_files[i]);
    }
    return 0;
}

int BosFsClient::Chmod(const std::string &path, mode_t mode) {
    RecursiveLockGuard guard(&m_lock);
    return 0;
}

int BosFsClient::Chown(const std::string &path, uid_t uid, gid_t gid) {
    RecursiveLockGuard guard(&m_lock);
    return 0;
}

BosFileHandler *BosFsClient::Open(const std::string &path, int flags) {
    LOG(INFO) << "Open file:" << path << ", flags:" << flags;
    RecursiveLockGuard guard(&m_lock);
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    /*
    BosFileHandler *file_handler = GetOpenFileHandler(path);
    if (file_handler != NULL) {
        return file_handler;
    }
    */

    BosFileHandler *handler = CreateFileHandler(path, flags);
    if ((flags & O_ACCMODE) == O_RDONLY) {
        if (Getattr(path, handler->Getattr())) {
            delete handler;
            return NULL;
        }

        handler->AddRef();
        m_open_files.insert(std::make_pair(path, handler));
        return handler;
    } else {
        // for write
        Getattr(path, handler->Getattr());
        /*
        if (handler->Getattr()->st_size != 0 && (handler->GetFlags() & O_TRUNC) == 0) {
            handler->SetError(-EPERM);
            return handler;
        }
        */
        ::bce::bos::InitMultiUploadRequest request(m_bucket, std::string(path));
        ::bce::bos::InitMultiUploadResponse response;
        int ret = bos_client.InitMultiUpload(request, &response);
        ret = BosResponseToPoxisError(ret, &response); 

        if (ret != 0) {
            delete handler;
            return NULL;
        }

        std::string upload_id;
        response.GetUploadId(&upload_id);
        handler->SetUploadId(upload_id);

        handler->AddRef();
        m_open_files.insert(std::make_pair(path, handler));
        return handler;
    }

    return NULL;
}

BosFileHandler *BosFsClient::Create(const std::string &path, mode_t mode) {
    LOG(INFO) << "Create file:" << path << ", mode:" << mode;
    RecursiveLockGuard guard(&m_lock);
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    BosFileHandler *handler = CreateFileHandler(path, O_RDWR);
    int ret = Getattr(path, handler->Getattr());
    if (ret != -ENOENT) {
        handler->SetError(ret);
        return handler;
    }

    ::bce::bos::InitMultiUploadRequest request(m_bucket, std::string(path));
    ::bce::bos::InitMultiUploadResponse response;
    ret = bos_client.InitMultiUpload(request, &response);
    ret = BosResponseToPoxisError(ret, &response); 

    if (ret != 0) {
        delete handler;
        return NULL;
    }

    std::string upload_id;
    response.GetUploadId(&upload_id);
    handler->SetUploadId(upload_id);

    InitFileAttr(handler->Getattr());
    handler->Getattr()->st_mode = mode | S_IFREG;

    handler->AddRef();
    m_open_files.insert(std::make_pair(path, handler));
    handler->SetExistInBos(false);

    return handler;
}

ssize_t BosFsClient::Read(BosFileHandler *handler, void *buffer, off_t offset, size_t length) {
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    if (offset > handler->Getattr()->st_size) {
        return 0;
    }

    if (static_cast<uint64_t>(length + offset) > handler->Getattr()->st_size) {
        length = handler->Getattr()->st_size - offset;
    }

    if (length == 0) {
        return length;
    }

    int page_index = offset / m_read_page_size;
    int page_start = offset % m_read_page_size;
    size_t read_length = 0;
    while (read_length < length) {
        int page_length = m_read_page_size - page_start;
        if (page_length + read_length > length) {
            page_length = length - read_length;
        }

        handler->Lock();
        std::string *page_cache = handler->GetReadPageCache(page_index);
        if (page_cache->size() == 0) {
            page_cache->reserve(m_read_page_size);
            ::bce::bos::StringOutputStream out_stream(page_cache);

            ::bce::bos::GetObjectRequest request(m_bucket, handler->GetFileName());
            ::bce::bos::GetObjectResponse response(&out_stream);
            request.SetRange(page_index * m_read_page_size, m_read_page_size);

            int ret = bos_client.GetObject(request, &response);
            ret = BosResponseToPoxisError(ret, &response); 

            if (ret != 0) {
                handler->UnLock();
                return ret;
            }
        }

        memcpy(reinterpret_cast<char *>(buffer) + read_length,
                page_cache->c_str() + page_start, page_length);

        handler->UnLock();

        page_index++;
        page_start = 0;
        read_length += page_length;
    }


    return length;
}

ssize_t BosFsClient::Write(BosFileHandler *handler, const void *buf, off_t offset, size_t length) {
    if (offset != handler->GetWriteSize()) {
        handler->SetError(-EPERM);
        return -1;
    }

    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    handler->GetWriteBuffer()->append(reinterpret_cast<const char *>(buf), length);
    handler->SetDirty(true);
    handler->IncWriteSize(length);
    if (handler->GetWriteBuffer()->size() < kCachePageSize) {
        return length;
    }

    UploadPart(handler, true);

    return length;
}

int BosFsClient::WriteBlock(BosFileHandler *handler, int index, const std::string &content) {
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    LOG(INFO) << "filename:" << handler->GetFileName()
        << ", index:" << index << ", upload_id:" << handler->GetUploadId();
    ::bce::bos::UploadPartRequest request(m_bucket, handler->GetFileName(), content,
            index, handler->GetUploadId());
    ::bce::bos::UploadPartResponse response;
    int ret = bos_client.UploadPart(request, &response);
    ret = BosResponseToPoxisError(ret, &response);

    if (ret != 0) {
        return ret;
    }

    std::string etag;
    response.GetETag(&etag);
    LOG(INFO) << "filename:" << handler->GetFileName()
        << ", index:" << index << ", upload_id:" << handler->GetUploadId()
        << ", etag:" << etag;

    handler->AddPartInfo(index, etag);

    return 0;
}

size_t BosFsClient::UploadPart(BosFileHandler *handler, bool clean) {
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    int index = handler->Index();

    int ret = WriteBlock(handler, index, *(handler->GetWriteBuffer()));
    if (ret == 0) {
        if (clean) {
            handler->GetWriteBuffer()->clear();
            handler->NextIndex();
        }

        handler->Getattr()->st_mtime = time(NULL);
        handler->SetDirty(false);
    }

    return ret;
}

int BosFsClient::Close(BosFileHandler *handler) {
    std::map<int, std::string> parts_info;
    std::string filename;
    std::string upload_id;
    int mode;
    {
        RecursiveLockGuard guard(&m_lock);
        if ((handler->GetFlags() & O_ACCMODE) == O_RDONLY) {
            handler->Release();
            return 0;
        }

        if (handler->GetWriteSize() == 0 && handler->ExistInBos() == true
                && (handler->GetFlags() & O_TRUNC) == 0) {
            handler->Release();
            return 0;
        }

        LOG(INFO) << "write size:" << handler->GetWriteSize()
            << ", ExistInBos:" << handler->ExistInBos()
            << ", Flags:" << (handler->GetFlags() & O_TRUNC);

        if (handler->IsDirty()) {
            while (UploadPart(handler, true) != 0) {
            }
        }

        filename = handler->GetFileName();
        parts_info.insert(handler->GetPartsInfo().begin(), handler->GetPartsInfo().end());
        upload_id = handler->GetUploadId();
        mode = handler->GetMode();

        handler->Release();
    }

    LOG(INFO) << "begin commit: upload_id:" << upload_id
        << ", filename:" << filename;

    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    ::bce::bos::CompleteMultipartUploadRequest request(m_bucket,
            filename, upload_id);
    ::bce::bos::CompleteMultipartUploadResponse response;

    for (std::map<int, std::string>::const_iterator it = parts_info.begin();
            it != parts_info.end(); it++) {
        LOG(INFO) << "add part: upload_id:" << upload_id << ", index:" << it->first
            << ", etag:" << it->second;
        request.AddPart(it->first, it->second);
    }

    request.AddUserMetaData("x-bce-meta-fs-mode", NumberToString(mode));
    request.AddUserMetaData("x-bce-meta-fs-file-type", "reg");

    int ret = 0;
    while (true) {
        int ret = bos_client.CompleteMultipartUpload(request, &response);
        ret = BosResponseToPoxisError(ret, &response); 

        if (ret == 0) {
            break;
        }

        if (ret == -ENOENT) {
            return 0;
        }
    }

    std::stringstream ss;
    ss << filename << "#" << mode << "#" << 0 << "#" << time(NULL);
    ::bce::bos::PutObjectRequest mode_request(g_fs_config.GetBucket(), ss.str(), "");
    ::bce::bos::PutObjectResponse mode_response;

    while (true) {
        ret = bos_client.PutObject(mode_request, &mode_response);
        ret = BosResponseToPoxisError(ret, &mode_response);
        if (ret == 0) {
            break;
        }
    }

    return ret;
}

BosDirHandler *BosFsClient::OpenDir(const std::string &path) {
    ::bce::bos::Client bos_client(m_ak, m_sk, m_client_options);
    // get self and parent
    BosDirHandler *dir_handler = new BosDirHandler(path);
    struct stat attr;
    Getattr(path, &attr);
    dir_handler->AddItem(".", attr);

    int last_slash_index = path.size()- 1;
    while (last_slash_index > 0) {
        if (path[last_slash_index] == '/') {
            std::string parent = std::string(path, last_slash_index);
            Getattr(path, &attr);
            break;
        }

        last_slash_index--;
    }
    dir_handler->AddItem("..", attr);

    // get children
    ::bce::bos::ListObjectsRequest request(m_bucket);
    request.SetDelimiter("/");
    request.SetMaxKeys(1000);
    std::string next_marker = "";

    std::string prefix(path.c_str() + 1);
    if (prefix.size() != 0) {
        prefix = prefix + "/";
    }
    request.SetPrefix(prefix);

    while (true) {
        ::bce::bos::ListObjectsResponse  response;
        int ret = bos_client.ListObjects(request, &response);
        ret = BosResponseToPoxisError(ret, &response);
        if (ret != 0) {
            dir_handler->SetError(ret);
            return dir_handler;
        }

        std::vector< ::bce::bos::Content> content;
        response.GetContents(&content);

        std::vector<std::string> files;
        for (uint32_t i = 0; i < content.size(); i++) {
            files.push_back(content[i].key);
        }

        std::vector<std::string> dirs;
        response.GetCommonPrefixes(&dirs);

        dir_handler->AddListInfo(files, dirs);

        bool is_truncated;
        response.GetIsTruncated(&is_truncated);
        if (!is_truncated) {
            break;
        }

        response.GetNextMarker(&next_marker);
        request.SetMarker(next_marker);
    }

    dir_handler->ParseListInfo();

    return dir_handler;
}

int BosFsClient::ReadDir(BosDirHandler *handler, std::string * filename, struct stat *attr) {
    FileStat file_stat;
    std::string full_filename;
    if (handler->Item(&full_filename, &file_stat)) {
        return 0;
    }
    handler->Next();

    int prefix_len = handler->GetPath().size();
    if (full_filename != "." && full_filename != ".." && prefix_len != 1) {
        filename->assign(full_filename.c_str() + prefix_len);
    } else {
        filename->assign(full_filename);
    }

    memset(attr, 0, sizeof(*attr));

    attr->st_nlink = 1;
    attr->st_uid = g_fs_config.GetLocalUid();
    attr->st_gid = g_fs_config.GetLocalGid();

    if (file_stat.type == 0) {
        attr->st_mode = file_stat.mode | S_IFDIR;
    } else {
        attr->st_mode = file_stat.mode | S_IFREG;
        attr->st_size = file_stat.size;
    }

    attr->st_mtime = file_stat.create_time;

    // add for attr cache
    // when user use ls -l, application will call many lstate after readdir
    /*
    full_filename = std::string("/") + full_filename;
    BosFileHandler *attr_cache_handler = CreateFileHandler(full_filename, O_RDONLY);
    memcpy(attr_cache_handler->Getattr(), attr, sizeof(*attr));
    m_open_files.insert(std::make_pair(full_filename, attr_cache_handler));
    LOG(INFO) << "readdir, path:" << full_filename << ", size:" << attr->st_size;
    */

    return 1;
}

int BosFsClient::CloseDir(BosDirHandler *handler) {
    return 0;
}

void *BosFsClient::TriggerWorker(void *args) {
    BosFsClient *fs_client = reinterpret_cast<BosFsClient *>(args);
    sleep(1);
    for (;;) {
        fs_client->DoTrigger();
        usleep(500 * 1000);
    }

    return (void *)NULL;
}

void BosFsClient::DoTrigger() {
    for(std::map<std::string, BosFileHandler *>::iterator it = m_open_files.begin();
            it != m_open_files.end();) {
        BosFileHandler *file_handler = it->second;
        OnFileHandlerTrigger(file_handler);

        if (file_handler->GetRefCount() == 1) {
            m_open_files.erase(it++);
            file_handler->Release();
        } else {
            it++;
        }
    }
}

int  BosFsClient::Truncate(const std::string &path, off_t off) {
    int ret = 0;
    BosFileHandler *handler = NULL;
    do {
        if (off != 0) {
            ret = -ENOSYS;
            break;
        }

        handler = BosFsClient::Open(path, O_WRONLY | O_TRUNC);
        if (handler == NULL) {
            ret = -EIO;
            break;
        }

        if (handler->GetError() != 0) {
            ret = handler->GetError();
            break;
        }

        BosFsClient::Write(handler, "", 0, 0);
        if (handler->GetError() != 0) {
            ret = handler->GetError();
            break;
        }
    } while (false);

    if (handler != NULL) {
       while (BosFsClient::Flush(handler) != 0) {
       }

        ret = BosFsClient::Close(handler);
    }

    return ret;
}

RecursiveLock *BosFsClient::GetFileLock(const std::string &filename) {
    RecursiveLockGuard guard(&m_lock);
    std::map<std::string, FileLock *>::iterator it = m_file_lock_manager.find(filename);
    if (it == m_file_lock_manager.end()) {
        it->second->ref++;
        return it->second->lock;
    }

    FileLock *lock = new FileLock();
    lock->ref++;
    m_file_lock_manager[filename] = lock;

    return lock->lock;
}

void BosFsClient::ReleaseFileLock(const std::string &filename, RecursiveLock *lock) {
    RecursiveLockGuard guard(&m_lock);
    std::map<std::string, FileLock *>::iterator it = m_file_lock_manager.find(filename);
    assert(it != m_file_lock_manager.end());

    FileLock *file_lock = it->second;
    file_lock->ref--;
    if (file_lock->ref == 0) {
        delete file_lock;
        m_file_lock_manager.erase(it);
    }
}

int BosFsClient::Flush(BosFileHandler *handler) {
    if ((handler->GetFlags() & O_ACCMODE) == O_RDONLY) {
        // handler->Release();
        return 0;
    }

    return UploadPart(handler, false);
}

END_FS_NAMESPACE
