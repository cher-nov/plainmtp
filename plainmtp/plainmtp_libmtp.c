#include "plainmtp.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <libmtp.h>

#include "wcsdup.h"
#include "utf8_wchar.h"
#include "wpd_puid.h"
#include "common.h"

/* NB: The code marked with the "SHARED MEMORY MOMENT" comment depends on the implicit assumption
  that our library and libmtp will share the same memory allocator and heap across a module
  boundary. Seems to be a mistake of the libmtp API: https://github.com/libmtp/libmtp/issues/121 */

static plainmtp_bool is_libmtp_initialized = PLAINMTP_FALSE;

/* By PTP/MTP standards, the values 0x00000000 and 0xFFFFFFFF are reserved for contextual use for
  both object handles and storage IDs. */
#define STORAGE_ID_NULL (0x00000000)
#define OBJECT_HANDLE_NULL LIBMTP_FILES_AND_FOLDERS_ROOT  /* See set_object_values(). */

#define CURSOR_HAS_ENUMERATION( Cursor ) \
  !( ( (Cursor)->enumeration == NULL ) || ( (Cursor)->enumeration == (Cursor) ) )

#define CURSOR_HAS_STORAGE_ID( Cursor ) \
  !( (Cursor)->values.storage_id == STORAGE_ID_NULL )

#define WSTRING_PRINTABLE( String ) \
  !( ( (String) == NULL ) || ( (String)[0] == L'\0' ) )

typedef char* (*device_info_string_f) (
  LIBMTP_mtpdevice_t* );

typedef enum {
  CURSOR_ENTITY_DEVICE,
  CURSOR_ENTITY_STORAGE,
  CURSOR_ENTITY_OBJECT
} cursor_entity_e;

typedef struct zz_file_exchange_s {
  plainmtp_data_f callback;
  size_t chunk_limit;
  void* custom_state;
} file_exchange_s;

typedef struct zz_entity_location_s {
  uint32_t storage_id;
  uint32_t object_handle;
  uint32_t parent_handle;
} entity_location_s;

typedef struct zz_storage_enumeration_s {
  uint32_t id;
  plainmtp_image_s entity;

  /* The node refers to itself if the following storages could not be retrieved. */
  struct zz_storage_enumeration_s* next;
} storage_enumeration_s;

struct zz_plainmtp_context_s {
  plainmtp_registry_s device_list;  /* MUST be the first field for typecasting to public type. */
  LIBMTP_raw_device_t* hardware_list;
};

struct zz_plainmtp_device_s {
  LIBMTP_mtpdevice_t* libmtp_socket;
  plainmtp_bool read_only;
};

/* TODO: Unlike WPD wrapper for plainmtp, this wrapper is relied on the public values stored in
  'current_entity' for the implementation of the cursor logic (for example, duplication of the
  cursor). They're available to the API user, which can change them, leading to a malfunction. */
struct zz_plainmtp_cursor_s {
  plainmtp_image_s current_entity;  /* MUST be the first field for typecasting to public type. */
  plainmtp_image_s parent_entity;  /* Contains undefined values if no enumeration in progress. */

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
    - plainmtp_cursor_s (an owner) - if the last enumeration has failed (also a default value).
  */

  void* enumeration;
};

static void set_object_values( entity_location_s* descriptor, const LIBMTP_file_t* object ) {
{
  descriptor->storage_id = object->storage_id;
  descriptor->object_handle = object->item_id;

  /* Fix libmtp semantic nonsense with root parent_id != LIBMTP_FILES_AND_FOLDERS_ROOT.
    https://github.com/libmtp/libmtp/commit/4c162fa4eef539fa4eae3f4f92f0f4bf60d70c19 */
  descriptor->parent_handle = (object->parent_id == 0) ? OBJECT_HANDLE_NULL : object->parent_id;
}}

static void set_storage_values( entity_location_s* descriptor, uint32_t storage_id ) {
{
  descriptor->storage_id = storage_id;
  descriptor->object_handle = OBJECT_HANDLE_NULL;
  descriptor->parent_handle = OBJECT_HANDLE_NULL;
}}

