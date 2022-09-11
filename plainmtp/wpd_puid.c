#include "wpd_puid.h"

#include <stdlib.h>

/* NB: Before C99, adjacent string literals had to be of the same type to be concatenated.
  https://stackoverflow.com/questions/6603275/how-to-convert-defined-string-literal-to-a-wide-string-literal
  https://stackoverflow.com/questions/21788652/how-do-i-concatenate-wide-string-literals-with-prid32-priu64-etc */
#define ZZ_WPRINTF_MODIFIER_JOIN( Prefix, Literal ) Prefix ## Literal
#define ZZ_WPRINTF_MODIFIER_EXPAND( Literal ) ZZ_WPRINTF_MODIFIER_JOIN( L, Literal )
#define WPRINTF_INT32_MODIFIER ZZ_WPRINTF_MODIFIER_EXPAND( PRINTF_INT32_MODIFIER )
#define WPRINTF_INT64_MODIFIER ZZ_WPRINTF_MODIFIER_EXPAND( PRINTF_INT64_MODIFIER )

/* Additional unique identifiers in WPD have the format '?ID-{item_0,item_1,,item_N}', where the
  first character specifies the entity type and the item list specifies some of its properties. */
#define WPD_STORAGE_ID_FORMAT_PREFIX L"SID-{%"WPRINTF_INT32_MODIFIER L"X,"

const wchar_t wpd_root_persistent_id[] = L"DEVICE";

/* NB: We assume 'unsigned int' here because pstdint.h doesn't provide PRINTF_INT8_MODIFIER. */
static const wchar_t wpd_guid_format_string[] =
  L"{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}";

#define WPD_GUID_FORMAT_LIST \
  X(0),X(1),X(2),X(3),  X(4),X(5),  X(6),X(7),  X(8),X(9),  X(10),X(11),X(12),X(13),X(14),X(15)

#define WPD_GUID_FORMAT_COUNT 16

wchar_t* make_wpd_storage_unique_id( uint32_t storage_id, uint64_t capacity,
  const wchar_t* volume_string, size_t volume_string_length
) {
  wchar_t *buffer, *result;
  int length;

  /* Unfortunately, swprintf() does not allow to calculate the required size of the buffer that is
    allocated to store the result, like C99 snprintf() does, so we had to use the maximum possible.
    https://stackoverflow.com/questions/67510981/why-the-different-behavior-of-snprintf-vs-swprintf
    https://stackoverflow.com/questions/3693479/why-does-c-not-have-an-snwprintf-function */
  size_t length_limit
    = 8  /* Number of identifier syntax characters ('SID-{,,}'). */
    + 8  /* Length of the UINT32_MAX value hexadecimal representation ('FFFFFFFF'). */
    + 20  /* Length of the UINT64_MAX value decimal representation ('18446744073709551615'). */
    + 1;  /* For the resulting null-terminator. */
{
  if (volume_string == NULL) {
    volume_string = L"";
  } else {
    /* 254 is the maximum length of PTP/MTP protocol strings (excluding the null-terminator). */
    length_limit += (volume_string_length > 0) ? volume_string_length : 254;
  }

  buffer = malloc( length_limit * sizeof(*buffer) );
  if (buffer == NULL) { return NULL; }

  length = swprintf( buffer, length_limit,
    WPD_STORAGE_ID_FORMAT_PREFIX L"%ls,%"WPRINTF_INT64_MODIFIER L"u}",
    storage_id, volume_string, capacity );

  if (length > 0) {
    /* Try to reduce memory usage. */
    result = realloc( buffer, (length+1) * sizeof(*buffer) );
    return (result != NULL) ? result : buffer;
  }

  free( buffer );
  return NULL;
}}

plainmtp_bool parse_wpd_storage_unique_id( const wchar_t* source, uint32_t* OUT_storage_id ) {
{
  return ( swscanf( source, WPD_STORAGE_ID_FORMAT_PREFIX, OUT_storage_id ) == 1 );
}}

