#include "plainmtp.h"

#include <assert.h>

/* https://docs.microsoft.com/en-us/windows/win32/winprog/using-the-windows-headers */
/* https://docs.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt */
#define NTDDI_VERSION 0x06010000  /* NTDDI_WIN7 */
#define WINVER 0x0601  /* _WIN32_WINNT_WIN7 */
#define _WIN32_WINNT WINVER
#define WIN32_LEAN_AND_MEAN
#define CONST_VTABLE
#include <Windows.h>
#include <ObjBase.h>
#include <PropIdl.h>
#include <PropVarUtil.h>
#include <PortableDeviceTypes.h>
#include <PortableDeviceApi.h>
#include <PortableDevice.h>

#if defined(_MSC_VER) && !defined(PLAINMTP_NO_PRAGMA_LINKAGE)
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

#define FILE_SHARE_EXCLUSIVE (0)

#define INVOKE( interface, method ) \
  (interface)->lpVtbl method ( (interface)

#define INVOKE_0( interface, method ) \
  ( (interface)->lpVtbl method ( interface ) )

#define INVOKE_1( interface, method, argument ) \
  ( (interface)->lpVtbl method ( interface, argument ) )

#define RELEASE_INSTANCE( interface ) \
  ( (void)( (interface)->lpVtbl->Release( interface ) ) )

#define RELEASE_INSTANCE_SAFE( interface ) \
  if ((interface) != NULL) RELEASE_INSTANCE( interface )

typedef HRESULT (STDMETHODCALLTYPE *device_info_string_f) (
  IPortableDeviceManager*, LPCWSTR, WCHAR*, DWORD* );

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
    were percent-encoded. But ->GetObjectIDsFromPersistentUniqueIDs() method understood such a PUID
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

struct zz_plainmtp_context_s {
  /* MUST be the first field for typecasting to public type. */
  struct zz_plainmtp_registry_s device_list;

  IPortableDeviceManager* wpd_manager;
  IPortableDeviceKeyCollection* wpd_values_request;
};

struct zz_plainmtp_device_s {
  IPortableDevice* wpd_socket;
  IPortableDeviceContent* wpd_content;
  IPortableDeviceResources* wpd_resources;
  IPortableDeviceProperties* wpd_properties;
  IPortableDeviceKeyCollection* values_request;
};

/* TODO: IPortableDeviceValues is used to achieve cost-free reference counting semantics (which is
  useful e.g. for cursor duplication) and length-aware strings that can be obtained without calling
  wcslen() to calculate the buffer size. However, it requires HRESULT error check for every access,
  which is not ergonomic for plain values (such as WPD_OBJECT_SIZE), so it's better to store them
  separately (for example, in a struct, containing 'uint64_t size' and IPortableDeviceValues). To
  do this, make_values_request() should be split into requests for regular and temporary values. */
struct zz_plainmtp_cursor_s {
  /* MUST be the first field for typecasting to public type. */
  struct zz_plainmtp_image_s current_object;

  /* Contains undefined values if parent_values == NULL. */
  struct zz_plainmtp_image_s parent_object;

  IPortableDeviceValues* current_values;
  IPortableDeviceValues* parent_values;  /* If not NULL, there's an enumeration in progress. */

  /* If NULL, the current object was never enumerated, or enumeration has ended with an error. */
  IEnumPortableDeviceObjectIDs* enumerator;
};

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

  - Keep in mind the presence of a NULL check in CoTaskMemFree() and apply it where possible to
    use RELEASE_INSTANCE() instead of RELEASE_INSTANCE_SAFE(), with no variable initialization.

  - Use the facts that on acquiring the first resource, cleanup is not needed in case of error, and
    that the last resource acquired before return doesn't need release code in the cleanup block.
    Also remember that usage of contiguous memory blocks is not only more efficient than separate
    allocations but also requires only one check and cleanup operation.
*/

static LPWSTR make_device_info( IPortableDeviceManager* wpd_manager, LPCWSTR device_id,
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

/* NB: We need this rather meaningless function because the C95 standard prohibits to copy a value
  of type 'T*' into an object of type 'const T*' via a pointer of type 'T**', while at the same
  time assigning them without an explicit typecast is permitted. So we're unable to just directly
  pass a LPCWSTR buffer to the ->GetDevices() method that expects a LPWSTR one.
  https://stackoverflow.com/questions/74370703/modify-pointer-to-const-variable-const-int-through-a-pointer-to-pointer-to
  Seems like an arbitrary (i.e. unnecessarily restrictive) constraint of C95 at least. */
static DWORD obtain_wpd_device_ids( IPortableDeviceManager* wpd_manager, LPCWSTR* buffer,
  DWORD max_count _In_range_(1, max_count) /* suppress C6385 (shut up, MSVC) */
) {
  HRESULT hr;
  LPWSTR* wpd_device_ids;
  DWORD i;
{
  wpd_device_ids = CoTaskMemAlloc( max_count * sizeof(*wpd_device_ids) );
  if (wpd_device_ids == NULL) { return 0; }

  hr = INVOKE( wpd_manager, ->GetDevices ), wpd_device_ids, &max_count );
  if (SUCCEEDED(hr)) {
    for (i = 0; i < max_count; ++i) { buffer[i] = wpd_device_ids[i]; }
  } else {
    max_count = 0;
  }

  CoTaskMemFree( wpd_device_ids );
  return max_count;
}}

/* NB: This will successfully return an empty string in case of invalid PUID specified. */
static LPWSTR make_object_handle_from_puid( IPortableDeviceContent* wpd_content,
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

  hr = INVOKE_1( puid_list, ->Add, &propvar );

  /* It's necessary to explicitly state that we don't own the string to prevent deallocation. */
  V_UNION(&propvar, pwszVal) = NULL;
  (void)PropVariantClear( &propvar );

  if (SUCCEEDED(hr)) {
    hr = INVOKE( wpd_content, ->GetObjectIDsFromPersistentUniqueIDs ), puid_list, &handle_list );
  }

  RELEASE_INSTANCE( puid_list );
  if (FAILED(hr)) { return NULL; }

  /* PropVariantClear() reinitializes the variant, so we don't invoke PropVariantInit() again. */
  hr = INVOKE( handle_list, ->GetAt ), 0, &propvar );
  RELEASE_INSTANCE( handle_list );
  if (FAILED(hr)) { return NULL; }

  (void)PropVariantToStringAlloc( &propvar, &result );  /* Will set 'result' to NULL on error. */
  (void)PropVariantClear( &propvar );

  return result;
}}

