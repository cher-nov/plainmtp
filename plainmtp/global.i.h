#ifndef ZZ_PLAINMTP_GLOBAL_H_IG
#define ZZ_PLAINMTP_GLOBAL_H_IG

typedef enum zz_plainmtp_bool {
  PLAINMTP_FALSE = 0,
  PLAINMTP_TRUE = !PLAINMTP_FALSE
} plainmtp_bool;

/**************************************************************************************************/

#define ZZ_PLAINMTP_TOKEN_JOIN( Stream_A, Stream_B ) Stream_A Stream_B
#define ZZ_PLAINMTP_TOKEN_MERGE( Stream_A, Stream_B ) Stream_A ## Stream_B
#define ZZ_PLAINMTP_MACRO_EXPAND( Action, Symbol_A, Symbol_B ) \
  ZZ_PLAINMTP_TOKEN_## Action ( Symbol_A, Symbol_B )

#define ZZ_PLAINMTP_TAG_NAME__struct
#define ZZ_PLAINMTP_OPAQUE( Name ) struct Name; typedef struct zz_## Name

/**************************************************************************************************/

#ifndef PLAINMTP_INTERNAL_PREFIX
  #define PLAINMTP_INTERNAL_PREFIX libplainmtp_
#endif

#define PLAINMTP( Identifier ) \
  ZZ_PLAINMTP_MACRO_EXPAND( MERGE, PLAINMTP_INTERNAL_PREFIX, Identifier )

#define PLAINMTP_OPAQUE( Specifier ) \
  ZZ_PLAINMTP_MACRO_EXPAND( JOIN, ZZ_PLAINMTP_OPAQUE, ( ZZ_PLAINMTP_TAG_NAME__## Specifier ) )

#endif /* ZZ_PLAINMTP_GLOBAL_H_IG */
