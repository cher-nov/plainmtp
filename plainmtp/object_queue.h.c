#include "object_queue.c.h"

/**************************************************************************************************/
#ifndef PP_PLAINMTP_CONFLICTING_DIRECTIVES

#define CALCULATE_BUFFER_SIZE( Capacity ) \
  ( sizeof( object_queue_s ) + (Capacity) * sizeof( object_queue_item_s ) )

#define ACCESS_ELEMENTS( Queue ) \
  ( (object_queue_item_s*) ((Queue)+1) )

#else
#undef PP_PLAINMTP_CONFLICTING_DIRECTIVES
#endif
/**************************************************************************************************/

struct ZZ_PLAINMTP(object_queue_s) {
  size_t first;
  size_t next;
  size_t capacity;
};
