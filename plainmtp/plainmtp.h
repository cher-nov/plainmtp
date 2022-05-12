#ifndef ZZ_PLAINMTP_MAIN_H_IG
#define ZZ_PLAINMTP_MAIN_H_IG

/* TODO: Check for C95 / wchar_t support properly, because __STDC_VERSION__ is undefined on MSVC.
  https://en.wikipedia.org/wiki/ANSI_C#C95
  https://docs.microsoft.com/en-us/cpp/preprocessor/predefined-macros

#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199409L)
  #error __STDC_VERSION__ This API requires C95 language compatibility.
#endif
*/

#include <stddef.h>
#include <time.h>

/* Prior to C99, there were no 64-bit integer types, so their support in C95 is compiler-dependent.
  Official website for this library: http://www.azillionmonkeys.com/qed/ninja.html */
#include "../3rdparty/pstdint.h"

#define PLAINMTP_VERSION_MAJOR 0
#define PLAINMTP_VERSION_MINOR 1
#define PLAINMTP_VERSION_BUILD 0

typedef int plainmtp_bool;
#define PLAINMTP_FALSE (0)
#define PLAINMTP_TRUE (!PLAINMTP_FALSE)

/* This is the prototype of a callback function user should use to transfer or receive data. The
  callback model was chosen over the contextual one due to the fact that PTP/MTP can perform only
  one operation at a time and don't allow to receive / transfer multiple files simultaneously. */
typedef void* (*plainmtp_data_f)
(
  /* A pointer that was returned by the previous call of this function; NULL at the first one. */
  void* prior_chunk,

  /* A size of the data chunk that buffer already contains (on receive) or that is requested (on
    transfer). Will never exceed the value passed at the first call; contains 0 at the last one. */
  size_t size,

  /* An arbitrary user's pointer that was passed to the corresponding API function. */
  void* custom_state
);  /*
  Returns pointer to buffer for the next data chunk (on receive), or pointer to the requested data
  (on transfer). If this was the last call (the operation has finished or an error has occurred),
  returns NULL.
*/

/* Library context. Can be typecast to (const plainmtp_registry_s*) type to access information
  about available devices, connected to the machine. */
typedef struct zz_plainmtp_context_s plainmtp_context_s;

/* Device handle. */
typedef struct zz_plainmtp_device_s plainmtp_device_s;

/* Cursor. This is the main primitive to access objects on the device and perform operations on
  them (enumerate child objects, create new ones, read their binary data etc). Points to the only
  one specific object at a time. Can be typecast to (const plainmtp_image_s*) type to access
  information about it. There's never a cursor in invalid state, so any operation on it is either
  success or failure. But they're not being updated automatically, so it's possible to obtain a
  cursor that will point to a non-existent object (e.g. if it was later deleted by someone). */
typedef struct zz_plainmtp_cursor_s plainmtp_cursor_s;

typedef struct zz_plainmtp_registry_s {
  size_t count;

  /* BEWARE: All the next members AND their fields may be NULL! */
  wchar_t** ids;
  wchar_t** names;
  wchar_t** vendors;
  wchar_t** strings;
} plainmtp_registry_s;

typedef struct zz_plainmtp_image_s {
  /* Object's unique ID that persists between connection sessions. Can be NULL if not supported. */
  /* IMPORTANT: This is NEITHER a Persistent Unique Object Identifier (PUID) from the original MTP
    standard NOR guaranteed to be represented in the same GUID format as in PUID. */
  wchar_t* id;

  /* Either a file name or any other descriptive string that can be used as such. Can be NULL. */
  wchar_t* name;

  /* Date/time in standard portable C format. If information is not available, contains zeros. */
  struct tm datetime;
} plainmtp_image_s;


#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the library and obtain the operating context. */
extern plainmtp_context_s* plainmtp_startup(void);  /*
  Returns a pointer to the allocated context. If initialization has failed, returns NULL.
*/

/* Finalize the library and dispose the operating context. */
extern void plainmtp_shutdown
(
  /* A pointer to the operating context that was allocated by plainmtp_startup(). */
  plainmtp_context_s* context
);

/* Establish a session with the device. */
extern plainmtp_device_s* plainmtp_device_start
(
  /* A pointer to the operating context. */
  plainmtp_context_s* context,

  /* Index of the device in the context registry. */
  size_t device_index,

  /* Flag to request read-only (True) or read-write (False) access. */
  plainmtp_bool read_only
);  /*
  Returns a pointer to the allocated device handle. If session wasn't opened, returns NULL.
*/

/* Close a session with the device and release resources acquired for the device handle. */
extern void plainmtp_device_finish
(
  /* A pointer to the device handle that was obtained through plainmtp_device_start(). */
  plainmtp_device_s* device
);

