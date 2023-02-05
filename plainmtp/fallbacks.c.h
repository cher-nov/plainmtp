#ifndef ZZ_PLAINMTP_FALLBACKS_C_IG
#define ZZ_PLAINMTP_FALLBACKS_C_IG
#include "common.i.h"

#include <wchar.h>

/* TODO: Support weak symbol linking for these functions on some platforms?
  https://stackoverflow.com/questions/2290587/gcc-style-weak-linking-in-visual-studio */

#ifdef CC_PLAINMTP_FALLBACK_WCSDUP
  PLAINMTP_EXTERN wchar_t* zz_plainmtp_wcsdup( const wchar_t* );
#else
  #define zz_plainmtp_wcsdup wcsdup
#endif

#else
#error ZZ_PLAINMTP_FALLBACKS_C_IG
#endif