static cursor_entity_e get_cursor_state( plainmtp_cursor_s* cursor,
  entity_location_s* OUT_descriptor
) {
  cursor_entity_e result;
  entity_location_s descriptor;
{
  if (CURSOR_HAS_ENUMERATION(cursor)) {
    if (CURSOR_HAS_STORAGE_ID(cursor)) {
      set_object_values( &descriptor, cursor->enumeration );
      result = CURSOR_ENTITY_OBJECT;
    } else {
      set_storage_values( &descriptor, ((storage_enumeration_s*)cursor->enumeration)->id );
      result = CURSOR_ENTITY_STORAGE;
    }
  } else {
    descriptor = cursor->values;

    if (descriptor.object_handle != OBJECT_HANDLE_NULL) {
      result = CURSOR_ENTITY_OBJECT;
    } else if (CURSOR_HAS_STORAGE_ID(cursor)) {
      result = CURSOR_ENTITY_STORAGE;
    } else {
      result = CURSOR_ENTITY_DEVICE;
    }
  }

  if (OUT_descriptor != NULL) { *OUT_descriptor = descriptor; }
  return result;
}}

static wchar_t* make_device_info( LIBMTP_mtpdevice_t* socket, device_info_string_f method ) {
  wchar_t* result;
  char* utf8_string;
{
  utf8_string = method( socket );
  if (utf8_string == NULL) { return NULL; }

  result = make_wide_string_from_utf8( utf8_string, NULL );
  free( utf8_string );  /* SHARED MEMORY MOMENT */

  return result;
}}

static wchar_t* make_storage_name( LIBMTP_devicestorage_t* values ) {
  wchar_t* result;
  const wchar_t* label;
{
  if (WSTRING_PRINTABLE( values->StorageDescription )) {
    result = make_wide_string_from_utf8( values->StorageDescription, NULL );
    if (result != NULL) { return result; }
  }

  switch (values->StorageType) {
    case 0x0000: label = L"Undefined"; break;
    case 0x0001: label = L"Fixed ROM"; break;
    case 0x0002: label = L"Removable ROM"; break;
    case 0x0003: label = L"Fixed RAM"; break;
    case 0x0004: label = L"Removable RAM"; break;
    default: label = L"Reserved";
  }

  return wcsdup( label );
}}

static wchar_t* make_storage_unique_id( LIBMTP_devicestorage_t* storage,
  wchar_t** OUT_volume_string
) {
  wchar_t *result, *volume_string;
  size_t volume_string_length;
{
  if (storage->VolumeIdentifier != NULL) {
    volume_string = make_wide_string_from_utf8( storage->VolumeIdentifier, &volume_string_length );
    if (volume_string == NULL) { return NULL; }
  } else {
    volume_string = NULL;
    volume_string_length = 0;
  }

  result = make_wpd_storage_unique_id( storage->id, storage->MaxCapacity, volume_string,
    volume_string_length );

  if ( (OUT_volume_string != NULL) && (result != NULL) ) {
    *OUT_volume_string = volume_string;
  } else {
    free( volume_string );
  }

  return result;
}}

static LIBMTP_devicestorage_t* find_storage_by_id( LIBMTP_devicestorage_t* chain,
  uint32_t storage_id ) {
{
  while (chain != NULL) {
    if (chain->id == storage_id) { return chain; }
    chain = chain->next;
  }

  return NULL;
}}

/**************************************************************************************************/

