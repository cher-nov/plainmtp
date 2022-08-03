#include "wcsdup.h"

#include <stdlib.h>

wchar_t* wcsdup( const wchar_t* string ) {
  const size_t length = wcslen( string ) + 1;
  wchar_t* result = malloc( length * sizeof(*result) );
  if (result == NULL) { return NULL; }
  return wmemcpy( result, string, length );
}
