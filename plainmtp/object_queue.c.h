#ifndef ZZ_PLAINMTP_OBJECT_QUEUE_C_IG
#define ZZ_PLAINMTP_OBJECT_QUEUE_C_IG
#include "common.i.h"

#include "../3rdparty/pstdint.h"

typedef struct zz_plainmtp_object_queue_s object_queue_s;

typedef struct zz_plainmtp_object_queue_item_s {
  uint32_t storage_id;
  uint32_t object_handle;
} object_queue_item_s;

PLAINMTP_EXTERN object_queue_s* PLAINMTP(object_queue_create( size_t capacity ));
PLAINMTP_EXTERN object_queue_s* PLAINMTP(object_queue_push( object_queue_s* data,
  uint32_t storage_id, uint32_t object_handle ));
PLAINMTP_EXTERN plainmtp_bool PLAINMTP(object_queue_pop( object_queue_s* data,
  object_queue_item_s* value ));

#else
#error ZZ_PLAINMTP_OBJECT_QUEUE_C_IG
#endif