plainmtp_context_s* plainmtp_startup(void) {
  LIBMTP_error_number_t status;
  LIBMTP_mtpdevice_t* libmtp_socket;
  LIBMTP_raw_device_t* libmtp_hardware_list = NULL;
  plainmtp_context_s* context;
  int libmtp_device_count, i;
  char *memory_block = NULL;
  wchar_t **libmtp_device_names, **libmtp_device_vendors, **libmtp_device_strings;
{
  if (!is_libmtp_initialized) {
#   ifndef NDEBUG
    LIBMTP_Set_Debug( LIBMTP_DEBUG_ALL );
#   endif
    LIBMTP_Init();
    is_libmtp_initialized = PLAINMTP_TRUE;
  }

  status = LIBMTP_Detect_Raw_Devices( &libmtp_hardware_list, &libmtp_device_count );
  switch (status) {
    case LIBMTP_ERROR_NONE:
    case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
    break;

    default:
      goto failed;
  }

  memory_block = malloc( sizeof(*context) + 3 * (size_t)libmtp_device_count * sizeof(wchar_t*) );
  if (memory_block == NULL) { goto failed; }
  context = (plainmtp_context_s*)memory_block;

  if ( libmtp_device_count > 0 ) {
    libmtp_device_names = (wchar_t**)( memory_block + sizeof(*context) );
    libmtp_device_vendors = libmtp_device_names + libmtp_device_count;
    libmtp_device_strings = libmtp_device_vendors + libmtp_device_count;

    for (i = 0; i < libmtp_device_count; ++i) {
      libmtp_socket = LIBMTP_Open_Raw_Device_Uncached( &libmtp_hardware_list[i] );
      if (libmtp_socket == NULL) {
        libmtp_device_names[i] = NULL;
        libmtp_device_vendors[i] = NULL;
        libmtp_device_strings[i] = NULL;
        continue;
      }

      libmtp_device_names[i] = make_device_info( libmtp_socket, LIBMTP_Get_Friendlyname );
      libmtp_device_vendors[i] = make_device_info( libmtp_socket, LIBMTP_Get_Manufacturername );
      libmtp_device_strings[i] = make_device_info( libmtp_socket, LIBMTP_Get_Modelname );
      LIBMTP_Release_Device( libmtp_socket );
    }
  } else {
    libmtp_device_names = NULL;
    libmtp_device_vendors = NULL;
    libmtp_device_strings = NULL;
  }

  context->hardware_list = libmtp_hardware_list;

  context->device_list.count = libmtp_device_count;
  context->device_list.ids = NULL;
  context->device_list.names = libmtp_device_names;
  context->device_list.vendors = libmtp_device_vendors;
  context->device_list.strings = libmtp_device_strings;

  return context;

failed:
  free( libmtp_hardware_list );  /* SHARED MEMORY MOMENT */
  return NULL;
}}

void plainmtp_shutdown( plainmtp_context_s* context ) {
  size_t i;
{
  assert( context != NULL );

  for (i = 0; i < context->device_list.count; ++i) {
    free( context->device_list.names[i] );
    free( context->device_list.vendors[i] );
    free( context->device_list.strings[i] );
  }

  free( context->hardware_list );  /* SHARED MEMORY MOMENT */
  free( context );
}}

plainmtp_device_s* plainmtp_device_start( plainmtp_context_s* context, size_t device_index,
  plainmtp_bool read_only
) {
  plainmtp_device_s* device;
{
  assert( context != NULL );
  assert( device_index < context->device_list.count );

  device = malloc( sizeof(*device) );
  if (device == NULL) { goto failed; }

  /* We use LIBMTP_Open_Raw_Device_Uncached() instead of LIBMTP_Open_Raw_Device() because MTP is
    event-oriented, but libmtp doesn't process events and thus doesn't update its own cache (what
    WPD, for example, apparently does). Since we don't process them too (by design), this forces us
    to use the uncached mode to achieve WPD-like behavior with libmtp. */

  device->libmtp_socket = LIBMTP_Open_Raw_Device_Uncached( &context->hardware_list[device_index] );
  if (device->libmtp_socket == NULL) { goto failed; }

  device->read_only = read_only;
  return device;

failed:
  free( device );
  return NULL;
}}

void plainmtp_device_finish( plainmtp_device_s* device ) {
{
  assert( device != NULL );

  LIBMTP_Release_Device( device->libmtp_socket );
  free( device );
}}

/**************************************************************************************************/

static plainmtp_bool obtain_image_copy( plainmtp_image_s* entity, plainmtp_image_s* source ) {
  wchar_t* unique_id;
{
  unique_id = wcsdup( source->id );
  if (unique_id == NULL) { return PLAINMTP_FALSE; }
  entity->id = unique_id;

  if (source->name != NULL) { entity->name = wcsdup( source->name ); }
  entity->datetime = source->datetime;

  return PLAINMTP_TRUE;
}}