/* Set cursor to object specified by another one. */
extern plainmtp_cursor_s* plainmtp_cursor_assign
(
  /* Cursor to be changed. If NULL, it creates a copy of 'source'. */
  plainmtp_cursor_s* cursor,

  /* Cursor to be assigned. If NULL, it frees the cursor. */
  plainmtp_cursor_s* source
);  /*
  Returns 'cursor' if it was changed; or a pointer to the created cursor.
  If an error has occurred or cursor was freed, returns NULL. A cursor retains its state on error.
*/

/* Set cursor to object by its persistent unique ID. */
extern plainmtp_cursor_s* plainmtp_cursor_switch
(
  /* Cursor to be changed. If NULL, a new one will be created. */
  plainmtp_cursor_s* cursor,

  /* Persistent unique ID of the object. */
  const wchar_t* object_id,

  /* Handle of the device the object belongs to. */
  plainmtp_device_s* device
);  /*
  Returns 'cursor' if it was changed; or a pointer to the created cursor.
  If an error has occurred or cursor was freed, returns NULL. A cursor retains its state on error.
*/

/* Updates the cursor information about the object that is accessible by user through typecasting
  cursor to (const plainmtp_image_s*) type. If there's an enumeration in progress, it will be
  finished if function succeeds. */
extern plainmtp_bool plainmtp_cursor_update
(
  /* Cursor to be updated. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the object belongs to. */
  plainmtp_device_s* device
);  /*
  Returns True if information has been updated successfully, False otherwise (e.g. if object wasn't
  found because it has been deleted).
*/

/* Switches cursor to the object's parent. */
extern plainmtp_bool plainmtp_cursor_return
(
  /* Cursor to be switched. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the object belongs to. */
  plainmtp_device_s* device
);  /*
  If 'device' is not NULL, returns True if cursor was switched successfully, False otherwise.
  If there's an enumeration in progress, it will be aborted. Switching is guaranteed in this case.
  If cursor points to the device's root object, does nothing and returns True.

  If 'device' is NULL, returns True if there's an enumeration in progress, False otherwise.
*/

/* Enumerate child objects one-by-one. On the first call this shadows the current object and
  switches to its first child. All the subsequent calls switch the cursor to the next child. If
  there's no more child objects to enumerate, or an error has occurred, switches back to the
  initially shadowed object. */
extern plainmtp_bool plainmtp_cursor_select
(
  /* Cursor that is being enumerated. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the object belongs to. */
  plainmtp_device_s* device
);  /*
  If 'device' is not NULL, returns True if cursor was switched successfully to the next child.
  If there's no more child objects to enumerate, or an error has occurred, it switches back to the
  shadowed object and returns False. This allows enumeration with a simple 'while' loop.

  If 'device' is NULL, returns True if the last enumerating (i.e. with 'device' as not NULL) call
  of this function has ended with an error, or the current object was never enumerated at all.
  Otherwise, returns False. If there's still an enumeration in progress, it will be aborted, but
  retaining the current object (contrary to plainmtp_cursor_return() that resets cursor to the
  shadowed object), which is an operation that is always guaranteed to succeed.
*/

/* Receive the data of the object pointed to by the cursor. */
extern plainmtp_bool plainmtp_cursor_receive
(
  /* Cursor that points to the object to be received. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the object belongs to. */
  plainmtp_device_s* device,

  /* Maximum size of the data chunk. If 0, there's no limit. */
  size_t chunk_limit,

  /* Callback function to receive the data. See plainmtp_data_f description for details. */
  plainmtp_data_f callback,

  /* An arbitrary user's pointer that will be passed to callback unchanged. */
  void* custom_state
);  /*
  Returns True if object has been received successfully, False otherwise.
*/

/* Transfer data as the new child object. */
extern plainmtp_bool plainmtp_cursor_transfer
(
  /* Cursor that points to the object to be the parent of a new one. */
  plainmtp_cursor_s* parent,

  /* Handle of the device the parent object belongs to. */
  plainmtp_device_s* device,

  /* Name for the new object. */
  const wchar_t* name,

  /* Size of the data to be transferred. */
  uint64_t size,

  /* Maximum size of the data chunk. If 0, there's no limit. */
  size_t chunk_limit,

  /* Callback function to transfer the data. See plainmtp_data_f description for details. */
  plainmtp_data_f callback,

  /* An arbitrary user's pointer that will be passed to callback unchanged. */
  void* custom_state,

  /* A pointer to the cursor that will be set to the new object. If the underlying value is NULL,
    the function will make a new cursor. Can be NULL to not get any cursor whatsoever. */
  plainmtp_cursor_s** SET_result
);  /*
  Returns True if object has been created and data transferred successfully, False otherwise.
  If 'SET_result' is not NULL, but it failed to set the cursor after transferring the data, then
  the underlying value will be NULL.
*/

#ifdef __cplusplus
}
#endif

#endif /* ZZ_PLAINMTP_MAIN_H_IG */
