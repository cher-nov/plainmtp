#include "object_queue.h.c"

#include <stdlib.h>
#include <string.h>

#define object_queue_create PLAINMTP(object_queue_create)
object_queue_s* object_queue_create( size_t capacity ) {
  object_queue_s* result;
{
  if (capacity == 0) {
    /* 4096 bytes (4 KiB) is a typical memory page size. */
    capacity = (4096 - sizeof(object_queue_s)) / sizeof(object_queue_item_s);
  }

  result = malloc( CALCULATE_BUFFER_SIZE( capacity ) );
  if (result == NULL) { return NULL; }

  result->first = 0;
  result->next = 0;
  result->capacity = capacity;

  return result;
}}

#define object_queue_push PLAINMTP(object_queue_push)
object_queue_s* object_queue_push( object_queue_s* data, uint32_t storage_id,
  uint32_t object_handle
) {
  object_queue_item_s* value;
  object_queue_item_s* elements = ACCESS_ELEMENTS( data );
  const size_t count = data->next - data->first;
{
  if ( (data->first >= count) && (count > 0) ) {
    /* There's enough previously used space to copy all current elements without overlap. */
    memcpy( elements, elements + data->first, count * sizeof(object_queue_item_s) );

  } else if (data->next == data->capacity) {
    /* Golden ratio approximation. */
    const size_t capacity = (data->capacity + 1) / 2 + data->capacity;
    data = realloc( data, CALCULATE_BUFFER_SIZE(capacity) );

    if (data != NULL) {
      elements = ACCESS_ELEMENTS( data );
      data->capacity = capacity;
      goto insert;
    }

    /* Last ditch attempt to obtain space in the most inefficient way. */
    if (data->first == 0) { return NULL; }
    memmove( elements, elements + data->first, count * sizeof(object_queue_item_s) );

  } else
    goto insert;

  data->next -= data->first;
  data->first = 0;

insert:
  value = elements + data->next++;
  value->storage_id = storage_id;
  value->object_handle = object_handle;

  return data;
}}

#define object_queue_pop PLAINMTP(object_queue_pop)
plainmtp_bool object_queue_pop( object_queue_s* data, object_queue_item_s* value ) {
{
  if (data->first == data->next) { return PLAINMTP_FALSE; }
  *value = ACCESS_ELEMENTS( data ) [data->first++];
  return PLAINMTP_TRUE;
}}

#ifdef PP_PLAINMTP_OBJECT_QUEUE_C_EX
#include PP_PLAINMTP_OBJECT_QUEUE_C_EX
#endif