plainmtp_bool read_wpd_plain_guid( wpd_guid_plain_i result, const wchar_t* source ) {
  int filled;
  unsigned int values[WPD_GUID_FORMAT_COUNT];
{
# define X(i) &values[i]
  filled = swscanf( source, wpd_guid_format_string, WPD_GUID_FORMAT_LIST );
# undef X
  if (filled != WPD_GUID_FORMAT_COUNT) { return PLAINMTP_FALSE; }

# define X(i) result[i] = (uint8_t)values[i]
  (WPD_GUID_FORMAT_LIST);
# undef X
  return PLAINMTP_TRUE;
}}

void write_wpd_plain_guid( const wpd_guid_plain_i source, wchar_t* result ) {
{
# define X(i) (unsigned int)(source[i])
  (void)swprintf( result, WPD_GUID_STRING_SIZE, wpd_guid_format_string, WPD_GUID_FORMAT_LIST );
# undef X
}}

/*
  This is the algorithm used by Windows Portable Devices to calculate unique object identifiers on
  PTP devices that do not support "Persistent Unique Object Identifier" property available in MTP.

  This function is an endian-agnostic version of the following code, originally little-endian:

    union {
      uint32_t u4[4];
      uint16_t u2[8];
      uint8_t u1[16];
    } result;

    result.u4[0] = handle;
    result.u4[1] = parent;
    result.u4[2] = storage;
    result.u4[3] = size;

    while (name[i] != L'\0') {
      result.u2[i % 8] ^= (uint16_t)name[i];
      ++i;
    }

    printf( "{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}\n",
      (unsigned long)result.u4[0], (unsigned short)result.u2[2], (unsigned short)result.u2[3],
      (unsigned char)result.u1[8], (unsigned char)result.u1[9],
      (unsigned char)result.u1[10], (unsigned char)result.u1[11], (unsigned char)result.u1[12],
      (unsigned char)result.u1[13], (unsigned char)result.u1[14], (unsigned char)result.u1[15]
    );

  NB: In PTP, object size is only 32-bit wide, not 64-bit, so it's applicable here.
*/

void get_wpd_fallback_object_id( wpd_guid_plain_i result, const wchar_t* name, uint32_t handle,
  uint32_t parent, uint32_t storage, uint32_t size
) {
  uint16_t units[8] = {0};
  size_t i = 0;
{
  /* First, do all the arithmetic, because byte order doesn't affect the result here. */

  if (name != NULL) while (name[i] != L'\0') {
    units[i % 8] ^= (uint16_t)name[i];
    ++i;
  }

  units[0] ^= (uint16_t) handle;
  units[1] ^= (uint16_t)(handle >> 16);

  units[2] ^= (uint16_t) parent;
  units[3] ^= (uint16_t)(parent >> 16);

  units[4] ^= (uint16_t) storage;
  units[5] ^= (uint16_t)(storage >> 16);

  units[6] ^= (uint16_t) size;
  units[7] ^= (uint16_t)(size >> 16);

  /* Then, lay down the resulting units in the order they would have been printed in Windows. */

  result[0] = (uint8_t)(units[1] >> 8);
  result[1] = (uint8_t) units[1];
  result[2] = (uint8_t)(units[0] >> 8);
  result[3] = (uint8_t) units[0];

  result[4] = (uint8_t)(units[2] >> 8);
  result[5] = (uint8_t) units[2];

  result[6] = (uint8_t)(units[3] >> 8);
  result[7] = (uint8_t) units[3];

  result[8] = (uint8_t) units[4];
  result[9] = (uint8_t)(units[4] >> 8);

  result[10] = (uint8_t) units[5];
  result[11] = (uint8_t)(units[5] >> 8);
  result[12] = (uint8_t) units[6];
  result[13] = (uint8_t)(units[6] >> 8);
  result[14] = (uint8_t) units[7];
  result[15] = (uint8_t)(units[7] >> 8);
}}
