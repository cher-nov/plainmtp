#include "plainmtp_wpd.h.c"

#include <assert.h>

#include <ObjBase.h>
#include <PropIdl.h>
#include <PropVarUtil.h>
#include <PortableDeviceTypes.h>
#include <PortableDevice.h>

#if defined(_MSC_VER) && !defined(CC_PLAINMTP_NO_PRAGMA_LINKAGE)
  #pragma comment( lib, "PortableDeviceGUIDs" )
  #pragma comment( lib, "Propsys" )
  #pragma comment( lib, "ole32" )
  #pragma comment( lib, "OleAut32" )
#endif

#include "../3rdparty/stager.h"

/* TODO: WPD randomly fails if some other process also uses the device. How should we handle it?
  https://docs.microsoft.com/en-us/archive/blogs/dimeby8/help-wpd-api-calls-randomly-fail-with-0x800700aa-error_busy
  https://stackoverflow.com/questions/34290054/why-am-i-not-getting-the-wpd-object-original-file-namei-e-the-filename-of-the
*/

/*
  The semantics of COM utilization is very C++ oriented by nature. And the most noticeable problem
  here is the object lifetime management. Most COM wrappers for C++ (MFC, ATL/WTL etc.) use RAII to
  achieve this, which is quite valid, but we don't have such a marvellous thing in Pure C.

  It's worth noting that our API implies the use of contexts. So we apply the following scheme:

  - Persistent objects (i.e. that describe the context) are initialized with 3-phase GOTO cleanup
    on errors. If you need more phases, you maybe need an additional function. In some cases, the
    STAGER (stager.h) macros could be used instead (see plainmtp_device_start() for an example).

  - Temporary objects (that is, those required only for initialization) are handled in separate
    functions with more structured cleanup of them before return, both on success and failure.

  - Keep in mind the presence of the NULL check in CoTaskMemFree() and apply it where possible to
    avoid excessive NULL checks in our code, while also trying not to initialize variables to NULL.

  - Use the facts that on acquiring the first resource, cleanup is not needed in case of error, and
    that the last resource acquired before return doesn't need release code in the cleanup block.
    Also remember that usage of contiguous memory blocks is not only more efficient than separate
    allocations but also requires only one check and cleanup operation.
*/

/*
  Very important notes about handling WPD_OBJECT_PERSISTENT_UNIQUE_ID:

  - Persistent Unique Object Identifiers (PUIDs or PUOIDs) were introduced only within the MTP
    specification, as PTP extension (the property code is 0xDC41, or 0b1101110001000001 in binary,
    where starting 1101 bits mean "Vendor-Extended Property Code", according to the PTP standard).
    But it seems that WPD always assigns persistent IDs for objects even if they're unsupported by
    the device itself: https://docs.microsoft.com/en-us/windows/win32/wpd_sdk/wpd-content-type-all
    At least it definitely has a fallback algorithm for PTP (which is now known, see wpd_puid.c).

    I've tested Canon PowerShot A700 camera that supports PTP (including PictBridge) but not MTP,
    and WPD had reported the PUIDs successfully. I've also tested Sony DSC-H50 camera that supports
    PTP/MTP and PictBridge as two different modes, and it's worth noting that their PUID values
    were different. The PTP and MTP modes are combined there into one, so it probably supports
    extended MTP property codes anyway, even if the machine (the "initiator") is using PTP only.

  - WPD also assigns PUIDs even for portable devices that use other protocols than PTP/MTP (e.g.
    Mass Storage Class). I've tested Sony DSC-H50 in Mass Storage mode, and PUIDs and object
    handles were consisting of full file paths on the storage, with the only difference that PUIDs
    were percent-encoded. But ::GetObjectIDsFromPersistentUniqueIDs() method understood such a PUID
    without percent-encoding as well. So it seems that PUIDs in WPD are also implicitly compatible
    with the FAT and NTFS filesystems used by Windows, though it's an undocumented feature. An
    indirect proof of this is the fact that Windows Explorer, in order to open files from PTP/MTP
    devices with regular applications, places them in a temporary folder with a path containing the
    PUID of a parent object for this file on the device. Example (on Windows 7):
    C:\Users\MainUser\AppData\Local\Temp\WPDNSE\{00002242-0001-0001-0000-000000000000}\picture.jpg

  - PUIDs also may contain plain spaces - e.g. in the aforementioned case with Mass Storage Class
    protocol when filesystem paths are used as PUIDs.

  ...to be continued.
*/

