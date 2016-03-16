#ifndef BAIDU_BCE_BOS_REFABLE_H_
#define BAIDU_BCE_BOS_REFABLE_H_
#include "common_def.h"
#include "stdio.h"
BEGIN_FS_NAMESPACE

class Refable {
public:
    Refable() {
        m_ref = 0;
    }

    virtual ~Refable() {
        fflush(stdout);
    }

    int AddRef() {
        m_ref++;
        return 0;
    }

    int Release() {
        m_ref--;
        if (m_ref == 0) {
            delete this;
        }

        return 0;
    }

    int GetRefCount() const {
        return m_ref;
    }

private:
    int m_ref;
};
END_FS_NAMESPACE

#endif //BAIDU_INF_BCE_BOS_REFABLE_H_
