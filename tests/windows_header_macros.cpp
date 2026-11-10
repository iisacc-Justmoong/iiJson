#include <algorithm>
#include <limits>
#include <windows.h>

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#include <iiJson/Schema.h>

#if !defined(min) || !defined(max)
#error "iiJson public headers must preserve the consuming application's macros"
#endif
