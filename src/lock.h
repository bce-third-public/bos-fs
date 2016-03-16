#ifndef BAIDU_BCE_BOS_FS_LOCK_H_
#define BAIDU_BCE_BOS_FS_LOCK_H_
#include <sys/types.h>
#include "common_def.h"

#include <assert.h>
#include <pthread.h>

BEGIN_FS_NAMESPACE
END_FS_NAMESPACE
class RecursiveLock {
public:
    RecursiveLock() {
        assert(pthread_mutexattr_init(&m_attr) == 0);
        assert(pthread_mutexattr_settype(&m_attr, PTHREAD_MUTEX_RECURSIVE) == 0);
        assert(pthread_mutex_init(&m_lock, &m_attr) == 0);
    }

    ~RecursiveLock() {
        assert(pthread_mutex_destroy(&m_lock) == 0);
    }

    void Lock() {
        assert(pthread_mutex_lock(&m_lock) == 0);
    }

    void UnLock() {
        assert(pthread_mutex_unlock(&m_lock) == 0);
    }

private:
    pthread_mutex_t m_lock;
    pthread_mutexattr_t m_attr;
};

class RecursiveLockGuard {
public:
    RecursiveLockGuard(RecursiveLock *lock) {
        m_lock = lock;
        m_lock->Lock();
    }

    ~RecursiveLockGuard() {
        m_lock->UnLock();
    }

private:
    RecursiveLock *m_lock;
};
#endif //BAIDU_INF_BCE_BOS_FS_LOCK_H_
