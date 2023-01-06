#ifndef ZZ_PLAINMTP_OBJECT_QUEUE_H_IG
#define ZZ_PLAINMTP_OBJECT_QUEUE_H_IG
#include "common.i.h"

#include "../3rdparty/pstdint.h"

typedef struct zz_object_queue_s object_queue_s;

typedef struct zz_object_queue_item_s {
  uint32_t storage_id;
  uint32_t object_handle;
} object_queue_item_s;

extern object_queue_s* object_queue_create( size_t capacity );
extern object_queue_s* object_queue_push( object_queue_s* data, uint32_t storage_id,
  uint32_t object_handle );
extern plainmtp_bool object_queue_pop( object_queue_s* data, object_queue_item_s* value );

#endif /* ZZ_PLAINMTP_OBJECT_QUEUE_H_IG */
