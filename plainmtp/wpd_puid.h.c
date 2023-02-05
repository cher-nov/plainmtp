#include "wpd_puid.c.h"

/**************************************************************************************************/
#ifndef PP_PLAINMTP_CONFLICTING_DIRECTIVES

/* NB: Before C99, adjacent string literals had to be of the same type to be concatenated.
  https://stackoverflow.com/questions/6603275/how-to-convert-defined-string-literal-to-a-wide-string-literal
  https://stackoverflow.com/questions/21788652/how-do-i-concatenate-wide-string-literals-with-prid32-priu64-etc */
#define ZZ_WPRINTF_MODIFIER_JOIN( Prefix, Literal ) Prefix ## Literal
#define ZZ_WPRINTF_MODIFIER_EXPAND( Literal ) ZZ_WPRINTF_MODIFIER_JOIN( L, Literal )
#define WPRINTF_INT32_MODIFIER ZZ_WPRINTF_MODIFIER_EXPAND( PRINTF_INT32_MODIFIER )
#define WPRINTF_INT64_MODIFIER ZZ_WPRINTF_MODIFIER_EXPAND( PRINTF_INT64_MODIFIER )

/* Additional unique identifiers in WPD have the format '?ID-{item_0,item_1,,item_N}', where the
  first character specifies the entity type and the item list specifies some of its properties. */
#define WPD_STORAGE_ID_FORMAT_PREFIX L"SID-{%"WPRINTF_INT32_MODIFIER L"X,"

#define WPD_GUID_FORMAT_LIST \
  X(0),X(1),X(2),X(3),  X(4),X(5),  X(6),X(7),  X(8),X(9),  X(10),X(11),X(12),X(13),X(14),X(15)

#else
#undef PP_PLAINMTP_CONFLICTING_DIRECTIVES
#endif
/**************************************************************************************************/

enum { WPD_GUID_FORMAT_COUNT = 16 };

/**************************************************************************************************/
#ifndef CC_PLAINMTP_NO_INTERNAL_API

PLAINMTP_EXTERN const wchar_t ZZ_PLAINMTP(wpd_guid_format_string[]);

#endif /* CC_PLAINMTP_NO_INTERNAL_API */