#define make_device_info ZZ_PLAINMTP(make_device_info)
PLAINMTP_INTERNAL LPWSTR make_device_info( IPortableDeviceManager* wpd_manager, LPCWSTR device_id,
  device_info_string_f method
) {
  WCHAR* result;
  HRESULT hr;
  DWORD length = 0;
{
  hr = method( wpd_manager, device_id, NULL, &length );
  if (length > 0) {
    result = CoTaskMemAlloc( length * sizeof(*result) );
    hr = method( wpd_manager, device_id, result, &length );

    if (SUCCEEDED(hr)) { return result; }
    CoTaskMemFree( result );
  }

  return NULL;
}}

/*
  The implementation of this function assumes badly written ::GetDevices() and/or possible change
  of the WPD manager state from another thread by meeting the following requirements:
  1. On error, ::GetDevices() may not set 'wpd_device_count' to 0 or whatsoever.
  2. Between calls of ::GetDevices(), 'wpd_device_count' may change without ::RefreshDeviceList().
*/
#define obtain_wpd_device_ids ZZ_PLAINMTP(obtain_wpd_device_ids)
PLAINMTP_INTERNAL HRESULT obtain_wpd_device_ids( IPortableDeviceManager* wpd_manager,
  LPWSTR** OUT_device_ids, size_t* OUT_device_count
) {
  HRESULT hr;
  LPWSTR* wpd_device_ids;
  DWORD wpd_device_count = 0;
{
  hr = IPortableDeviceManager_GetDevices( wpd_manager, NULL, &wpd_device_count );
  if (FAILED(hr)) { return hr; }

  if (wpd_device_count > 0) {
    wpd_device_ids = CoTaskMemAlloc( wpd_device_count * sizeof(*wpd_device_ids) );
    if (wpd_device_ids == NULL) { return E_OUTOFMEMORY; }

    hr = IPortableDeviceManager_GetDevices( wpd_manager, wpd_device_ids, &wpd_device_count );
    if (FAILED(hr)) {
      CoTaskMemFree( wpd_device_ids );
      return hr;
    }

    if (wpd_device_count == 0) {
      CoTaskMemFree( wpd_device_ids );
      wpd_device_ids = NULL;
    }
  } else {
    wpd_device_ids = NULL;
  }

  *OUT_device_ids = wpd_device_ids;
  *OUT_device_count = wpd_device_count;
  return hr;
}}

/* NB: This will successfully return an empty string in case of invalid PUID specified. */
#define make_object_handle_from_puid ZZ_PLAINMTP(make_object_handle_from_puid)
PLAINMTP_INTERNAL LPWSTR make_object_handle_from_puid( IPortableDeviceContent* wpd_content,
  LPCWSTR object_puid
) {
  LPWSTR result;
  HRESULT hr;
  IPortableDevicePropVariantCollection *handle_list = NULL, *puid_list;
  PROPVARIANT propvar;
{
  assert( wpd_content != NULL );

  hr = CoCreateInstance( &CLSID_PortableDevicePropVariantCollection, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDevicePropVariantCollection, &puid_list );
  if (FAILED(hr)) { return NULL; }

  /*
    For some unknown reason, InitPropVariantFromString() is not available in C mode.
    However, it would require string duplication, which is pointless here anyway.
  */

  PropVariantInit( &propvar );
  V_VT(&propvar) = VT_LPWSTR;
  V_UNION(&propvar, pwszVal) = (LPWSTR)object_puid;

  hr = IPortableDevicePropVariantCollection_Add( puid_list, &propvar );

  /* It's necessary to explicitly state that we don't own the string to prevent deallocation. */
  V_UNION(&propvar, pwszVal) = NULL;
  (void)PropVariantClear( &propvar );

  if (SUCCEEDED(hr)) {
    hr = IPortableDeviceContent_GetObjectIDsFromPersistentUniqueIDs( wpd_content, puid_list,
      &handle_list );
  }

  IUnknown_Release( puid_list );
  if (FAILED(hr)) { return NULL; }

  /* PropVariantClear() reinitializes the variant, so we don't invoke PropVariantInit() again. */
  hr = IPortableDevicePropVariantCollection_GetAt( handle_list, 0, &propvar );
  IUnknown_Release( handle_list );
  if (FAILED(hr)) { return NULL; }

  (void)PropVariantToStringAlloc( &propvar, &result );  /* Will set 'result' to NULL on error. */
  (void)PropVariantClear( &propvar );

  return result;
}}

