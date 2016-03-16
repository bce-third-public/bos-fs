#include "common_def.h"
#include "fs_config.h"

#include <assert.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#include <grp.h>

#include <gflags/gflags.h>

DEFINE_string(bucket, "cuican", "you bucket name");
DEFINE_string(endpoint, "bos.qasandbox.bcetest.baidu.com", "you endpoint");
DEFINE_string(ak, "fe958d90ca964f4193bc3a468f55fa8f", "you access key");
DEFINE_string(sk, "c5fbf4af89aa4e59a98bb8539f6815a1", "you access secret");

DEFINE_string(local_user, "work", "local user name");
DEFINE_string(local_group, "work", "local group name");

DEFINE_string(cache_root, "", "local group name");
DEFINE_bool(use_cache, false, "use cache");

BEGIN_FS_NAMESPACE
FsConfig::FsConfig() {
    m_bucket = FLAGS_bucket;
    m_endpoint = FLAGS_endpoint;
    m_ak = FLAGS_ak;
    m_sk = FLAGS_sk;

    m_local_user = FLAGS_local_user;
    m_local_group = FLAGS_local_group;

    if (m_local_user != "") {
        struct passwd *passwd = getpwnam(m_local_user.c_str());
        assert(passwd != NULL);

        m_local_uid = passwd->pw_uid;
        m_local_gid = passwd->pw_gid;
    } else {
        m_local_uid = getuid();
        m_local_gid = getgid();
    }

    if (m_local_group != "") {
        struct group *group = getgrnam(m_local_group.c_str());
        if (group != NULL) {
            m_local_gid = group->gr_gid;
        }
    }

    m_cache_dir = FLAGS_cache_root;
    m_use_cache = FLAGS_use_cache;
    printf("use_cache:%d\n", m_use_cache);
}

FsConfig g_fs_config;
END_FS_NAMESPACE