static IPortableDeviceKeyCollection* make_values_request(void) {
  HRESULT hr;
  IPortableDeviceKeyCollection* result;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceKeyCollection, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceKeyCollection, &result );

  if (SUCCEEDED(hr)) {
    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_ID );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_PERSISTENT_UNIQUE_ID );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_PARENT_ID );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_ORIGINAL_FILE_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_HINT_LOCATION_DISPLAY_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_NAME );
    if (FAILED(hr)) { goto failed; }

    hr = INVOKE_1( result, ->Add, &WPD_OBJECT_DATE_MODIFIED );
    if (FAILED(hr)) { goto failed; }
  }

  return result;

failed:
  RELEASE_INSTANCE( result );
  return NULL;
}}

static plainmtp_context_s* make_library_context(void) {
  HRESULT hr;
  IPortableDeviceManager* wpd_manager;
  IPortableDeviceKeyCollection* wpd_values_request;
  plainmtp_context_s* context = NULL;
  DWORD wpd_device_count = 0, i;
  LPCWSTR *wpd_device_ids, *wpd_device_names, *wpd_device_vendors, *wpd_device_strings;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceManager, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceManager, &wpd_manager );
  if (FAILED(hr)) { goto failed; }

  hr = INVOKE( wpd_manager, ->GetDevices ), NULL, &wpd_device_count );
  if (FAILED(hr)) { goto failed_main; }

  wpd_values_request = make_values_request();
  if (wpd_values_request == NULL) { goto failed_main; }

  context = CoTaskMemAlloc( sizeof(*context) + 4 * (size_t)wpd_device_count *
    sizeof(*wpd_device_ids) );
  if (context == NULL) { goto failed_full; }

  if ( wpd_device_count > 0 ) {
    wpd_device_ids = (LPCWSTR*)(context + 1);
    wpd_device_count = obtain_wpd_device_ids( wpd_manager, wpd_device_ids, wpd_device_count );
    if (wpd_device_count == 0) { goto failed_full; }

    wpd_device_names = wpd_device_ids + wpd_device_count;
    wpd_device_vendors = wpd_device_names + wpd_device_count;
    wpd_device_strings = wpd_device_vendors + wpd_device_count;

    for (i = 0; i < wpd_device_count; ++i) {
      wpd_device_names[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceFriendlyName );
      wpd_device_vendors[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceManufacturer );
      wpd_device_strings[i] = make_device_info( wpd_manager, wpd_device_ids[i],
        wpd_manager->lpVtbl->GetDeviceDescription );
    }
  } else {
    wpd_device_ids = NULL;
    wpd_device_names = NULL;
    wpd_device_vendors = NULL;
    wpd_device_strings = NULL;
  }

  context->wpd_manager = wpd_manager;
  context->wpd_values_request = wpd_values_request;

  context->device_list.count = wpd_device_count;
  context->device_list.ids = wpd_device_ids;
  context->device_list.names = wpd_device_names;
  context->device_list.vendors = wpd_device_vendors;
  context->device_list.strings = wpd_device_strings;

  return context;

