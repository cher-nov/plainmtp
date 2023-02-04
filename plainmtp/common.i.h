#ifndef ZZ_PLAINMTP_COMMON_H_IG
#define ZZ_PLAINMTP_COMMON_H_IG

#include "global.i.h"

typedef enum {
  PLAINMTP_GOOD = PLAINMTP_FALSE,
  PLAINMTP_BAD = PLAINMTP_TRUE,
  PLAINMTP_NONE = ~PLAINMTP_FALSE  /* "nicht einmal falsch", lol */
} plainmtp_3val;  /* "three-valued" or "trivalent" */

/**************************************************************************************************/

#define ZZ_PLAINMTP_SUBCLASS_END( Elements ) Elements }
#define ZZ_PLAINMTP_SUBCLASS( Name, Super_Field ) \
  struct Name { struct zz_## Name Super_Field; \
  ZZ_PLAINMTP_SUBCLASS_END

/**************************************************************************************************/

#ifndef __cplusplus
  #define PLAINMTP_EXTERN extern
#else
  #define PLAINMTP_EXTERN extern "C"
#endif

#ifndef PLAINMTP_NO_INTERNAL_API
  #define PLAINMTP_INTERNAL
#else
  #define PLAINMTP_INTERNAL static
#endif

#define ZZ_PLAINMTP( Identifier ) zz_plainmtp_## Identifier

#define PLAINMTP_SUBCLASS( Specifier, Super_Field ) ZZ_PLAINMTP_MACRO_EXPAND( JOIN, \
  ZZ_PLAINMTP_SUBCLASS, ( ZZ_PLAINMTP_TAG_NAME__## Specifier, Super_Field ) \
)

#endif /* ZZ_PLAINMTP_COMMON_H_IG */
