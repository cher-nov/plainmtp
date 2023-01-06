#include "utf8_wchar.c.h"

#include <stdlib.h>

size_t utf8_strlen( const char* utf8_string ) {
  size_t result = 0;
{
  for (;;) {
    const char unit = *utf8_string;

    if ('\0' == unit) {
      return result;
    } else if ( (0xF8 & unit) == 0xF0 ) {
      utf8_string += 4;
    } else if ( (0xF0 & unit) == 0xE0 ) {
      utf8_string += 3;
    } else if ( (0xE0 & unit) == 0xC0 ) {
      utf8_string += 2;
    } else {
      utf8_string += 1;
    }

    ++result;
  }
}}

wchar_t* make_wide_string_from_utf8( const char* utf8_string, size_t* OUT_length ) {
  wchar_t* result;
  unsigned long codepoint;
  size_t length, i;
{
  /*if (utf8_string == NULL) { return NULL; }*/

  length = utf8_strlen( utf8_string );
  if (OUT_length != NULL) { *OUT_length = length; }

  result = malloc( (length+1) * sizeof(*result) );
  if (result == NULL) { return NULL; }

  for (i = 0; i < length; ++i) {
    if ( (0xF8 & utf8_string[0]) == 0xF0 ) {
      codepoint = (0x07 & utf8_string[0]) << 18
                | (0x3F & utf8_string[1]) << 12
                | (0x3F & utf8_string[2]) << 6
                | (0x3F & utf8_string[3]);
      utf8_string += 4;

    } else if ( (0xF0 & utf8_string[0]) == 0xE0 ) {
      codepoint = (0x0F & utf8_string[0]) << 12
                | (0x3F & utf8_string[1]) << 6
                | (0x3F & utf8_string[2]);
      utf8_string += 3;

    } else if ( (0xE0 & utf8_string[0]) == 0xC0 ) {
      codepoint = (0x1F & utf8_string[0]) << 6
                | (0x3F & utf8_string[1]);
      utf8_string += 2;

    } else {
      codepoint = utf8_string[0];
      utf8_string += 1;
    }

    /* NB: Range of wchar_t is implementation-defined; this is possibly a narrowing conversion. */
    result[i] = (wchar_t)codepoint;
  }

  result[length] = L'\0';
  return result;
}}

/* TODO: This implementation is locale-dependent, should we do anything about it? */
char* make_multibyte_string( const wchar_t* source ) {
  char* result;
  size_t length;
  mbstate_t state = {0};
{
  length = wcsrtombs( NULL, &source, 0, &state );
  if (length == (size_t)-1) { return NULL; }

  ++length;
  result = malloc( length );
  if (result == NULL) { return NULL; }

  length = wcsrtombs( result, &source, length, &state );
  if (length != (size_t)-1) { return result; }

  free( result );
  return NULL;
}}
