#ifndef BAIDU_BCE_BOS_FS_COMMON_DEF_H_
#define BAIDU_BCE_BOS_FS_COMMON_DEF_H_
#define FUSE_USE_VERSION 26
#include <stdio.h>

#define BEGIN_FS_NAMESPACE \
    namespace baidu {\
    namespace bce {\
    namespace bos {\
    namespace fs {

#define END_FS_NAMESPACE }}}}

#define fs_ns baidu::bce::bos::fs

const int kCachePageSize = 1024 * 1024 * 5;

#include "log.h"
#endif //BAIDU_BCE_BOS_FS_COMMON_DEF_H_