failed_full:
  CoTaskMemFree( context );
  RELEASE_INSTANCE( wpd_values_request );

failed_main:
  RELEASE_INSTANCE( wpd_manager );

failed:
  return NULL;
}}

static IPortableDevice* make_connection_socket( LPCWSTR device_id, plainmtp_bool read_only ) {
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
    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION ));
    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_MAJOR_VERSION, PLAINMTP_VERSION_MAJOR ));
    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_MINOR_VERSION, PLAINMTP_VERSION_MINOR ));
    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_REVISION, PLAINMTP_VERSION_BUILD ));
    (void)(INVOKE( machine_request, ->SetStringValue ),
      &WPD_CLIENT_NAME, L"plainmtp - Windows Portable Devices (WPD)" ));

    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_DESIRED_ACCESS, file_mode ));
    (void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_SHARE_MODE, share_mode ));

    /*(void)(INVOKE( machine_request, ->SetUnsignedIntegerValue ),
      &WPD_CLIENT_MANUAL_CLOSE_ON_DISCONNECT, TRUE ));*/

    hr = INVOKE( result, ->Open ), device_id, machine_request );
    RELEASE_INSTANCE( machine_request );
    if (SUCCEEDED(hr)) { return result; }
  }

  RELEASE_INSTANCE( result );
  return NULL;
}}

/*
  There's no plans to support multithreading natively at the moment, so we use:
  - CoInitialize() instead of CoInitializeEx()
  - CLSID_PortableDevice instead of CLSID_PortableDeviceFTM
*/

plainmtp_context_s* plainmtp_startup(void) {
  HRESULT hr;
  plainmtp_context_s* result;
{
  hr = CoInitialize( NULL );
  if (SUCCEEDED(hr)) {
    result = make_library_context();
    if (result != NULL) { return result; }
    CoUninitialize();
  }

  return NULL;
}}

void plainmtp_shutdown( plainmtp_context_s* context ) {
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

  RELEASE_INSTANCE( context->wpd_manager );
  RELEASE_INSTANCE( context->wpd_values_request );

  CoTaskMemFree( context );
  CoUninitialize();
}}

plainmtp_device_s* plainmtp_device_start( plainmtp_context_s* context, size_t device_index,
  plainmtp_bool read_only
) {
  HRESULT hr;
  plainmtp_device_s* device = NULL;  /* STAGER requires all variables to be initialized. */
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
      /*INVOKE_0( device->wpd_socket, ->Close );*/
      RELEASE_INSTANCE( device->wpd_socket );
    });

    STAGER_PHASE(3, {
      hr = INVOKE_1( device->wpd_socket, ->Content, &device->wpd_content );
      if (FAILED(hr)) break;
    },{
      RELEASE_INSTANCE( device->wpd_content );
    });

    STAGER_PHASE(4, {
      hr = INVOKE_1( device->wpd_content, ->Transfer, &device->wpd_resources );
      if (FAILED(hr)) break;
    },{
      RELEASE_INSTANCE( device->wpd_resources );
    });

    STAGER_PHASE(5, {
      hr = INVOKE_1( device->wpd_content, ->Properties, &device->wpd_properties );
      if (FAILED(hr)) break;
    },{
      RELEASE_INSTANCE( device->wpd_properties );
    });

    STAGER_SUCCESS({
      /* As this instance is identical across all the devices, we just obtain a reference to it. */
      device->values_request = context->wpd_values_request;
      (void)INVOKE_0( context->wpd_values_request, ->AddRef );
      return device;
    });
  }

  return NULL;
}}