static plainmtp_3val obtain_object_image( plainmtp_image_s* entity, LIBMTP_file_t* object,
  const wpd_guid_plain_i required_id
) {
  wpd_guid_plain_i plain_guid;
  wchar_t *name, *id_string;
{
  if (object->filename != NULL) {
    name = make_wide_string_from_utf8( object->filename, NULL );
    if (name == NULL) { return PLAINMTP_BAD; }
  } else {
    name = NULL;
  }

  /* TODO: Usage of the WPD fallback algorithm here is a temporary workaround solution for unique
    object identifiers, since libmtp can't access the actual MTP property due to lack of support
    for the UINT128 protocol datatype. Detailed issue: https://github.com/libmtp/libmtp/issues/117

    It should be noted that this is also slightly incorrect here, as it requires a narrowing cast
    for the object size, which is 64-bit wide in MTP, and needs 'name' which can be NULL. Moreover,
    the wrapper should not have the need in this algorithm at all, because libmtp supports only MTP
    and ignores PTP devices. */

  get_wpd_fallback_object_id( plain_guid, name, object->item_id, object->parent_id,
    object->storage_id, (uint32_t)object->filesize );

  /* BEWARE: Short-circuit evaluation matters here! */
  if ( (required_id != NULL)
    && (memcmp( plain_guid, required_id, sizeof(wpd_guid_plain_i) ) != 0)
  ) {
    free( name );
    return PLAINMTP_NONE;
  }

  id_string = malloc( WPD_GUID_STRING_SIZE * sizeof(*id_string) );
  if (id_string == NULL) {
    free( name );
    return PLAINMTP_BAD;
  }

  write_wpd_plain_guid( plain_guid, id_string );

  entity->id = id_string;
  entity->name = name;

  /* NB: I personally would prefer gmtime() here, but that's how WPD wrapper for plainmtp works. */
  entity->datetime = *localtime( &object->modificationdate );

  return PLAINMTP_GOOD;
}}

static plainmtp_3val obtain_storage_image( plainmtp_image_s* entity,
  LIBMTP_devicestorage_t* storage, const wchar_t* required_id
) {
  wchar_t *unique_id, *storage_name;
{
  unique_id = make_storage_unique_id( storage, &storage_name );
  if (unique_id == NULL) { return PLAINMTP_BAD; }

  /* BEWARE: Short-circuit evaluation matters here! */
  if ( (required_id != NULL) && (wcscmp( unique_id, required_id ) != 0) ) {
    free( unique_id );
    free( storage_name );
    return PLAINMTP_NONE;
  }

  if (!WSTRING_PRINTABLE( storage_name )) {
    free( storage_name );
    storage_name = make_storage_name( storage );
  }

  entity->id = unique_id;
  entity->name = storage_name;
  entity->datetime.tm_mday = 0;  /* There's no datetime information for storages. */

  return PLAINMTP_GOOD;
}}

static plainmtp_bool obtain_device_image( plainmtp_image_s* entity, LIBMTP_mtpdevice_t* socket ) {
  wchar_t* unique_id;
{
  unique_id = wcsdup( wpd_root_persistent_id );
  if (unique_id == NULL) { return PLAINMTP_FALSE; }
  entity->id = unique_id;

  entity->name = make_device_info( socket, LIBMTP_Get_Modelname );
  entity->datetime.tm_mday = 0;  /* There's no datetime information for the device root. */

  return PLAINMTP_TRUE;
}}

static void wipe_entity_image( plainmtp_image_s* entity ) {
{
  free( entity->id );
  free( entity->name );
}}

static void free_libmtp_object_listing( LIBMTP_file_t* chain ) {
{
  do {
    LIBMTP_file_t* node = chain;
    chain = node->next;
    LIBMTP_destroy_file_t( node );
  } while (chain != NULL);
}}

/* NB: This function doesn't maintain the cursor state, which must be explicitly adjusted later. */
static void wipe_enumeration_data( plainmtp_cursor_s* cursor, entity_location_s* OUT_descriptor ) {
  void* chain = cursor->enumeration;
{
  wipe_entity_image( (OUT_descriptor != NULL) ? &cursor->parent_entity : &cursor->current_entity );

  if (!CURSOR_HAS_STORAGE_ID(cursor)) {
    storage_enumeration_s* node = chain;
    if (OUT_descriptor != NULL) { set_storage_values( OUT_descriptor, node->id ); }

    /* When enumerating storages, cursor->current_entity is a copy of cursor->enumeration->entity
      provided for the API user, so we skip the first iteration to avoid freeing it twice. */

    for (;;) {
      chain = node->next;
      free( node );

      if ( (chain == NULL) || (chain == node) ) { return; }

      node = chain;
      wipe_entity_image( &node->entity );
    }
  }

  if (OUT_descriptor != NULL) { set_object_values( OUT_descriptor, chain ); }
  free_libmtp_object_listing( chain );
}}

static void clear_cursor( plainmtp_cursor_s* cursor ) {
{
  if (CURSOR_HAS_ENUMERATION(cursor)) {
    wipe_enumeration_data( cursor, NULL );
    wipe_entity_image( &cursor->parent_entity );
  } else {
    wipe_entity_image( &cursor->current_entity );
  }
}}