#define make_values_request ZZ_PLAINMTP(make_values_request)
PLAINMTP_INTERNAL IPortableDeviceKeyCollection* make_values_request(void) {
  HRESULT hr;
  IPortableDeviceKeyCollection* result;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceKeyCollection, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceKeyCollection, &result );

  if (SUCCEEDED(hr)) {
    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_ID );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_PERSISTENT_UNIQUE_ID );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_PARENT_ID );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_ORIGINAL_FILE_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_HINT_LOCATION_DISPLAY_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_DEVICE_FRIENDLY_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = IPortableDeviceKeyCollection_Add( result, &WPD_OBJECT_DATE_MODIFIED );
    if (FAILED(hr)) { goto failed; }
  }

  return result;

failed:
  IUnknown_Release( result );
  return NULL;
}}

#define make_library_context ZZ_PLAINMTP(make_library_context)
PLAINMTP_INTERNAL struct plainmtp_context_s* make_library_context(void) {
  HRESULT hr;
  IPortableDeviceManager* wpd_manager;
  IPortableDeviceKeyCollection* wpd_values_request;
  struct plainmtp_context_s* context = NULL;
  LPWSTR* device_ids_buffer;
  LPCWSTR *wpd_device_ids, *wpd_device_names, *wpd_device_vendors, *wpd_device_strings;
  size_t device_count, i;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceManager, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceManager, &wpd_manager );
  if (FAILED(hr)) { goto failed; }

  hr = obtain_wpd_device_ids( wpd_manager, &device_ids_buffer, &device_count );
  if (FAILED(hr)) { goto failed_main; }

  context = CoTaskMemAlloc( sizeof(*context) + 4 * device_count * sizeof(*wpd_device_ids) );
  if (context == NULL) { goto failed_full; }

  if ( device_count == 0 ) {
    wpd_device_ids = NULL;
    wpd_device_names = NULL;
    wpd_device_vendors = NULL;
    wpd_device_strings = NULL;
  } else {
    wpd_device_ids = (LPCWSTR*)(context + 1);
    wpd_device_names = wpd_device_ids + device_count;
    wpd_device_vendors = wpd_device_names + device_count;
    wpd_device_strings = wpd_device_vendors + device_count;

    for (i = 0; i < device_count; ++i) {
      wpd_device_ids[i] = device_ids_buffer[i];  /* proclaims LPCWSTR as the "effective type" */
      wpd_device_names[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceFriendlyName );
      wpd_device_vendors[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceManufacturer );
      wpd_device_strings[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceDescription );
    }

    CoTaskMemFree( device_ids_buffer );
  }

  wpd_values_request = make_values_request();
  if (wpd_values_request == NULL) { goto failed_main; }

  context->wpd_manager = wpd_manager;
  context->wpd_values_request = wpd_values_request;

  context->device_list.count = device_count;
  context->device_list.ids = wpd_device_ids;
  context->device_list.names = wpd_device_names;
  context->device_list.vendors = wpd_device_vendors;
  context->device_list.strings = wpd_device_strings;

  return context;

failed_full:
  CoTaskMemFree( device_ids_buffer );

failed_main:
  CoTaskMemFree( context );
  IUnknown_Release( wpd_manager );

failed:
  return NULL;
}}

#define make_connection_socket ZZ_PLAINMTP(make_connection_socket)
PLAINMTP_INTERNAL IPortableDevice* make_connection_socket( LPCWSTR device_id,
  plainmtp_bool read_only
) {
  IPortableDevice* result;
  IPortableDeviceValues* machine_request;
  HRESULT hr;
  DWORD file_mode, share_mode;
{
  if (read_only) {
    file_mode = GENERIC_READ;
    share_mode = FILE_SHARE_READ;
  } else {
    file_mode = GENERIC_READ | GENERIC_WRITE;
    share_mode = FILE_SHARE_EXCLUSIVE;
  }

  hr = CoCreateInstance( &CLSID_PortableDevice, NULL, CLSCTX_INPROC_SERVER, &IID_IPortableDevice,
    &result );
  if (FAILED(hr)) { return NULL; }

  hr = CoCreateInstance( &CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceValues, &machine_request );

  if (SUCCEEDED(hr)) {
    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION );
    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_MAJOR_VERSION, PLAINMTP_VERSION_MAJOR );
    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_MINOR_VERSION, PLAINMTP_VERSION_MINOR );
    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_REVISION, PLAINMTP_VERSION_BUILD );
    (void)IPortableDeviceValues_SetStringValue( machine_request,
      &WPD_CLIENT_NAME, L"plainmtp - Windows Portable Devices (WPD)" );

    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_DESIRED_ACCESS, file_mode );
    (void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_SHARE_MODE, share_mode );

    /*(void)IPortableDeviceValues_SetUnsignedIntegerValue( machine_request,
      &WPD_CLIENT_MANUAL_CLOSE_ON_DISCONNECT, TRUE );*/

    hr = IPortableDevice_Open( result, device_id, machine_request );
    IUnknown_Release( machine_request );
    if (SUCCEEDED(hr)) { return result; }
  }

  IUnknown_Release( result );
  return NULL;
}}

