#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <locale.h>

#include "../plainmtp/plainmtp.h"

#define COMMAND_ENUMERATE (L'e')
#define COMMAND_LIST (L'l')
#define COMMAND_RECEIVE (L'r')
#define COMMAND_TRANSFER (L't')

#define PATH_DELIMITER (L'\\')

#define PUT_LINE(string) (void)fprintf( stderr, "%s\n", string )
#define PUT_CHAR(symbol) (void)fputc( symbol, stderr )
#define PUT_TEXT(format) (void)fprintf( stderr, format

static const wchar_t* const empty_wstr = L"";
#define WSNN(string) ( (string) != NULL ? (string) : empty_wstr )

static plainmtp_bool seek_object( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  const wchar_t* name, size_t length
) {
  const plainmtp_image_s* const image = (plainmtp_image_s*)cursor;
{
  while (plainmtp_cursor_select( cursor, device )) {
    if (image->name == NULL) { continue; }

    /* BEWARE: Short-circuit evaluation matters here! */
    /* NB: wcsncmp() must be checked first to guarantee minimum length of the string. */
    if ( (wcsncmp( name, image->name, length ) == 0) && (image->name[length] == L'\0') ) {
      return !plainmtp_cursor_select( cursor, NULL );
    }
  }

  return PLAINMTP_FALSE;
}}

/* NB: Both PTP and MTP technically allow empty and/or even duplicate filenames. */
static wchar_t* adjust_cursor( plainmtp_cursor_s* cursor, plainmtp_device_s* device, wchar_t* path,
  size_t* OUT_filename_length
) {
  size_t length = 0;
{
  while (path[length] != L'\0') {
    if (path[length] == PATH_DELIMITER) {
      if (!seek_object( cursor, device, path, length )) { return NULL; }
      path += length + 1;
      length = 0;
    } else {
      ++length;
    }
  }

  if (OUT_filename_length == NULL) {
    if (!seek_object( cursor, device, path, length )) { return NULL; }
  } else {
    *OUT_filename_length = length;
  }

  return path;
}}

static int command_enumerate( plainmtp_context_s* context ) {
  size_t i;
  const plainmtp_registry_s* const registry = (plainmtp_registry_s*)context;
{
  PUT_TEXT( "Devices available: %lu\n"), (unsigned long)registry->count );
  for (i = 0; i < registry->count; ++i) {
    PUT_TEXT( "\n%lu\t"), (unsigned long)i );
    if (registry->names != NULL) { PUT_TEXT( "%-31ls "), WSNN(registry->names[i]) ); }
    if (registry->vendors != NULL) { PUT_TEXT( "%-31ls "), WSNN(registry->vendors[i]) ); }
    if (registry->strings != NULL) { PUT_TEXT( "%-31ls "), WSNN(registry->strings[i]) ); }
    if (registry->ids != NULL) { PUT_TEXT( "\n\t%ls\n"), WSNN(registry->ids[i]) ); }
  }

  PUT_CHAR('\n');
  return EXIT_SUCCESS;
}}

static int command_list( plainmtp_cursor_s* cursor, plainmtp_device_s* device ) {
  size_t count = 0;
  const plainmtp_image_s* const image = (plainmtp_image_s*)cursor;
{
  PUT_TEXT( "\n%ls\t: %ls\n\n"), WSNN(image->name), WSNN(image->id) );

  while ( plainmtp_cursor_select(cursor, device) ) {
    char strftime_result[] = "0000-00-00 00:00:00";  /* We need to refresh it every iteration. */

    if (image->datetime.tm_mday != 0) {
      /* NB: Format string "%F %T" is C99, so we had to write an equivalent one here. */
      (void)strftime( strftime_result, sizeof(strftime_result), "%Y-%m-%d %H:%M:%S",
        &image->datetime );
    }

    PUT_TEXT( "  %ls :\t%s\t%ls\n"), WSNN(image->id), strftime_result, WSNN(image->name) );
    ++count;
  }

  PUT_TEXT( "\nObjects total: %lu\n"), (unsigned long)count );
  if (plainmtp_cursor_select(cursor, NULL)) {
    PUT_LINE( "!!! An error occurred while enumerating the specified folder." );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}}

