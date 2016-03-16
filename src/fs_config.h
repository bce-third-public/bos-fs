#ifndef BAIDU_BCE_BOS_FS_CONFIG_H_
#define BAIDU_BCE_BOS_FS_CONFIG_H_
#include "common_def.h"

#include <sys/types.h>

#include <string>

BEGIN_FS_NAMESPACE
class FsConfig {
public:
    FsConfig();

    std::string GetBucket() const {
        return m_bucket;
    }

    std::string GetEndpoint() const {
        return m_endpoint;
    }

    std::string GetAk() const {
        return m_ak;
    }

    std::string GetSk() const {
        return m_sk;
    }

    uid_t GetLocalUid() const {
        return m_local_uid;
    }

    gid_t GetLocalGid() const {
        return m_local_gid;
    }

    const std::string &GetCacheDir() const {
        return m_cache_dir;
    }

    bool UseCache() const {
        return m_use_cache;
    }

private:
    std::string m_bucket;
    std::string m_endpoint;
    std::string m_ak;
    std::string m_sk;

    std::string m_local_user;
    std::string m_local_group;

    uid_t m_local_uid;
    gid_t m_local_gid;

    std::string m_cache_dir;
    bool m_use_cache;
};

extern FsConfig g_fs_config;

END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_FS_CONFIG_H_
