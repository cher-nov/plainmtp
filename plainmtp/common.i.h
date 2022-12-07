#ifndef ZZ_PLAINMTP_COMMON_H_IG
#define ZZ_PLAINMTP_COMMON_H_IG

#include "global.i.h"

typedef enum {
  PLAINMTP_GOOD = PLAINMTP_FALSE,
  PLAINMTP_BAD = PLAINMTP_TRUE,
  PLAINMTP_NONE = ~PLAINMTP_FALSE  /* "nicht einmal falsch", lol */
} plainmtp_3val;  /* "three-valued" or "trivalent" */

/**************************************************************************************************/

#ifndef __cplusplus
  #define PLAINMTP_EXTERN extern
#else
  #define PLAINMTP_EXTERN extern "C"
#endif

#endif /* ZZ_PLAINMTP_COMMON_H_IG */
