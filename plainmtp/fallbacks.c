#include "fallbacks.c.h"

#include <stdlib.h>

#ifndef zz_plainmtp_wcsdup
wchar_t* zz_plainmtp_wcsdup( const wchar_t* string ) {
  const size_t length = wcslen( string ) + 1;
  wchar_t* result = malloc( length * sizeof(*result) );
  if (result == NULL) { return NULL; }
  return wmemcpy( result, string, length );
}
#endif

#ifdef PLAINMTP_FALLBACKS_C_EX
#include PLAINMTP_FALLBACKS_C_EX
#endif
