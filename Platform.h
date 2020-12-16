
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdio.h> // +
#include <time.h> // +
#include <limits.h> // +
#include <string>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
//#include <string.h>
//#include <unistd.h>
#include <signal.h>
//#include <math.h>
#include "sys/types.h"
#include "sys/stat.h"
#include <fcntl.h>
#include <fstream>
#include <thread>
//#include "sbc_api.h"
//#include "dlog.h"

/// Debug logging helpers
//#define DBG_LOG(level, format, ...)  dlog(level, "%s(%d)," format "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DBG_LOG(level, format, ...) DLOG(level, format"\n", ##__VA_ARGS__)

#endif // PLATFORM_H
