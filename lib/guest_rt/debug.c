#include <stdarg.h>
#include <stdio.h>

__attribute__((weak)) void
__gyh_debug__(const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}