static plainmtp_cursor_s* prepare_cursor( plainmtp_cursor_s* cursor, plainmtp_image_s* entity ) {
{
  if (cursor == NULL) {
    cursor = malloc( sizeof(*cursor) );
    if (cursor == NULL) { return NULL; }
  } else {
    clear_cursor( cursor );
  }

  cursor->current_entity = *entity;
  cursor->enumeration = cursor;

  return cursor;
}}

static plainmtp_cursor_s* setup_cursor_to_object( plainmtp_cursor_s* cursor,
  LIBMTP_file_t* object
) {
  plainmtp_image_s entity;
{
  if ( obtain_object_image( &entity, object, NULL ) == PLAINMTP_GOOD ) {
    cursor = prepare_cursor( cursor, &entity );
    if (cursor != NULL) {
      set_object_values( &cursor->values, object );
      return cursor;
    }

    wipe_entity_image( &entity );
  }

  return NULL;
}}

static plainmtp_cursor_s* setup_cursor_to_storage( plainmtp_cursor_s* cursor,
  LIBMTP_devicestorage_t* storage, const wchar_t* required_id
) {
  plainmtp_image_s entity;
{
  if ( obtain_storage_image( &entity, storage, required_id ) == PLAINMTP_GOOD ) {
    cursor = prepare_cursor( cursor, &entity );
    if (cursor != NULL) {
      set_storage_values( &cursor->values, storage->id );
      return cursor;
    }

    wipe_entity_image( &entity );
  }

  return NULL;
}}

static plainmtp_cursor_s* setup_cursor_to_device( plainmtp_cursor_s* cursor,
  LIBMTP_mtpdevice_t* socket
) {
  plainmtp_image_s entity;
{
  if ( obtain_device_image( &entity, socket ) ) {
    cursor = prepare_cursor( cursor, &entity );
    if (cursor != NULL) {
      set_storage_values( &cursor->values, STORAGE_ID_NULL );
      return cursor;
    }

    wipe_entity_image( &entity );
  }

  return NULL;
}}

static plainmtp_cursor_s* setup_cursor_by_handle( plainmtp_cursor_s* cursor,
  LIBMTP_mtpdevice_t* socket, uint32_t object_handle
) {
  LIBMTP_file_t* object;
{
  object = LIBMTP_Get_Filemetadata( socket, object_handle );
  if (object == NULL) { return NULL; }

  cursor = setup_cursor_to_object( cursor, object );
  LIBMTP_destroy_file_t( object );

  return cursor;
}}

static plainmtp_cursor_s* setup_cursor_by_lookup( plainmtp_cursor_s* cursor,
  LIBMTP_mtpdevice_t* socket, const wpd_guid_plain_i required_id
) {
  plainmtp_cursor_s* result = NULL;
  LIBMTP_file_t *chain, *object;
  plainmtp_image_s entity;
  object_queue_s *bfs_pipeline, *data;
  object_queue_item_s step = {STORAGE_ID_NULL, LIBMTP_FILES_AND_FOLDERS_ROOT};
{
  /* TODO: Can this be faster? LIBMTP_Get_Files_And_Folders() parses all objects into LIBMTP_file_t
    instances, while we need only their handles here, so that is quite slow. It's worth noting that
    LIBMTP_Get_Folder_List() is not suitable because it gets the full object list internally.
    Moreover, it doesn't work in the uncached mode: https://github.com/libmtp/libmtp/issues/129 */

  bfs_pipeline = object_queue_create(0);
  if (bfs_pipeline == NULL) { return NULL; }

  do {
    chain = LIBMTP_Get_Files_And_Folders( socket, step.storage_id, step.object_handle );

    while (chain != NULL) {
      object = chain;
      chain = object->next;

      switch (obtain_object_image( &entity, object, required_id )) {
        default:
          if (object->filetype == LIBMTP_FILETYPE_FOLDER) {
            data = object_queue_push( bfs_pipeline, object->storage_id, object->item_id );
            if (data == NULL) { break; }
            bfs_pipeline = data;
          }

          LIBMTP_destroy_file_t( object );
        continue;

        case PLAINMTP_GOOD:
          result = prepare_cursor( cursor, &entity );
          if (result != NULL) {
            set_object_values( &result->values, object );
          } else {
            wipe_entity_image( &entity );
          }
        case PLAINMTP_BAD:
        break;
      }

      free_libmtp_object_listing( object );
      goto quit;
    }

  } while (object_queue_pop( bfs_pipeline, &step ));

quit:
  free( bfs_pipeline );
  return result;
}}

