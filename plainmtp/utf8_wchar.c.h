#ifndef ZZ_PLAINMTP_UTF8_WCHAR_H_IG
#define ZZ_PLAINMTP_UTF8_WCHAR_H_IG
#include "common.i.h"

#include <wchar.h>

extern size_t utf8_strlen( const char* utf8_string );
extern wchar_t* make_wide_string_from_utf8( const char* utf8_string, size_t* OUT_length );
extern char* make_multibyte_string( const wchar_t* source );

#endif /* ZZ_PLAINMTP_UTF8_WCHAR_H_IG */
