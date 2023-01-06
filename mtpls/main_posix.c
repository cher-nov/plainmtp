#define _LARGEFILE64_SOURCE
#include <stdlib.h>
#include <stdio.h>

#include "../3rdparty/pstdint.h"
#include "../plainmtp/utf8_wchar.c.h"

extern int wmain( int argc, wchar_t* argv[] );

/* Why the heck is the 'mode' parameter of Microsoft's _wfopen() a wide string?! */
FILE* _wfopen( const wchar_t* filename, const wchar_t* mode ) {
  FILE* result = NULL;
  char *mbs_filename, *mbs_mode;
{
  mbs_filename = make_multibyte_string( filename );
  if (mbs_filename == NULL) { return NULL; }

  mbs_mode = make_multibyte_string( mode );
  if (mbs_mode != NULL) {
    result = fopen( mbs_filename, mbs_mode );
    free( mbs_mode );
  }

  free( mbs_filename );
  return result;
}}

int _fseeki64( FILE* stream, int64_t offset, int origin ) {
  return fseeko64( stream, (__off64_t)offset, origin );
}

int64_t _ftelli64( FILE* stream ) {
  return (int64_t)ftello64( stream );
}

/* NB: This assumes argv[] to be encoded in UTF-8, like modern Linux does (as of 2022). */
int main( int argc, char* argv[] ) {
  wchar_t **wstr_argv, **argv_wmain;
  int result = EXIT_FAILURE, i;
{
  wstr_argv = malloc( (argc*2 + 1) * sizeof(*wstr_argv) );
  if (wstr_argv == NULL) { goto failed; }

  /* A separate copy is required because the main function is allowed to change argv[] values. */
  argv_wmain = wstr_argv + argc;

  for (i = 0; i < argc; ++i) {
    wstr_argv[i] = make_wide_string_from_utf8( argv[i], NULL );
    if (wstr_argv[i] == NULL) { goto cleanup; }
    argv_wmain[i] = wstr_argv[i];
  }

  argv_wmain[argc] = NULL;
  result = wmain( argc, argv_wmain );

cleanup:
  while (i > 0) { free( wstr_argv[--i] ); }
  free( wstr_argv );

failed:
  return result;
}}