static void* CB_receive_file( void* chunk, size_t size, void* file ) {
{
  PUT_TEXT( "> chunk: %p\tfile: %p\tsize: %-10lu\r"), chunk, file, (unsigned long)size );

  if (size == 0) {
    PUT_LINE( "\n--" );
    free( chunk );
    return NULL;
  }

  if (chunk == NULL) {
    PUT_CHAR('\n');
    return malloc( size );
  }

# ifndef NDEBUG
  PUT_CHAR('\n');
# endif

  size = fwrite( chunk, size, 1, file );
  return (size != 0) ? chunk : NULL;
}}

static int command_receive( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  const wchar_t* output_file
) {
  plainmtp_bool result;
  FILE* output;
{
  output = _wfopen( output_file, L"wb" );
  if (output == NULL) {
    PUT_LINE( "failed to open the output file" );
    return EXIT_FAILURE;
  }

  result = plainmtp_cursor_receive( cursor, device, 0, &CB_receive_file, output );
  (void)fclose( output );
  PUT_CHAR('\n');

  if (!result) {
    PUT_LINE( "failed to receive the file" );
    return EXIT_FAILURE;
  }

  PUT_LINE( "file has been received successfully" );
  return EXIT_SUCCESS;
}}

static void* CB_transfer_file( void* chunk, size_t size, void* file ) {
  const plainmtp_bool initial_call = (chunk == NULL);
{
  PUT_TEXT( "> chunk: %p\tfile: %p\tsize: %-10lu\r"), chunk, file, (unsigned long)size );

  if (size == 0) {
    PUT_LINE( "\n--" );
    goto cleanup;
  }

  if (initial_call) {
    PUT_CHAR('\n');
    chunk = malloc( size );
    if (chunk == NULL) {
      PUT_LINE( "  failed to allocate chunk memory" );
      return NULL;
    }
  }
# ifndef NDEBUG
  else { PUT_CHAR('\n'); }
# endif

  size = fread( chunk, size, 1, file );
  if (size != 0) { return chunk; }

  /* We need this check to prevent double-free during the final call in case of fread() error. */
  if (initial_call) {
cleanup:
    free( chunk );
  }

  return NULL;
}}

static int command_transfer( plainmtp_cursor_s* cursor, plainmtp_device_s* device,
  const wchar_t* source_file, const wchar_t* object_name
) {
  plainmtp_bool result = PLAINMTP_FALSE;
  FILE* source;
  int64_t size;
  const plainmtp_image_s* const image = (plainmtp_image_s*)cursor;
{
  source = _wfopen( source_file, L"rb" );
  if (source == NULL) {
    PUT_LINE( "failed to open the file specified to transfer" );
    return EXIT_FAILURE;
  }

  if ( _fseeki64(source, 0, SEEK_END) != 0 ) { goto size_error; }
  size = _ftelli64( source );
  if ( size < 0 ) { goto size_error; }

  if ( _fseeki64(source, 0, SEEK_SET) == 0 ) {
    result = plainmtp_cursor_transfer( cursor, device, object_name, size, 0, &CB_transfer_file,
      source, &cursor );
  } else {
size_error:
    PUT_LINE( "failed to get size of the file specified to transfer" );
  }

  (void)fclose( source );
  PUT_CHAR('\n');

  if (!result) {
    PUT_LINE( "failed to transfer the file" );
    return EXIT_FAILURE;
  }

  PUT_LINE( "file has been transferred successfully" );
  if (cursor != NULL) {
    PUT_TEXT( "  %ls :\t%ls\n"), WSNN(image->id), WSNN(image->name) );
  } else {
    PUT_LINE( "!!! An error occurred while switching the cursor" );
  }

  return EXIT_SUCCESS;
}}