void plainmtp_device_finish( plainmtp_device_s* device ) {
{
  assert( device != NULL );

  RELEASE_INSTANCE( device->values_request );

  RELEASE_INSTANCE( device->wpd_properties );
  RELEASE_INSTANCE( device->wpd_resources );
  RELEASE_INSTANCE( device->wpd_content );

  /*INVOKE_0( device->wpd_socket, ->Close );*/
  RELEASE_INSTANCE( device->wpd_socket );

  CoTaskMemFree( device );
}}

/**************************************************************************************************/

static void wipe_object_image( struct zz_plainmtp_image_s* object ) {
{
  CoTaskMemFree( (void*)object->id );
  CoTaskMemFree( (void*)object->name );
}}

/* TODO: Consider trying more properties for name. Devices connected using Mass Storage Class
  protocol may not report WPD_OBJECT_NAME for the root (DEVICE) object - e.g. Sony DSC-H50. */
static plainmtp_bool obtain_object_image( struct zz_plainmtp_image_s* object,
  IPortableDeviceValues* values
) {
  HRESULT hr;
  LPWSTR tempstr;
  PROPVARIANT propvar;
  SYSTEMTIME systime;
{
  hr = INVOKE( values, ->GetStringValue ), &WPD_OBJECT_PERSISTENT_UNIQUE_ID, &tempstr );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }
  object->id = tempstr;

  PropVariantInit( &propvar );
  hr = INVOKE( values, ->GetValue ), &WPD_OBJECT_DATE_MODIFIED, &propvar );

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

  hr = INVOKE( values, ->GetStringValue ), &WPD_OBJECT_ORIGINAL_FILE_NAME, &tempstr );
  if (SUCCEEDED(hr)) { goto quit; }

  /* This is required at least for storage objects because they don't have a proper filename. */
  hr = INVOKE( values, ->GetStringValue ), &WPD_OBJECT_HINT_LOCATION_DISPLAY_NAME, &tempstr );
  if (SUCCEEDED(hr)) { goto quit; }
  hr = INVOKE( values, ->GetStringValue ), &WPD_OBJECT_NAME, &tempstr );
  if (FAILED(hr)) { tempstr = NULL; }

quit:
  object->name = tempstr;
  return PLAINMTP_TRUE;
}}

static void clear_cursor( plainmtp_cursor_s* cursor ) {
{
  wipe_object_image( &cursor->current_object );
  RELEASE_INSTANCE( cursor->current_values );

  if (cursor->parent_values != NULL) {
    wipe_object_image( &cursor->parent_object );
    RELEASE_INSTANCE( cursor->parent_values );
  }

  RELEASE_INSTANCE_SAFE( cursor->enumerator );
}}

/* NB: This enforces atomic one-time change to preserve cursor initial state in case of error. */
static plainmtp_cursor_s* setup_cursor_by_values( plainmtp_cursor_s* cursor,
  IPortableDeviceValues* values
) {
  struct zz_plainmtp_image_s object;
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
  cursor->current_values = values;  /* Caller must perform ->AddRef() by itself, if necessary. */
  cursor->parent_values = NULL;
  cursor->enumerator = NULL;

  return cursor;
}}

static plainmtp_cursor_s* setup_cursor_by_handle( plainmtp_cursor_s* cursor,
  plainmtp_device_s* device, LPCWSTR handle
) {
  HRESULT hr;
  IPortableDeviceValues* values;
{
  hr = INVOKE( device->wpd_properties, ->GetValues ), handle, device->values_request, &values );
  if (FAILED(hr)) { return NULL; }

  cursor = setup_cursor_by_values( cursor, values );
  if (cursor == NULL) { RELEASE_INSTANCE( values ); }

  return cursor;
}}

