#ifndef ZZ_PLAINMTP_WCSDUP_H_IG
#define ZZ_PLAINMTP_WCSDUP_H_IG

#include <wchar.h>

/* TODO: Use the default wcsdup() implementation if available on the target platform. The current
  one should be made a weak symbol and only applied as a fallback using conditional compilation. */

extern wchar_t* wcsdup( const wchar_t* );

#endif /* ZZ_PLAINMTP_WCSDUP_H_IG */