/*
  There's no plans to support multithreading natively at the moment, so we use:
  - CoInitialize() instead of CoInitializeEx()
  - CLSID_PortableDevice instead of CLSID_PortableDeviceFTM
*/

struct plainmtp_context_s* plainmtp_startup(void) {
  HRESULT hr;
  struct plainmtp_context_s* result;
{
  hr = CoInitialize( NULL );
  if (SUCCEEDED(hr)) {
    result = make_library_context();
    if (result != NULL) { return result; }
    CoUninitialize();
  }

  return NULL;
}}

void plainmtp_shutdown( struct plainmtp_context_s* context ) {
  size_t i;
{
  assert( context != NULL );

  /*
    We don't use the FreePortableDevicePnPIDs() function because this would require 4 loops over 1.
    https://docs.microsoft.com/en-us/windows/win32/wpd_sdk/freeportabledevicepnpids
  */

  for (i = 0; i < context->device_list.count; ++i) {
    CoTaskMemFree( (void*)context->device_list.ids[i] );
    CoTaskMemFree( (void*)context->device_list.names[i] );
    CoTaskMemFree( (void*)context->device_list.vendors[i] );
    CoTaskMemFree( (void*)context->device_list.strings[i] );
  }

  IUnknown_Release( context->wpd_manager );
  IUnknown_Release( context->wpd_values_request );

  CoTaskMemFree( context );
  CoUninitialize();
}}

struct plainmtp_device_s* plainmtp_device_start( struct plainmtp_context_s* context,
  size_t device_index, plainmtp_bool read_only
) {
  HRESULT hr;
  struct plainmtp_device_s* device = NULL;  /* STAGER requires all variables to be initialized. */
  int i;
{
  assert( context != NULL );
  assert( device_index < context->device_list.count );

  STAGER_BLOCK(i) {
    STAGER_PHASE(1, {
      device = CoTaskMemAlloc( sizeof(*device) );
      if (device == NULL) break;
    },{
      CoTaskMemFree( device );
    });

    STAGER_PHASE(2, {
      device->wpd_socket = make_connection_socket( context->device_list.ids[device_index],
        read_only );
      if (device->wpd_socket == NULL) break;
    },{
      /*IPortableDevice_Close( device->wpd_socket );*/
      IUnknown_Release( device->wpd_socket );
    });

    STAGER_PHASE(3, {
      hr = IPortableDevice_Content( device->wpd_socket, &device->wpd_content );
      if (FAILED(hr)) break;
    },{
      IUnknown_Release( device->wpd_content );
    });

    STAGER_PHASE(4, {
      hr = IPortableDeviceContent_Transfer( device->wpd_content, &device->wpd_resources );
      if (FAILED(hr)) break;
    },{
      IUnknown_Release( device->wpd_resources );
    });

    STAGER_PHASE(5, {
      hr = IPortableDeviceContent_Properties( device->wpd_content, &device->wpd_properties );
      if (FAILED(hr)) break;
    },{
      IUnknown_Release( device->wpd_properties );
    });

    STAGER_SUCCESS({
      /* As this instance is identical across all the devices, we just obtain a reference to it. */
      device->values_request = context->wpd_values_request;
      (void)IUnknown_AddRef( context->wpd_values_request );
      return device;
    });
  }

  return NULL;
}}

void plainmtp_device_finish( struct plainmtp_device_s* device ) {
{
  assert( device != NULL );

  IUnknown_Release( device->values_request );

  IUnknown_Release( device->wpd_properties );
  IUnknown_Release( device->wpd_resources );
  IUnknown_Release( device->wpd_content );

  /*IPortableDevice_Close( device->wpd_socket );*/
  IUnknown_Release( device->wpd_socket );

  CoTaskMemFree( device );
}}

/**************************************************************************************************/