static plainmtp_cursor_s* setup_cursor_by_id( plainmtp_cursor_s* cursor,
  LIBMTP_mtpdevice_t* socket, uint32_t storage_id, plainmtp_bool force_update,
  const wchar_t* required_id
) {
  LIBMTP_devicestorage_t* storage;
{
  if ( (socket->storage == NULL) || force_update ) {
    int status = LIBMTP_Get_Storage( socket, LIBMTP_STORAGE_SORTBY_NOTSORTED );
    if (status != 0) { return NULL; }
  }

  storage = find_storage_by_id( socket->storage, storage_id );
  if (storage == NULL) { return NULL; }

  return setup_cursor_to_storage( cursor, storage, required_id );
}}

static storage_enumeration_s* make_storage_enumeration( LIBMTP_mtpdevice_t* socket ) {
  int status;
  LIBMTP_devicestorage_t* chain;
  storage_enumeration_s *last_node = NULL, *next_node, *result;
  storage_enumeration_s** link = &result;
{
  status = LIBMTP_Get_Storage( socket, LIBMTP_STORAGE_SORTBY_MAXSPACE );
  if (status != 0) { return NULL; }
  chain = socket->storage;

  while (chain != NULL) {
    next_node = malloc( sizeof(*next_node) );

    /* BEWARE: Short-circuit evaluation matters here! */
    if ( (next_node == NULL)
      || (obtain_storage_image( &next_node->entity, chain, NULL ) != PLAINMTP_GOOD)
    ) {
      free( next_node );
      *link = last_node;
      return result;
    }

    next_node->id = chain->id;

    *link = next_node;
    link = &next_node->next;

    last_node = next_node;
    chain = chain->next;
  }

  *link = NULL;
  return result;
}}

plainmtp_cursor_s* plainmtp_cursor_assign( plainmtp_cursor_s* cursor, plainmtp_cursor_s* source ) {
  plainmtp_image_s entity;
{
  if (cursor == source) { return cursor; }

  if (source == NULL) {
    clear_cursor( cursor );
    free( cursor );
  } else if ( obtain_image_copy( &entity, &source->current_entity ) ) {
    cursor = prepare_cursor( cursor, &entity );
    if (cursor != NULL) {
      (void)get_cursor_state( source, &cursor->values );
      return cursor;
    }

    wipe_entity_image( &entity );
  }

  return NULL;
}}

plainmtp_cursor_s* plainmtp_cursor_switch( plainmtp_cursor_s* cursor, const wchar_t* entity_id,
  plainmtp_device_s* device
) {
  wpd_guid_plain_i object_id;
  uint32_t storage_id;
{
  assert( device != NULL );

  /* BEWARE: Short-circuit evaluation matters here! */
  if ( (entity_id == NULL) || (wcscmp( entity_id, wpd_root_persistent_id ) == 0) ) {
    return setup_cursor_to_device( cursor, device->libmtp_socket );
  }

  if (parse_wpd_storage_unique_id( entity_id, &storage_id )) {
    return setup_cursor_by_id( cursor, device->libmtp_socket, storage_id, PLAINMTP_TRUE,
      entity_id );
  }

  if (read_wpd_plain_guid( object_id, entity_id )) {
    return setup_cursor_by_lookup( cursor, device->libmtp_socket, object_id );
  }

  return NULL;
}}

plainmtp_bool plainmtp_cursor_update( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  entity_location_s descriptor;
{
  assert( cursor != NULL );
  assert( device != NULL );

  switch (get_cursor_state( cursor, &descriptor )) {
    case CURSOR_ENTITY_DEVICE:
      cursor = setup_cursor_to_device( cursor, device->libmtp_socket );
    break;

    case CURSOR_ENTITY_STORAGE:
      cursor = setup_cursor_by_id( cursor, device->libmtp_socket, descriptor.storage_id,
        PLAINMTP_TRUE, NULL );
    break;

    case CURSOR_ENTITY_OBJECT:
      cursor = setup_cursor_by_handle( cursor, device->libmtp_socket, descriptor.object_handle );
    break;
  }

  return (cursor != NULL);
}}