plainmtp_cursor_s* plainmtp_cursor_assign( plainmtp_cursor_s* cursor, plainmtp_cursor_s* source ) {
{
  if (cursor != source) {
    if (source == NULL) {
      clear_cursor( cursor );
      CoTaskMemFree( cursor );
      return NULL;
    }

    cursor = setup_cursor_by_values( cursor, source->current_values );
    if (cursor != NULL) { (void)INVOKE_0( source->current_values, ->AddRef ); }
  }

  return cursor;
}}

plainmtp_cursor_s* plainmtp_cursor_switch( plainmtp_cursor_s* cursor, const wchar_t* entity_id,
  plainmtp_device_s* device
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

plainmtp_bool plainmtp_cursor_update( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  HRESULT hr;
  LPWSTR handle;
{
  assert( cursor != NULL );
  assert( device != NULL );

  hr = INVOKE( cursor->current_values, ->GetStringValue ), &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  cursor = setup_cursor_by_handle( cursor, device, handle );
  CoTaskMemFree( handle );

  return (cursor != NULL);
}}

plainmtp_bool plainmtp_cursor_return( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  HRESULT hr;
  LPWSTR handle;
  plainmtp_bool is_shadowed, is_root;
{
  assert( cursor != NULL );

  is_shadowed = cursor->parent_values != NULL;
  if (device == NULL) { return is_shadowed; }

  if (is_shadowed) {
    wipe_object_image( &cursor->current_object );
    RELEASE_INSTANCE( cursor->current_values );

    cursor->current_object = cursor->parent_object;
    cursor->current_values = cursor->parent_values;
    cursor->parent_values = NULL;

    return PLAINMTP_TRUE;
  }

  hr = INVOKE( cursor->current_values, ->GetStringValue ), &WPD_OBJECT_PARENT_ID, &handle );
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

plainmtp_bool plainmtp_cursor_select( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  HRESULT hr;
  IPortableDeviceValues* values;
  LPWSTR handle;
{
  assert( cursor != NULL );

  if (device == NULL) {
    if (cursor->enumerator == NULL) { return PLAINMTP_TRUE; }
    if (cursor->parent_values != NULL) {
      RELEASE_INSTANCE( cursor->enumerator );
      cursor->enumerator = NULL;

      wipe_object_image( &cursor->parent_object );
      RELEASE_INSTANCE( cursor->parent_values );
      cursor->parent_values = NULL;
    }

    return PLAINMTP_FALSE;
  }

  if (cursor->parent_values == NULL) {
    if (cursor->enumerator != NULL) {
      hr = INVOKE_0( cursor->enumerator, ->Reset );
    } else {
      hr = INVOKE( cursor->current_values, ->GetStringValue ), &WPD_OBJECT_ID, &handle );
      if (FAILED(hr)) { return PLAINMTP_FALSE; }

      hr = INVOKE( device->wpd_content, ->EnumObjects ), 0, handle, NULL, &cursor->enumerator );
      CoTaskMemFree( handle );
    }

    if (FAILED(hr)) { return PLAINMTP_FALSE; }
    cursor->parent_object = cursor->current_object;
    cursor->parent_values = cursor->current_values;
  } else {
    wipe_object_image( &cursor->current_object );
    RELEASE_INSTANCE( cursor->current_values );
  }

  hr = INVOKE( cursor->enumerator, ->Next ), 1, &handle, NULL );
  if (hr == S_OK) {
    hr = INVOKE( device->wpd_properties, ->GetValues ), handle, device->values_request, &values );
    CoTaskMemFree( handle );

    if (SUCCEEDED(hr)) {
      if ( obtain_object_image( &cursor->current_object, values ) ) {
        cursor->current_values = values;
        return PLAINMTP_TRUE;
      }

      RELEASE_INSTANCE( values );
    }
  }

  cursor->current_object = cursor->parent_object;
  cursor->current_values = cursor->parent_values;
  cursor->parent_values = NULL;

  if (FAILED(hr)) {
    RELEASE_INSTANCE( cursor->enumerator );
    cursor->enumerator = NULL;
  }

  return PLAINMTP_FALSE;
}}