#define wipe_object_image ZZ_PLAINMTP(wipe_object_image)
PLAINMTP_INTERNAL void wipe_object_image( zz_plainmtp_cursor_s* object ) {
{
  CoTaskMemFree( (void*)object->id );
  CoTaskMemFree( (void*)object->name );
}}

#define obtain_object_image ZZ_PLAINMTP(obtain_object_image)
PLAINMTP_INTERNAL plainmtp_bool obtain_object_image( zz_plainmtp_cursor_s* object,
  IPortableDeviceValues* values
) {
  HRESULT hr;
  LPWSTR tempstr;
  PROPVARIANT propvar;
  SYSTEMTIME systime;
{
  hr = IPortableDeviceValues_GetStringValue( values, &WPD_OBJECT_PERSISTENT_UNIQUE_ID, &tempstr );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }
  object->id = tempstr;

  PropVariantInit( &propvar );
  hr = IPortableDeviceValues_GetValue( values, &WPD_OBJECT_DATE_MODIFIED, &propvar );

  if ( SUCCEEDED(hr)
    && (V_VT(&propvar) == VT_DATE)
    && VariantTimeToSystemTime(propvar.date, &systime)
  ) {
    object->datetime.tm_year = systime.wYear - 1900;
    object->datetime.tm_mon = systime.wMonth - 1;
    object->datetime.tm_mday = systime.wDay;
    object->datetime.tm_hour = systime.wHour;
    object->datetime.tm_min = systime.wMinute;
    object->datetime.tm_sec = systime.wSecond;
    object->datetime.tm_wday = systime.wDayOfWeek;
    object->datetime.tm_yday = -1;  /* To be adjusted by mktime() call. */
    (void)mktime( &object->datetime );

    object->datetime.tm_isdst = -1;  /* DST information is not available. */
  } else {
    object->datetime.tm_mday = 0;  /* There's no datetime information at all. */
  }

  (void)PropVariantClear( &propvar );

  /* First of all, we prefer names that are similar to filesystem ones. */
  hr = IPortableDeviceValues_GetStringValue( values, &WPD_OBJECT_ORIGINAL_FILE_NAME, &tempstr );
  if (SUCCEEDED(hr)) { goto quit; }

  /* This is required at least for storage objects because they don't have a proper filename. */
  hr = IPortableDeviceValues_GetStringValue( values, &WPD_OBJECT_HINT_LOCATION_DISPLAY_NAME,
    &tempstr );
  if (SUCCEEDED(hr)) { goto quit; }

  /* The most common and versatile property. */
  hr = IPortableDeviceValues_GetStringValue( values, &WPD_OBJECT_NAME, &tempstr );
  if (SUCCEEDED(hr)) { goto quit; }

  /* Next are special options for exceptional cases. Say, devices connected using the Mass Storage
    Class protocol may not report WPD_OBJECT_NAME for the root (DEVICE) object - e.g. Sony DSC-H50.
    NB: We don't check WPD_STORAGE_SERIAL_NUMBER and WPD_DEVICE_SERIAL_NUMBER because they're often
    present with empty or invalid (garbage) string values instead of being simply missing. */
  hr = IPortableDeviceValues_GetStringValue( values, &WPD_DEVICE_FRIENDLY_NAME, &tempstr );
  if (FAILED(hr)) { tempstr = NULL; }

quit:
  object->name = tempstr;
  return PLAINMTP_TRUE;
}}

#define clear_cursor ZZ_PLAINMTP(clear_cursor)
PLAINMTP_INTERNAL void clear_cursor( struct plainmtp_cursor_s* cursor ) {
{
  wipe_object_image( &cursor->current_object );
  IUnknown_Release( cursor->current_values );

  if (cursor->parent_values != NULL) {
    wipe_object_image( &cursor->parent_object );
    IUnknown_Release( cursor->parent_values );
  }

  if (cursor->enumerator != NULL) {
    IUnknown_Release( cursor->enumerator );
  }
}}

/* NB: This enforces atomic one-time change to preserve cursor initial state in case of error. */
#define setup_cursor_by_values ZZ_PLAINMTP(setup_cursor_by_values)
PLAINMTP_INTERNAL struct plainmtp_cursor_s* setup_cursor_by_values(
  struct plainmtp_cursor_s* cursor, IPortableDeviceValues* values
) {
  zz_plainmtp_cursor_s object;
{
  if ( !obtain_object_image( &object, values ) ) { return NULL; }

  if (cursor == NULL) {
    cursor = CoTaskMemAlloc( sizeof(*cursor) );
    if (cursor == NULL) {
      wipe_object_image( &object );
      return NULL;
    }
  } else {
    clear_cursor( cursor );
  }

  cursor->current_object = object;
  cursor->current_values = values;  /* Caller must perform ::AddRef() by itself, if necessary. */
  cursor->parent_values = NULL;
  cursor->enumerator = NULL;

  return cursor;
}}

