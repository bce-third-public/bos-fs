#include "log.h"

#include <stdio.h>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>

#include "util/bos_log.h"

BEGIN_FS_NAMESPACE
static int LogToStderr(const std::string &message) {
    // return fprintf(stderr, "%s\n", message.c_str());
    return 0;
}

int InitSdkLog() {
    ::bce::bos::Log::UserPrint = LogToStderr;
    return 0;
}

END_FS_NAMESPACE
