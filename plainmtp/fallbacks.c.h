#ifndef ZZ_PLAINMTP_FALLBACKS_H_IG
#define ZZ_PLAINMTP_FALLBACKS_H_IG
#include "common.i.h"

#include <wchar.h>

/* TODO: Support weak symbol linking for these functions on some platforms?
  https://stackoverflow.com/questions/2290587/gcc-style-weak-linking-in-visual-studio */

#ifdef PLAINMTP_FALLBACK_WCSDUP
  extern wchar_t* zz_plainmtp_wcsdup( const wchar_t* );
#else
  #define zz_plainmtp_wcsdup wcsdup
#endif

#endif /* ZZ_PLAINMTP_FALLBACKS_H_IG */
