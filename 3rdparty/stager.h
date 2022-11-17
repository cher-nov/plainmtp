/*
  stager - DEFER-like macro for multiple-phase initialization with guaranteed cleanup on failure.
  Licensed under the original WTFPL license by Sam Hocevar, version 2 (December 2004).
  Copyright (c) 2022, Dmitry D. Chernov
*/

#ifndef ZZ_STAGER_H_IG
#define ZZ_STAGER_H_IG

/*
  GCC-8: gcc -std=c89 -pedantic -Wall -Wextra -E -P -x c -o stager.i stager.h
  MSVC-2019: cl /Za /permissive- /Wall /P /EP /Tc stager.h
*/

#define STAGER_BLOCK( VariableName ) \
  for (VariableName = 1; VariableName != 0; VariableName = -VariableName+1) \
  switch (VariableName) for(;0; VariableName = -VariableName)

#define STAGER_PHASE( number, code, cleanup ) \
  case (number): code continue; \
  case -(number): cleanup continue

#define STAGER_SUCCESS( code ) \
  default: code do{}while(0)

/******************************************************************************/
#ifdef STAGER_COMPILE_AND_TEST

/*
  GCC-8: gcc -std=c89 -pedantic -Wall -Wextra -O3 -DSTAGER_COMPILE_AND_TEST -x c -o stager stager.h
  MSVC-2019: cl /Za /permissive- /Wall /O2 /DSTAGER_COMPILE_AND_TEST /Tc stager.h
*/

#ifdef NDEBUG
  #undef NDEBUG
#endif

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

int stager(int abort) {
  int last_phase = 0;
{
  STAGER_BLOCK {
    STAGER_PHASE(1, {
      puts("1 stage");
      assert( 1 == ++last_phase );
      if (abort == 1) break;
    },{
      puts("1 cleanup");
      assert( 1 == --last_phase );
      assert( 1 != abort );
    });

    STAGER_PHASE(2, {
      puts("2 stage");
      assert( 2 == ++last_phase );
      if (abort == 2) break;
    },{
      puts("2 cleanup");
      assert( 2 == --last_phase );
      assert( 2 != abort );
    });

    STAGER_PHASE(3, {
      puts("3 stage");
      assert( 3 == ++last_phase );
      if (abort == 3) break;
    },{
      /* NB: Being the last phase cleanup, this must never be called. Checked by 'abort' assert. */
      puts("3 cleanup");
      assert( 3 == --last_phase );
      assert( 3 != abort );
    });

    STAGER_SUCCESS({
      puts("> SUCCESS\n");
      assert( 4 == ++last_phase );
      /*assert( 0 == abort );  // commented for cases of invalid 'abort' argument */
      return 1;
    });
  }

  puts("> FAILURE\n");
  assert( 1 == last_phase );
  return 0;
}}

int main(void) {
  assert( stager(0) == 1 );
  assert( stager(1) == 0 );
  assert( stager(2) == 0 );
  assert( stager(3) == 0 );

  assert( stager(-1) == 1 );
  assert( stager(4) == 1 );

  return EXIT_SUCCESS;
}

#endif /* STAGER_COMPILE_AND_TEST */
/******************************************************************************/

#endif /* ZZ_STAGER_H_IG */
