#include "plainmtp.h"
#include "common.i.h"

/**************************************************************************************************/
#ifndef PLAINMTP_CONFLICTING_DIRECTIVES

#include <libmtp.h>

#include "wpd_puid.c.h"

/* By PTP/MTP standards, the values 0x00000000 and 0xFFFFFFFF are reserved for contextual use for
  both object handles and storage IDs. Alas, this exceeds the 'signed int' range of 'enum' in C. */
#define STORAGE_ID_NULL (0x00000000)
#define OBJECT_HANDLE_NULL LIBMTP_FILES_AND_FOLDERS_ROOT  /* See set_object_values(). */

#define CURSOR_HAS_ENUMERATION( Cursor ) \
  !( ( (Cursor)->enumeration == NULL ) || ( (Cursor)->enumeration == (Cursor) ) )

#define CURSOR_HAS_STORAGE_ID( Cursor ) \
  !( (Cursor)->values.storage_id == STORAGE_ID_NULL )

#define WSTRING_PRINTABLE( String ) \
  !( ( (String) == NULL ) || ( (String)[0] == L'\0' ) )

#else
#undef PLAINMTP_CONFLICTING_DIRECTIVES
#endif
/**************************************************************************************************/

typedef char* (*device_info_string_f) (
  LIBMTP_mtpdevice_t* );

typedef enum ZZ_PLAINMTP(cursor_entity_e) {
  CURSOR_ENTITY_DEVICE,
  CURSOR_ENTITY_STORAGE,
  CURSOR_ENTITY_OBJECT
} cursor_entity_e;

typedef struct ZZ_PLAINMTP(file_exchange_s) {
  plainmtp_data_f callback;
  size_t chunk_limit;
  void* custom_state;
} file_exchange_s;

typedef struct ZZ_PLAINMTP(entity_location_s) {
  uint32_t storage_id;
  uint32_t object_handle;
  uint32_t parent_handle;
} entity_location_s;

typedef struct ZZ_PLAINMTP(storage_enumeration_s) {
  uint32_t id;
  zz_plainmtp_cursor_s entity;

  /* The node refers to itself if the following storages could not be retrieved. */
  struct ZZ_PLAINMTP(storage_enumeration_s)* next;
} storage_enumeration_s;

PLAINMTP_SUBCLASS( struct plainmtp_context_s, device_list ) (
  LIBMTP_raw_device_t* hardware_list;
);

struct plainmtp_device_s {
  LIBMTP_mtpdevice_t* libmtp_socket;
  plainmtp_bool read_only;
};

PLAINMTP_SUBCLASS( struct plainmtp_cursor_s, current_entity ) (
  /* Contains undefined values if no enumeration in progress. */
  zz_plainmtp_cursor_s parent_entity;

  /*
    The members of 'values' field have the following additional semantics:
    - storage_id - Contains STORAGE_ID_NULL if the cursor represents the device root.
    - object_handle - Contains OBJECT_HANDLE_NULL if the cursor doesn't point to an object.
    - parent_handle - Contains OBJECT_HANDLE_NULL if the current object is in the storage root.
  */

  entity_location_s values;

  /*
    Contains NULL if the last enumeration was finished without any errors.
    When there's an enumeration, it can refer to a value of one of the following types:
    - LIBMTP_file_t - when enumerating objects.
    - storage_enumeration_s - when enumerating storages.
    - struct plainmtp_cursor_s (an owner) - if the last enumeration has failed (Default value).
  */

  void* enumeration;
);

/**************************************************************************************************/
#ifndef PLAINMTP_NO_INTERNAL_API

PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(is_libmtp_initialized);

PLAINMTP_EXTERN void ZZ_PLAINMTP(set_object_values( entity_location_s* descriptor,
  const LIBMTP_file_t* object ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(set_storage_values( entity_location_s* descriptor,
  uint32_t storage_id ));
PLAINMTP_EXTERN cursor_entity_e ZZ_PLAINMTP(get_cursor_state( struct plainmtp_cursor_s* cursor,
  entity_location_s* OUT_descriptor ));
PLAINMTP_EXTERN wchar_t* ZZ_PLAINMTP(make_device_info( LIBMTP_mtpdevice_t* socket,
  device_info_string_f method ));
PLAINMTP_EXTERN wchar_t* ZZ_PLAINMTP(make_storage_name( LIBMTP_devicestorage_t* values ));
PLAINMTP_EXTERN wchar_t* ZZ_PLAINMTP(make_storage_unique_id( LIBMTP_devicestorage_t* storage,
  wchar_t** OUT_volume_string ));
PLAINMTP_EXTERN LIBMTP_devicestorage_t* ZZ_PLAINMTP(find_storage_by_id(
  LIBMTP_devicestorage_t* chain, uint32_t storage_id ));

PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(obtain_image_copy( zz_plainmtp_cursor_s* entity,
  zz_plainmtp_cursor_s* source ));
PLAINMTP_EXTERN plainmtp_3val ZZ_PLAINMTP(obtain_object_image( zz_plainmtp_cursor_s* entity,
  LIBMTP_file_t* object, const wpd_guid_plain_i required_id ));
PLAINMTP_EXTERN plainmtp_3val ZZ_PLAINMTP(obtain_storage_image( zz_plainmtp_cursor_s* entity,
  LIBMTP_devicestorage_t* storage, const wchar_t* required_id ));
PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(obtain_device_image( zz_plainmtp_cursor_s* entity,
  LIBMTP_mtpdevice_t* socket ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(wipe_entity_image( zz_plainmtp_cursor_s* entity ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(free_libmtp_object_listing( LIBMTP_file_t* chain ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(wipe_enumeration_data( struct plainmtp_cursor_s* cursor,
  entity_location_s* OUT_descriptor ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(clear_cursor( struct plainmtp_cursor_s* cursor ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(prepare_cursor(
  struct plainmtp_cursor_s* cursor, zz_plainmtp_cursor_s* entity ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_to_object(
  struct plainmtp_cursor_s* cursor, LIBMTP_file_t* object ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_to_storage(
  struct plainmtp_cursor_s* cursor, LIBMTP_devicestorage_t* storage, const wchar_t* required_id ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_to_device(
  struct plainmtp_cursor_s* cursor, LIBMTP_mtpdevice_t* socket ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_by_handle(
  struct plainmtp_cursor_s* cursor, LIBMTP_mtpdevice_t* socket, uint32_t object_handle ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_by_lookup(
  struct plainmtp_cursor_s* cursor, LIBMTP_mtpdevice_t* socket,
  const wpd_guid_plain_i required_id ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_by_id(
  struct plainmtp_cursor_s* cursor, LIBMTP_mtpdevice_t* socket, uint32_t storage_id,
  plainmtp_bool force_update, const wchar_t* required_id ));
PLAINMTP_EXTERN storage_enumeration_s* ZZ_PLAINMTP(make_storage_enumeration(
  LIBMTP_mtpdevice_t* socket ));

PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(select_storage_first( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device ));
PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(select_storage_next( struct plainmtp_cursor_s* cursor ));
PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(select_object_first( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device ));
PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(select_object_next( struct plainmtp_cursor_s* cursor ));

PLAINMTP_EXTERN uint16_t ZZ_PLAINMTP(cb_file_data_exchange( void* ptp_context, void* wrapper_state,
  uint32_t chunk_size, unsigned char* chunk_data, uint32_t* OUT_processed ));

#endif /* PLAINMTP_NO_INTERNAL_API */