#define setup_cursor_by_handle ZZ_PLAINMTP(setup_cursor_by_handle)
PLAINMTP_INTERNAL struct plainmtp_cursor_s* setup_cursor_by_handle(
  struct plainmtp_cursor_s* cursor, struct plainmtp_device_s* device, LPCWSTR handle
) {
  HRESULT hr;
  IPortableDeviceValues* values;
{
  hr = IPortableDeviceProperties_GetValues( device->wpd_properties, handle, device->values_request,
    &values );
  if (FAILED(hr)) { return NULL; }

  cursor = setup_cursor_by_values( cursor, values );
  if (cursor == NULL) { IUnknown_Release( values ); }

  return cursor;
}}

struct plainmtp_cursor_s* plainmtp_cursor_assign( struct plainmtp_cursor_s* cursor,
  struct plainmtp_cursor_s* source ) {
{
  if (cursor != source) {
    if (source == NULL) {
      clear_cursor( cursor );
      CoTaskMemFree( cursor );
      return NULL;
    }

    cursor = setup_cursor_by_values( cursor, source->current_values );
    if (cursor != NULL) { (void)IUnknown_AddRef( source->current_values ); }
  }

  return cursor;
}}

struct plainmtp_cursor_s* plainmtp_cursor_switch( struct plainmtp_cursor_s* cursor,
  const wchar_t* entity_id, struct plainmtp_device_s* device
) {
  LPWSTR handle;
{
  assert( device != NULL );

  if (entity_id == NULL) {
    /* In WPD, this value is the same for both PUID and session-based handle of the root object. */
    entity_id = WPD_DEVICE_OBJECT_ID;
    handle = NULL;
  } else {
    handle = make_object_handle_from_puid( device->wpd_content, entity_id );
    if (handle == NULL) { return NULL; }
    entity_id = handle;
  }

  cursor = setup_cursor_by_handle( cursor, device, entity_id );
  CoTaskMemFree( handle );
  return cursor;
}}

