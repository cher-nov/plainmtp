#include "plainmtp.h"
#include "common.i.h"

/**************************************************************************************************/
#ifndef PP_PLAINMTP_CONFLICTING_DIRECTIVES

#define WIN32_LEAN_AND_MEAN
#define CONST_VTABLE
#define COBJMACROS
#define CINTERFACE  /* just for more obvious errors in case of attempt to compile in C++ mode */

/* https://docs.microsoft.com/en-us/windows/win32/winprog/using-the-windows-headers */
/* https://docs.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt */
/* https://devblogs.microsoft.com/oldnewthing/20070410-00/?p=27313 */
/* https://devblogs.microsoft.com/oldnewthing/20070411-00/?p=27283 */

#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN7  /* 0x06010000 */
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN7  /* 0x0601 */
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif

#include <Windows.h>
#include <PortableDeviceApi.h>

#else
#undef PP_PLAINMTP_CONFLICTING_DIRECTIVES
#endif
/**************************************************************************************************/

typedef HRESULT (STDMETHODCALLTYPE *device_info_string_f) (
  IPortableDeviceManager*, LPCWSTR, WCHAR*, DWORD* );

enum { FILE_SHARE_EXCLUSIVE = 0 };

PLAINMTP_SUBCLASS( struct plainmtp_context_s, device_list ) (
  IPortableDeviceManager* wpd_manager;
  IPortableDeviceKeyCollection* wpd_values_request;
);

struct plainmtp_device_s {
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
PLAINMTP_SUBCLASS( struct plainmtp_cursor_s, current_object ) (
  /* Contains undefined values if parent_values == NULL. */
  zz_plainmtp_cursor_s parent_object;

  IPortableDeviceValues* current_values;
  IPortableDeviceValues* parent_values;  /* If not NULL, there's an enumeration in progress. */

  /* If NULL, the current object was never enumerated, or enumeration has ended with an error. */
  IEnumPortableDeviceObjectIDs* enumerator;
);

/**************************************************************************************************/
#ifndef CC_PLAINMTP_NO_INTERNAL_API

PLAINMTP_EXTERN LPWSTR ZZ_PLAINMTP(make_device_info( IPortableDeviceManager* wpd_manager,
  LPCWSTR device_id, device_info_string_f method ));
PLAINMTP_EXTERN HRESULT ZZ_PLAINMTP(obtain_wpd_device_ids( IPortableDeviceManager* wpd_manager,
  LPWSTR** OUT_device_ids, size_t* OUT_device_count ));
PLAINMTP_EXTERN LPWSTR ZZ_PLAINMTP(make_object_handle_from_puid(
  IPortableDeviceContent* wpd_content, LPCWSTR object_puid ));

PLAINMTP_EXTERN IPortableDeviceKeyCollection* ZZ_PLAINMTP(make_values_request(void));
PLAINMTP_EXTERN struct plainmtp_context_s* ZZ_PLAINMTP(make_library_context(void));
PLAINMTP_EXTERN IPortableDevice* ZZ_PLAINMTP(make_connection_socket( LPCWSTR device_id,
  plainmtp_bool read_only ));

PLAINMTP_EXTERN void ZZ_PLAINMTP(wipe_object_image( zz_plainmtp_cursor_s* object ));
PLAINMTP_EXTERN plainmtp_bool ZZ_PLAINMTP(obtain_object_image( zz_plainmtp_cursor_s* object,
  IPortableDeviceValues* values ));
PLAINMTP_EXTERN void ZZ_PLAINMTP(clear_cursor( struct plainmtp_cursor_s* cursor ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_by_values(
  struct plainmtp_cursor_s* cursor, IPortableDeviceValues* values ));
PLAINMTP_EXTERN struct plainmtp_cursor_s* ZZ_PLAINMTP(setup_cursor_by_handle(
  struct plainmtp_cursor_s* cursor, struct plainmtp_device_s* device, LPCWSTR handle ));

PLAINMTP_EXTERN IStream* ZZ_PLAINMTP(make_transfer_stream( struct plainmtp_cursor_s* cursor,
  struct plainmtp_device_s* device, const wchar_t* name, uint64_t size,
  DWORD* OUT_optimal_chunk_size ));
PLAINMTP_EXTERN size_t ZZ_PLAINMTP(stream_write( IStream* stream, const char* data, size_t size ));

#endif /* CC_PLAINMTP_NO_INTERNAL_API */
