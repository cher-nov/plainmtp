#ifndef ZZ_PLAINMTP_GLOBAL_H_IG
#define ZZ_PLAINMTP_GLOBAL_H_IG

typedef enum zz_plainmtp_bool {
  PLAINMTP_FALSE = 0,
  PLAINMTP_TRUE = !PLAINMTP_FALSE
} plainmtp_bool;

/**************************************************************************************************/

#define ZZ_PLAINMTP_TOKEN_JOIN( Stream_A, Stream_B ) Stream_A ## Stream_B
#define ZZ_PLAINMTP_MACRO_JOIN( Symbol_A, Symbol_B ) ZZ_PLAINMTP_TOKEN_JOIN( Symbol_A, Symbol_B )

/**************************************************************************************************/

#ifndef PLAINMTP_INTERNAL_PREFIX
  #define PLAINMTP_INTERNAL_PREFIX libplainmtp_
#endif

#define PLAINMTP( Identifier ) ZZ_PLAINMTP_MACRO_JOIN( PLAINMTP_INTERNAL_PREFIX, Identifier )

#endif /* ZZ_PLAINMTP_GLOBAL_H_IG */
