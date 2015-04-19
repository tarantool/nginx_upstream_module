#ifndef DEBUG_H
#define DEBUG_H 1

#include <ngx_config.h>
#include <ngx_core.h>

#if defined(NGX_DEBUG)

# if (NGX_HAVE_VARIADIC_MACROS)
# define dd(...) do { \
        fprintf(stderr, "tnt *** "); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, " at %s line %d.\n", __FILE__, __LINE__); \
      } while(0)
# else

#include <stdarg.h>
#include <stdio.h>
#include <stdarg.h>

static void dd(const char* fmt, ...) {
}

# endif

#else

# if (NGX_HAVE_VARIADIC_MACROS)
# define dd(...)
# else

#include <stdarg.h>

static void dd(const char* fmt, ...) {
}

# endif

#endif // NGX_DEBUG

#endif