plainmtp_bool plainmtp_cursor_return( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  plainmtp_bool is_shadowed;
{
  assert( cursor != NULL );

  is_shadowed = CURSOR_HAS_ENUMERATION(cursor);
  if (device == NULL) { return is_shadowed; }

  if (is_shadowed) {
    wipe_enumeration_data( cursor, NULL );
    cursor->current_entity = cursor->parent_entity;
    cursor->enumeration = NULL;
    return PLAINMTP_TRUE;
  }

  /* Check if the cursor represents the device root. */
  if (cursor->values.storage_id == STORAGE_ID_NULL) { return PLAINMTP_TRUE; }

  if (cursor->values.object_handle == OBJECT_HANDLE_NULL) {
    /* The cursor represents the storage root. */
    cursor = setup_cursor_to_device( cursor, device->libmtp_socket );

  } else if (cursor->values.parent_handle == OBJECT_HANDLE_NULL) {
    /* The cursor represents an object from the storage root. */
    cursor = setup_cursor_by_id( cursor, device->libmtp_socket, cursor->values.storage_id,
      PLAINMTP_FALSE, NULL );

  } else {
    /* The cursor represents an object with a parent. */
    cursor = setup_cursor_by_handle( cursor, device->libmtp_socket, cursor->values.parent_handle );
  }

  return (cursor != NULL);
}}

/**************************************************************************************************/

static plainmtp_bool select_storage_first( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  storage_enumeration_s* chain;
{
  chain = make_storage_enumeration( device->libmtp_socket );
  if (chain == NULL) {
    cursor->enumeration = cursor;
    return PLAINMTP_FALSE;
  }

  cursor->parent_entity = cursor->current_entity;
  cursor->current_entity = chain->entity;

  cursor->enumeration = chain;
  return PLAINMTP_TRUE;
}}

static plainmtp_bool select_storage_next( plainmtp_cursor_s* cursor ) {
  storage_enumeration_s *node = cursor->enumeration, *chain = node->next;
{
  wipe_entity_image( &cursor->current_entity );  /* This also frees 'node->entity'. */
  free( node );

  if (chain == node) {
    cursor->enumeration = cursor;
    goto finished;
  }

  cursor->enumeration = chain;
  if (chain == NULL) { goto finished; }

  cursor->current_entity = chain->entity;
  return PLAINMTP_TRUE;

finished:
  cursor->current_entity = cursor->parent_entity;
  return PLAINMTP_FALSE;
}}

static plainmtp_bool select_object_first( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  LIBMTP_file_t* chain;
{
  /* NB: LIBMTP_Get_Files_And_Folders() always returns NULL for empty 'Association' objects.
    It also omits some errors in non-empty case, but still litters the error stack with them. */
  LIBMTP_Clear_Errorstack( device->libmtp_socket );

  /* TODO: Why doesn't this function report a PTP/MTP 'Invalid_ParentObject' error when calling
    with an object not of type 'Association', as required by the standard? (model: Honor 8X) */
  chain = LIBMTP_Get_Files_And_Folders( device->libmtp_socket, cursor->values.storage_id,
    cursor->values.object_handle );

  if (chain == NULL) {
    cursor->enumeration = (LIBMTP_Get_Errorstack( device->libmtp_socket ) == NULL) ? NULL : cursor;
    return PLAINMTP_FALSE;
  }

  cursor->parent_entity = cursor->current_entity;
  if (obtain_object_image( &cursor->current_entity, chain, NULL ) != PLAINMTP_GOOD) {
    free_libmtp_object_listing( chain );
    cursor->enumeration = cursor;
    return PLAINMTP_FALSE;
  }

  cursor->enumeration = chain;
  return PLAINMTP_TRUE;
}}

static plainmtp_bool select_object_next( plainmtp_cursor_s* cursor ) {
  LIBMTP_file_t *node = cursor->enumeration, *chain = node->next;
{
  wipe_entity_image( &cursor->current_entity );
  LIBMTP_destroy_file_t( node );

  if (chain == NULL) {
    cursor->enumeration = NULL;
    goto finished;
  }

  if (obtain_object_image( &cursor->current_entity, chain, NULL ) != PLAINMTP_GOOD) {
    free_libmtp_object_listing( chain );
    cursor->enumeration = cursor;
    goto finished;
  }

  cursor->enumeration = chain;
  return PLAINMTP_TRUE;

finished:
  cursor->current_entity = cursor->parent_entity;
  return PLAINMTP_FALSE;
}}