plainmtp_bool plainmtp_cursor_update( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device
) {
  HRESULT hr;
  LPWSTR handle;
{
  assert( cursor != NULL );
  assert( device != NULL );

  hr = IPortableDeviceValues_GetStringValue( cursor->current_values, &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  cursor = setup_cursor_by_handle( cursor, device, handle );
  CoTaskMemFree( handle );

  return (cursor != NULL);
}}

plainmtp_bool plainmtp_cursor_return( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device
) {
  HRESULT hr;
  LPWSTR handle;
  plainmtp_bool is_shadowed, is_root;
{
  assert( cursor != NULL );

  is_shadowed = cursor->parent_values != NULL;
  if (device == NULL) { return is_shadowed; }

  if (is_shadowed) {
    wipe_object_image( &cursor->current_object );
    IUnknown_Release( cursor->current_values );

    cursor->current_object = cursor->parent_object;
    cursor->current_values = cursor->parent_values;
    cursor->parent_values = NULL;

    return PLAINMTP_TRUE;
  }

  hr = IPortableDeviceValues_GetStringValue( cursor->current_values, &WPD_OBJECT_PARENT_ID,
    &handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  /* NB: The semantics of this function is such that it should return FALSE on error only. On the
    other hand, there's no difference in cursor semantics between root and child objects, and API
    doesn't provide a capability to distinguish them as well - which is in line with WPD.
    Therefore, trying to jump from the root cursor to its parent is not an error - we consider the
    root to be the parent of itself. It's worth mentioning that WPD seems to guarantee that the
    parent handle of the root object will always be an empty string - see WPD_OBJECT_PARENT_ID
    description here: https://docs.microsoft.com/en-us/windows/win32/wpd_sdk/object-properties */
  is_root = handle[0] == L'\0';

  cursor = setup_cursor_by_handle( cursor, device, handle );
  CoTaskMemFree( handle );

  return (cursor != NULL) || is_root;
}}

plainmtp_bool plainmtp_cursor_select( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device
) {
  HRESULT hr;
  IPortableDeviceValues* values;
  LPWSTR handle;
{
  assert( cursor != NULL );

  if (device == NULL) {
    if (cursor->enumerator == NULL) { return PLAINMTP_TRUE; }
    if (cursor->parent_values != NULL) {
      IUnknown_Release( cursor->enumerator );
      cursor->enumerator = NULL;

      wipe_object_image( &cursor->parent_object );
      IUnknown_Release( cursor->parent_values );
      cursor->parent_values = NULL;
    }

    return PLAINMTP_FALSE;
  }

  if (cursor->parent_values == NULL) {
    if (cursor->enumerator != NULL) {
      hr = IEnumPortableDeviceObjectIDs_Reset( cursor->enumerator );
    } else {
      hr = IPortableDeviceValues_GetStringValue( cursor->current_values, &WPD_OBJECT_ID, &handle );
      if (FAILED(hr)) { return PLAINMTP_FALSE; }

      hr = IPortableDeviceContent_EnumObjects( device->wpd_content, 0, handle, NULL,
        &cursor->enumerator );
      CoTaskMemFree( handle );
    }

    if (FAILED(hr)) { return PLAINMTP_FALSE; }
    cursor->parent_object = cursor->current_object;
    cursor->parent_values = cursor->current_values;
  } else {
    wipe_object_image( &cursor->current_object );
    IUnknown_Release( cursor->current_values );
  }

  hr = IEnumPortableDeviceObjectIDs_Next( cursor->enumerator, 1, &handle, NULL );
  if (hr == S_OK) {
    hr = IPortableDeviceProperties_GetValues( device->wpd_properties, handle,
      device->values_request, &values );
    CoTaskMemFree( handle );

    if (SUCCEEDED(hr)) {
      if ( obtain_object_image( &cursor->current_object, values ) ) {
        cursor->current_values = values;
        return PLAINMTP_TRUE;
      }

      IUnknown_Release( values );
    }
  }

  cursor->current_object = cursor->parent_object;
  cursor->current_values = cursor->parent_values;
  cursor->parent_values = NULL;

  if (FAILED(hr)) {
    IUnknown_Release( cursor->enumerator );
    cursor->enumerator = NULL;
  }

  return PLAINMTP_FALSE;
}}

/**************************************************************************************************/

#define make_transfer_stream ZZ_PLAINMTP(make_transfer_stream)
PLAINMTP_INTERNAL IStream* make_transfer_stream( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device, const wchar_t* name, uint64_t size,
  DWORD* OUT_optimal_chunk_size
) {
  HRESULT hr;
  IPortableDeviceValues* transfer_request;
  IStream* stream = NULL;
  LPWSTR handle;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceValues, &transfer_request );
  if (FAILED(hr)) { return NULL; }

  hr = IPortableDeviceValues_SetStringValue( transfer_request, &WPD_OBJECT_NAME, name );
  if (FAILED(hr)) { goto cleanup; }

  hr = IPortableDeviceValues_SetUnsignedLargeIntegerValue( transfer_request, &WPD_OBJECT_SIZE,
    size );
  if (FAILED(hr)) { goto cleanup; }

  hr = IPortableDeviceValues_GetStringValue( cursor->current_values, &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { goto cleanup; }

  hr = IPortableDeviceValues_SetStringValue( transfer_request, &WPD_OBJECT_PARENT_ID, handle );
  CoTaskMemFree( handle );
  if (FAILED(hr)) { goto cleanup; }

  (void)IPortableDeviceContent_CreateObjectWithPropertiesAndData( device->wpd_content,
    transfer_request, &stream, OUT_optimal_chunk_size, NULL );

cleanup:
  IUnknown_Release( transfer_request );
  return stream;
}}

#define stream_write ZZ_PLAINMTP(stream_write)
PLAINMTP_INTERNAL size_t stream_write( IStream* stream, const char* data, size_t size ) {
  const size_t result = size;
{
  /* NB: If 'data' is NULL, it will be checked and result in STG_E_INVALIDPOINTER. */
  do {
    ULONG bytes_written = 0;
    (void)ISequentialStream_Write( stream, data, (ULONG)size, &bytes_written );
    if (bytes_written == 0) { return 0; }
    data += bytes_written;
    size -= bytes_written;
  } while (size > 0);

  return result;
}}

plainmtp_bool plainmtp_cursor_receive( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device, size_t chunk_limit, plainmtp_data_f callback,
  void* custom_state
) {
  HRESULT hr;
  IStream* stream;
  LPWSTR handle;
  DWORD optimal_chunk_size;
  void* buffer;
{
  assert( cursor != NULL );
  assert( device != NULL );
  assert( callback != NULL );

  hr = IPortableDeviceValues_GetStringValue( cursor->current_values, &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  hr = IPortableDeviceResources_GetStream( device->wpd_resources, handle, &WPD_RESOURCE_DEFAULT,
    STGM_READ, &optimal_chunk_size, &stream );
  CoTaskMemFree( handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  /* NB: The IStream::Stat() method is not implemented for IPortableDeviceDataStream (returns
    E_NOTIMPL), making it impossible to obtain a guaranteed actual object size to be received. */

  if ( (chunk_limit == 0) || (optimal_chunk_size < chunk_limit) ) {
    chunk_limit = optimal_chunk_size;
  }

  buffer = callback( NULL, chunk_limit, custom_state );
  if (buffer == NULL) {
    hr = E_FAIL;
  } else {
    void* chunk = buffer;
    ULONG bytes_read;

    /* NB: IPortableDeviceDataStream does not comply with the ISequentialStream::Read() method
      specification, which requires S_FALSE to be returned if fewer bytes than requested have been
      read without any errors due to the end of the stream. In this case, it still returns S_OK. */

    while (
      bytes_read = 0,  /* NB: IPortableDeviceDataStream::Read() does not set this to 0 on error. */
      hr = ISequentialStream_Read( stream, chunk, (ULONG)chunk_limit, &bytes_read ),
      bytes_read != 0
    ) {
      /* NB: If the callback unexpectedly returns NULL, this will lead to STG_E_INVALIDPOINTER. */
      chunk = callback( chunk, bytes_read, custom_state );
    }

    (void)callback( buffer, 0, custom_state );
  }

  IUnknown_Release( stream );
  return SUCCEEDED(hr);
}}

plainmtp_bool plainmtp_cursor_transfer( struct plainmtp_cursor_s* parent,
  struct plainmtp_device_s* device, const wchar_t* name, uint64_t size, size_t chunk_limit,
  plainmtp_data_f callback, void* custom_state, struct plainmtp_cursor_s** SET_cursor
) {
  plainmtp_bool result = PLAINMTP_FALSE;
  HRESULT hr;
  IStream* stream;
  LPWSTR handle;
  DWORD optimal_chunk_size;
  void* buffer;
{
  assert( parent != NULL );
  assert( device != NULL );

  stream = make_transfer_stream( parent, device, name, size, &optimal_chunk_size );
  if (stream == NULL) { return PLAINMTP_FALSE; }

  if (callback != NULL) {
    if ( (chunk_limit == 0) || (optimal_chunk_size < chunk_limit) ) {
      chunk_limit = (optimal_chunk_size < size) ? optimal_chunk_size : (size_t)size;
    }

    /*if (size < chunk_limit) { chunk_limit = (size_t)size; }*/
    buffer = callback( NULL, chunk_limit, custom_state );

    if (buffer != NULL) {
      void* chunk = buffer;

      while (
        chunk_limit = stream_write( stream, chunk, chunk_limit ),
        size -= chunk_limit,
        (size > 0) && (chunk_limit > 0)
      ) {
        if (size < chunk_limit) { chunk_limit = (size_t)size; }
        chunk = callback( chunk, chunk_limit, custom_state );
      }

      (void)callback( buffer, 0, custom_state );
    }
  }

  if (size > 0) {
    assert( callback != NULL );
    goto cleanup;
  }

  hr = IStream_Commit( stream, STGC_DEFAULT );
  if (FAILED(hr)) { goto cleanup; }

  if (SET_cursor != NULL) {
    IPortableDeviceDataStream* wpd_stream;
    hr = IUnknown_QueryInterface( stream, &IID_IPortableDeviceDataStream, &wpd_stream );
    if (FAILED(hr)) { goto no_cursor; }

    hr = IPortableDeviceDataStream_GetObjectID( wpd_stream, &handle );
    IUnknown_Release( wpd_stream );

    if (SUCCEEDED(hr)) {
      *SET_cursor = setup_cursor_by_handle( *SET_cursor, device, handle );
      CoTaskMemFree( handle );
    } else {
no_cursor:
      *SET_cursor = NULL;
    }
  }

  result = PLAINMTP_TRUE;
cleanup:
  IUnknown_Release( stream );
  return result;
}}

#ifdef PP_PLAINMTP_MAIN_C_EX
#include PP_PLAINMTP_MAIN_C_EX
#endif
