/* Disclaimer: All of this was reverse-engineered through trial-and-error using the official
  Microsoft tools "WPD Information Tool" (WpdInfo.exe) and "MTP Device Simulator" from WPD SDK.
  No disassembly or any other copyright-infringing approach were involved. */

#ifndef ZZ_PLAINMTP_WPD_PUID_C_IG
#define ZZ_PLAINMTP_WPD_PUID_C_IG
#include "common.i.h"

#include <wchar.h>

#include "../3rdparty/pstdint.h"

#define WPD_GUID_STRING_SIZE sizeof("{01234567-0123-0123-0123-0123456789AB}")

/* NB: A plain array for the GUID instead of an 'union' is necessary for endian-agnostic code. */
typedef uint8_t wpd_guid_plain_i[16];

extern const wchar_t wpd_root_persistent_id[];

extern wchar_t* make_wpd_storage_unique_id( uint32_t storage_id, uint64_t capacity,
  const wchar_t* volume_string, size_t volume_string_length );
extern plainmtp_bool parse_wpd_storage_unique_id( const wchar_t* source,
  uint32_t* OUT_storage_id );

extern plainmtp_bool read_wpd_plain_guid( wpd_guid_plain_i result, const wchar_t* source );
extern void write_wpd_plain_guid( const wpd_guid_plain_i source, wchar_t* result );

extern void get_wpd_fallback_object_id( wpd_guid_plain_i result, const wchar_t* name,
  uint32_t handle, uint32_t parent, uint32_t storage, uint32_t size );

#else
#error ZZ_PLAINMTP_WPD_PUID_C_IG
#endif