/**************************************************************************************************/

static IStream* make_transfer_stream( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  const wchar_t* name, uint64_t size, DWORD* OUT_optimal_chunk_size
) {
  HRESULT hr;
  IPortableDeviceValues* transfer_request;
  IStream* stream = NULL;
  LPWSTR handle;
{
  hr = CoCreateInstance( &CLSID_PortableDeviceValues, NULL, CLSCTX_INPROC_SERVER,
    &IID_IPortableDeviceValues, &transfer_request );
  if (FAILED(hr)) { return NULL; }

  hr = INVOKE( transfer_request, ->SetStringValue ), &WPD_OBJECT_NAME, name );
  if (FAILED(hr)) { goto cleanup; }

  hr = INVOKE( transfer_request, ->SetUnsignedLargeIntegerValue ), &WPD_OBJECT_SIZE, size );
  if (FAILED(hr)) { goto cleanup; }

  hr = INVOKE( cursor->current_values, ->GetStringValue ), &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { goto cleanup; }

  hr = INVOKE( transfer_request, ->SetStringValue ), &WPD_OBJECT_PARENT_ID, handle );
  CoTaskMemFree( handle );
  if (FAILED(hr)) { goto cleanup; }

  (void)INVOKE( device->wpd_content, ->CreateObjectWithPropertiesAndData ), transfer_request,
    &stream, OUT_optimal_chunk_size, NULL );

cleanup:
  RELEASE_INSTANCE( transfer_request );
  return stream;
}}

static size_t stream_write( IStream* stream, const char* data, size_t size ) {
  const size_t result = size;
{
  /* NB: If 'data' is NULL, it will be checked and result in STG_E_INVALIDPOINTER. */
  do {
    ULONG bytes_written = 0;
    (void)INVOKE( stream, ->Write ), data, (ULONG)size, &bytes_written );
    if (bytes_written == 0) { return 0; }
    data += bytes_written;
    size -= bytes_written;
  } while (size > 0);

  return result;
}}

plainmtp_bool plainmtp_cursor_receive( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  size_t chunk_limit, plainmtp_data_f callback, void* custom_state
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

  hr = INVOKE( cursor->current_values, ->GetStringValue ), &WPD_OBJECT_ID, &handle );
  if (FAILED(hr)) { return PLAINMTP_FALSE; }

  hr = INVOKE( device->wpd_resources, ->GetStream ), handle, &WPD_RESOURCE_DEFAULT, STGM_READ,
    &optimal_chunk_size, &stream );
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
      bytes_read = 0,  /* NB: IPortableDeviceDataStream->Read() does not set this to 0 on error. */
      hr = INVOKE( stream, ->Read ), chunk, (ULONG)chunk_limit, &bytes_read ),
      bytes_read != 0
    ) {
      /* NB: If the callback unexpectedly returns NULL, this will lead to STG_E_INVALIDPOINTER. */
      chunk = callback( chunk, bytes_read, custom_state );
    }

    (void)callback( buffer, 0, custom_state );
  }

  RELEASE_INSTANCE( stream );
  return SUCCEEDED(hr);
}}

plainmtp_bool plainmtp_cursor_transfer( plainmtp_cursor_s* parent, plainmtp_device_s* device,
  const wchar_t* name, uint64_t size, size_t chunk_limit, plainmtp_data_f callback,
  void* custom_state, plainmtp_cursor_s** SET_cursor
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

  hr = INVOKE_1( stream, ->Commit, STGC_DEFAULT );
  if (FAILED(hr)) { goto cleanup; }

  if (SET_cursor != NULL) {
    IPortableDeviceDataStream* wpd_stream;
    hr = INVOKE( stream, ->QueryInterface ), &IID_IPortableDeviceDataStream, &wpd_stream );
    if (FAILED(hr)) { goto no_cursor; }

    hr = INVOKE_1( wpd_stream, ->GetObjectID, &handle );
    RELEASE_INSTANCE( wpd_stream );

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
  RELEASE_INSTANCE( stream );
  return result;
}}