/* We use wmain() to allow specifying filesystem paths with mixed languages. */
/* TODO: Use proper argument parsing. Why there's no such libraries with wchar_t support?.. */
int wmain( int argc, wchar_t* argv[] ) {
  int exit_code = EXIT_FAILURE;
  plainmtp_context_s* context = NULL;
  plainmtp_device_s* device = NULL;
  plainmtp_cursor_s* cursor = NULL;
  wchar_t command;
  unsigned int device_index;
  int limit, consumed;
  plainmtp_bool read_only;
  wchar_t *base_object_id, *machine_path, *path_end;
  size_t filename_length;
{
  /* This makes printf() et al. understand what is A Wide String at least on Windows. Fuck it. */
  (void)setlocale( LC_ALL, "" );

# define ASSERT_CLEANUP( condition, message ) \
  if (condition) do { \
    PUT_LINE( "ERROR: " message ); \
    goto cleanup; \
  } while(0)

  /* NB: Keep in mind the C90 string limit of 509 characters here. */
  ASSERT_CLEANUP( --argc <= 0, "no command given"
    "\nusage: mtpls COMMAND ARGUMENTS\n"
    "\nlist of available command lines, with their arguments:\n"
    "\n  e\t- enumerate all available compatible devices\n"
    "\n  l DEVICE_INDEX{:BASE_OBJECT_ID} {DEVICE_PATH}\t- list directory\n"
    "\n  r DEVICE_INDEX{:BASE_OBJECT_ID} {DEVICE_PATH} MACHINE_PATH\t- receive file\n"
    "\n  t DEVICE_INDEX{:BASE_OBJECT_ID} DEVICE_PATH MACHINE_PATH\t- transfer file\n"
    "\narguments enclosed in {} are optional"
    "\nbe careful if your BASE_OBJECT_ID contains spaces"
  );

  context = plainmtp_startup();
  ASSERT_CLEANUP( context == NULL, "failed to initialize plainmtp context" );

  command = argv[1][0];
  switch (command) {
    case COMMAND_LIST:
      limit = 2;
      read_only = PLAINMTP_TRUE;
    break;

    case COMMAND_RECEIVE:
      limit = 3;
      read_only = PLAINMTP_TRUE;
    break;

    case COMMAND_TRANSFER:
      limit = 3;  /* The presence of DEVICE_PATH is checked separately, see the next 'switch'. */
      read_only = PLAINMTP_FALSE;
    break;

    case COMMAND_ENUMERATE:
      exit_code = command_enumerate( context );
      goto cleanup;

    default:
      PUT_LINE( "unknown command" );
      goto cleanup;
  }

  ASSERT_CLEANUP( argc < limit, "not enough arguments" );
  ASSERT_CLEANUP( swscanf(argv[2], L"%u%n", &device_index, &consumed) < 1, "invalid id" );
  ASSERT_CLEANUP( device_index >= ((plainmtp_registry_s*)context)->count, "illegal id" );

  argv[2] += consumed;
  base_object_id = (*argv[2] == L':') ? (argv[2]+1) : NULL;
  machine_path = argv[argc];

  device = plainmtp_device_start( context, device_index, read_only );
  ASSERT_CLEANUP( device == NULL, "failed to establish device connection" );

  cursor = plainmtp_cursor_switch( NULL, base_object_id, device );
  ASSERT_CLEANUP( cursor == NULL, "failed to create object cursor, check object ID if specified" );

  if (argc > limit) {
    path_end = adjust_cursor( cursor, device, argv[3],
      (command != COMMAND_TRANSFER) ? NULL : &filename_length );

    if (path_end == NULL) {
      PUT_TEXT( "ERROR: failed to resolve path after `%ls`\n"),
        WSNN(((const plainmtp_image_s*)cursor)->name) );
      goto cleanup;
    }
  } else {
    path_end = NULL;
  }

  switch (command) {
    case COMMAND_LIST:
      exit_code = command_list( cursor, device );
    break;

    case COMMAND_RECEIVE:
      exit_code = command_receive( cursor, device, machine_path );
    break;

    case COMMAND_TRANSFER:
      ASSERT_CLEANUP( path_end == NULL, "DEVICE_PATH not specified (required for filename)" );
      exit_code = command_transfer( cursor, device, machine_path, path_end );
    break;
  }

cleanup:
  if (cursor != NULL) { plainmtp_cursor_assign( cursor, NULL ); }
  if (device != NULL) { plainmtp_device_finish( device ); }
  if (context != NULL) { plainmtp_shutdown( context ); }

  return exit_code;
# undef ASSERT_CLEANUP
}}