plainmtp_bool plainmtp_cursor_select( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
{
  assert( cursor != NULL );

  if (device == NULL) {
    if (cursor->enumeration == cursor) { return PLAINMTP_TRUE; }
    if (cursor->enumeration != NULL) {
      wipe_enumeration_data( cursor, &cursor->values );
      cursor->enumeration = NULL;
    }

    return PLAINMTP_FALSE;
  }

  if (CURSOR_HAS_ENUMERATION(cursor)) {
    if (CURSOR_HAS_STORAGE_ID(cursor)) { return select_object_next( cursor ); }
    return select_storage_next( cursor );
  }

  if (CURSOR_HAS_STORAGE_ID(cursor)) { return select_object_first( cursor, device ); }
  return select_storage_first( cursor, device );
}}

/**************************************************************************************************/

static uint16_t CB_file_data_exchange( void* ptp_context, void* wrapper_state, uint32_t chunk_size,
  unsigned char* chunk_data, uint32_t* OUT_processed
) {
  file_exchange_s* context = wrapper_state;
  size_t bytes_left = chunk_size, part_limit;
  void* result;
{
  part_limit = (context->chunk_limit == 0) ? chunk_size : context->chunk_limit;

  while (bytes_left > 0) {
    const size_t part_size = (part_limit < bytes_left) ? part_limit : bytes_left;
    result = context->callback( chunk_data, part_size, context->custom_state );
    if (result == NULL) { return LIBMTP_HANDLER_RETURN_ERROR; }

    chunk_data += part_size;
    bytes_left -= part_size;
  }

  *OUT_processed = chunk_size;
  return LIBMTP_HANDLER_RETURN_OK;

  (void)ptp_context;
}}

plainmtp_bool plainmtp_cursor_receive( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  size_t chunk_limit, plainmtp_data_f callback, void* custom_state
) {
  int status;
  entity_location_s descriptor;
  file_exchange_s context;
{
  assert( cursor != NULL );
  assert( device != NULL );
  assert( callback != NULL );

  if ( get_cursor_state( cursor, &descriptor ) != CURSOR_ENTITY_OBJECT ) { return PLAINMTP_FALSE; }

  context.callback = callback;
  context.custom_state = custom_state;
  context.chunk_limit = chunk_limit;

  status = LIBMTP_Get_File_To_Handler( device->libmtp_socket, descriptor.object_handle,
    &CB_file_data_exchange, &context, NULL, NULL );

  (void)callback( NULL, 0, custom_state );
  return (status == 0);
}}

plainmtp_bool plainmtp_cursor_transfer( plainmtp_cursor_s* parent, plainmtp_device_s* device,
  const wchar_t* name, uint64_t size, size_t chunk_limit, plainmtp_data_f callback,
  void* custom_state, plainmtp_cursor_s** SET_cursor
) {
  plainmtp_bool result;
  LIBMTP_file_t metadata = {0};
  entity_location_s descriptor;
  file_exchange_s context;
{
  assert( parent != NULL );
  assert( device != NULL );
  assert( (callback != NULL) || (size == 0) );

  if (device->read_only) { return PLAINMTP_FALSE; }
  if ( get_cursor_state( parent, &descriptor ) == CURSOR_ENTITY_DEVICE ) { return PLAINMTP_FALSE; }

  metadata.filename = make_multibyte_string( name );
  if (metadata.filename == NULL) { return PLAINMTP_FALSE; }

  metadata.parent_id = descriptor.object_handle;
  metadata.storage_id = descriptor.storage_id;
  metadata.filesize = size;
  metadata.filetype = LIBMTP_FILETYPE_UNKNOWN;

  context.callback = callback;
  context.custom_state = custom_state;
  context.chunk_limit = chunk_limit;

  result = LIBMTP_Send_File_From_Handler( device->libmtp_socket, &CB_file_data_exchange, &context,
    &metadata, NULL, NULL ) == 0;

  if (callback != NULL) {
    (void)callback( NULL, 0, custom_state );
  }

  if ( result && (SET_cursor != NULL) ) {
    *SET_cursor = setup_cursor_to_object( *SET_cursor, &metadata );
  }

  free( metadata.filename );
  return result;
}}
