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

#include "global.h"

#define PLAINMTP_VERSION_MAJOR 0
#define PLAINMTP_VERSION_MINOR 1
#define PLAINMTP_VERSION_BUILD 0

/* This is the prototype of a callback function user should use to transfer or receive data. The
  callback model was chosen over the contextual one due to the fact that PTP/MTP can perform only
  one operation at a time and don't allow to receive / transfer multiple files simultaneously. */
typedef void* (*plainmtp_data_f)
(
  /* A pointer whose meaning depends on the mode in which the API function prefers to operate.

    "Active" mode:
    - the initial call passes NULL as this argument, thus requesting the exchange buffer;
    - subsequent calls pass the previous result as this argument if it is not NULL;
    - the final call passes the result of the initial one, allowing to release memory if necessary.

    "Passive" mode:
    - the initial and subsequent calls pass some library pointer to its own exchange buffer;
    - the final call passes NULL as this argument.

    A call with NULL as this argument and 'size' set to 0 should be treated as the final call, not
    the initial one. Note that when there is no data to exchange, the library may omit the initial
    call and only perform the final one, depending on the specific implementation. */
  void* data,

  /* The size of the data chunk that is already in the buffer or is being requested. In "active"
    mode, this parameter will never exceed the value of its argument passed in the initial call. A
    value of 0 indicates the final call, which is always made unless the initial call in "active"
    mode has failed to provide an exchange buffer and returned NULL. */
  size_t size,

  /* An arbitrary user's pointer that was passed to the corresponding library API function. */
  void* custom_state
);  /*
  Returns NULL if the operation completed or an error occurred. Otherwise, the result should be a
  pointer for the next call (in "active" mode) or any pointer that is not NULL (in "passive" mode).
*/

/* Library context. Can be typecast to (const plainmtp_registry_s*) type to access information
  about available devices, connected to the machine. */
typedef struct zz_plainmtp_context_s plainmtp_context_s;

/* Device handle. */
typedef struct zz_plainmtp_device_s plainmtp_device_s;

/* Cursor. This is the main primitive to access entities on the device and perform operations on
  them (enumerate child entities, create new ones, read their binary data etc). Points to the only
  one specific entity at a time. Can be typecast to (const plainmtp_image_s*) type to access
  information about it. There's never a cursor in invalid state, so any operation on it is either
  success or failure. But they're not being updated automatically, so it's possible to obtain a
  cursor that will point to a non-existent entity (e.g. if it was later deleted by someone). */
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
  /* Entity's unique ID that persists between connection sessions. Guaranteed not to be NULL.
    IMPORTANT: This is NEITHER a Persistent Unique Object Identifier (PUID) from the original MTP
    standard NOR guaranteed to be represented in the same GUID format as in PUID. */
  wchar_t* id;

  /* Either a file name or any other descriptive string that can be used as such. Can be NULL. */
  wchar_t* name;

  /* Date/time in standard portable C format. When not available, datetime.tm_mday is set to 0. */
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

/* Set cursor to entity specified by another one. */
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

/* Set cursor to entity by its persistent unique ID. */
extern plainmtp_cursor_s* plainmtp_cursor_switch
(
  /* Cursor to be changed. If NULL, a new one will be created. */
  plainmtp_cursor_s* cursor,

  /* Persistent unique ID of the entity. */
  const wchar_t* entity_id,

  /* Handle of the device the entity belongs to. */
  plainmtp_device_s* device
);  /*
  Returns 'cursor' if it was changed; or a pointer to the created cursor.
  If an error has occurred or cursor was freed, returns NULL. A cursor retains its state on error.
*/

/* Updates the cursor information about the entity that is accessible by user through typecasting
  cursor to (const plainmtp_image_s*) type. If there's an enumeration in progress, it will be
  finished if function succeeds. */
extern plainmtp_bool plainmtp_cursor_update
(
  /* Cursor to be updated. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the entity belongs to. */
  plainmtp_device_s* device
);  /*
  Returns True if information has been updated successfully, False otherwise (e.g. if entity wasn't
  found because it has been deleted).
*/

/* Switches cursor to the entity's parent. */
extern plainmtp_bool plainmtp_cursor_return
(
  /* Cursor to be switched. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the entity belongs to. */
  plainmtp_device_s* device
);  /*
  If 'device' is not NULL, returns True if cursor was switched successfully, False otherwise.
  If there's an enumeration in progress, it will be aborted. Switching is guaranteed in this case.
  If cursor points to the device's root, does nothing and returns True.

  If 'device' is NULL, returns True if there's an enumeration in progress, False otherwise.
*/

/* Enumerate child entities one-by-one. On the first call this shadows the current entity and
  switches to its first child. All the subsequent calls switch the cursor to the next child. If
  there's no more child entities to enumerate, or an error has occurred, switches back to the
  initially shadowed entity. */
extern plainmtp_bool plainmtp_cursor_select
(
  /* Cursor that is being enumerated. */
  plainmtp_cursor_s* cursor,

  /* Handle of the device the entity belongs to. */
  plainmtp_device_s* device
);  /*
  If 'device' is not NULL, returns True if cursor was switched successfully to the next child.
  If there's no more child entities to enumerate, or an error has occurred, it switches back to the
  shadowed entity and returns False. This allows enumeration with a simple 'while' loop.

  If 'device' is NULL, returns True if the last enumerating (i.e. with 'device' as not NULL) call
  of this function has ended with an error, or the current entity was never enumerated at all.
  Otherwise, returns False. If there's still an enumeration in progress, it will be aborted, but
  retaining the current entity (contrary to plainmtp_cursor_return() that resets cursor to the
  shadowed entity), which is an operation that is always guaranteed to succeed.
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
  /* Cursor that points to the entity to be the parent of a new one. */
  plainmtp_cursor_s* parent,

  /* Handle of the device the parent entity belongs to. */
  plainmtp_device_s* device,

  /* Name for the new object. */
  const wchar_t* name,

  /* Size of the data to be transferred. */
  uint64_t size,

  /* Maximum size of the data chunk. If 0, there's no limit. Note that this function DOESN'T clip
    it if it is greater than 'size', so you may want to do it manually to allocate less memory. */
  size_t chunk_limit,

  /* Callback function to transfer the data. Can be NULL only if 'size' is 0. See plainmtp_data_f
    description for details. */
  plainmtp_data_f callback,

  /* An arbitrary user's pointer that will be passed to callback unchanged. */
  void* custom_state,

  /* A pointer to the cursor that will be set to the new object. If the underlying value is NULL,
    the function will make a new cursor. Can be NULL to avoid getting any cursor whatsoever. */
  plainmtp_cursor_s** SET_cursor
);  /*
  Returns True if object has been created and data transferred successfully, False otherwise.
  If 'SET_cursor' is not NULL, but it failed to set the cursor after transferring the data, then
  the underlying value will be NULL.
*/

#ifdef __cplusplus
}
#endif

#endif /* ZZ_PLAINMTP_MAIN_H_IG */
