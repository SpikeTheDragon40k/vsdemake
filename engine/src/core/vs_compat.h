/* Compiler shims. The core is C99; the Xbox 360 XDK compiler (Visual C++
 * 2010) has no C99 mode, so the XDK project compiles the core as C++ (/TP)
 * and these macros cover the few library differences. */
#ifndef VS_COMPAT_H
#define VS_COMPAT_H

#if defined(_MSC_VER) && _MSC_VER < 1900
#include <stdio.h>
#include <stdlib.h>
#ifndef snprintf
#define snprintf _snprintf
#endif
#define strtoull _strtoui64
#ifndef inline
#define inline __inline
#endif
#endif

#endif
