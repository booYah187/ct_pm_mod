#ifndef CT_RESOURCE_OVERLAY_H
#define CT_RESOURCE_OVERLAY_H

/*

* resource_overlay.h
*
* Resource overlays contained inside the SAME assets/resources.bin archive.
*
* Config:
*
* face 1
* ui 1
* music 2
* font 0
*
* Each option name maps directly to an options directory:
*
* options/face/1/
* options/ui/1/
* options/music/2/
* options/font/0/
*
* Every file underneath that directory can override the corresponding
* original resource.
*
* For example:
*
* Original:
*
* 
    Extension/chricon.png
  
*
* Config:
*
* 
    ui 1
  
*
* Overlay:
*
* 
    options/ui/1/Extension/chricon.png
  
*
* Likewise:
*
* 
    Game/battle/obj_win_btl_c_1.png
  
*
* becomes:
*
* 
    options/ui/1/Game/battle/obj_win_btl_c_1.png
  
*
* if "ui 1" is configured.
*
* Multiple options may be configured simultaneously. They are tried in
* config order. The first configured option whose overlay resource exists
* is used. If none of the configured options contains the requested
* resource, the normal resource path is used.
*
* Overlay paths are constructed using the actual libc++ basic_string
* constructor from libchrono.so, rather than manually fabricating the
* std::string representation.
  */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "config.h"
#include "so_util.h"

extern struct so_module game_mod;

/*

* ctr::ResourceManager::getData(
* 
  std::__ndk1::basic_string<char> const&,
  
* 
  int*
  
* )
  */
#define CT_RES_OVERLAY_GETDATA \
    "_ZN3ctr15ResourceManager7getDataERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcENS0_9allocatorIcEEEEPi"
/*

* libchrono.so's libc++ basic_string(const char *) constructor.
*
* The constructor at 0x557f10 is:
*
* std::__ndk1::basic_string<char>::basic_string(const char *)
*
* It creates the exact string representation expected by the game's
* ResourceManager.
  */
  #define CT_RES_OVERLAY_STRING_CTOR_OFFSET 0x557f10

/*

* GOT entry containing the runtime address of:
*
* std::__ndk1::basic_string<char>::~basic_string()
*
* The relocation table identifies this entry at 0xbc6b48.
  */
  #define CT_RES_OVERLAY_STRING_DTOR_GOT 0xbc6b48

typedef void *(*CtGetDataFn)(
const void *path_string,
int *size
);

typedef void (*CtStringCtorFn)(
void *self,
const char *text
);

typedef void (*CtStringDtorFn)(
void *self
);

/*

* Trampoline storage.
  */
  static unsigned char ct_res_overlay_trampoline[128];

static CtGetDataFn ct_res_overlay_getdata_trampoline = NULL;

/*

* Construct a real libc++ std::string using the game's own constructor.
*
* A libc++ basic_string object in this binary is 24 bytes.
*
* Using uint64_t[3] guarantees that the object is 8-byte aligned, which
* matches the alignment expected by the AArch64/libc++ implementation.
*
* The constructor itself handles both short and long strings and allocates
* any required backing storage using the same allocator used by the game.
  */
  static int ct_res_overlay_make_string(
  uint64_t storage[3],
  const char *path)
  {
  CtStringCtorFn ctor;

  if (!storage || !path)
  return 0;

  memset(
  storage,
  0,
  sizeof(uint64_t) * 3);

  ctor =
  (CtStringCtorFn)(
  (uintptr_t)game_mod.load_virtbase +
  CT_RES_OVERLAY_STRING_CTOR_OFFSET);

  ctor(
  storage,
  path);

  return 1;
  }

/*

* Destroy a temporary libc++ std::string using the game's imported
* destructor.
*
* The destructor symbol itself is undefined in libchrono.so and is resolved
* through the GOT. The relocation table identifies its GOT entry at
* 0xbc6b48.
  */
  static void ct_res_overlay_destroy_string(
  uint64_t storage[3])
  {
  CtStringDtorFn dtor;
  uintptr_t got_address;
  uintptr_t dtor_address;

  if (!storage)
  return;

  got_address =
  (uintptr_t)game_mod.load_virtbase +
  CT_RES_OVERLAY_STRING_DTOR_GOT;

  dtor_address =
  *(uintptr_t *)got_address;

  if (!dtor_address)
  return;

  dtor =
  (CtStringDtorFn)dtor_address;

  dtor(storage);
  }

/*

* Decode the std::string passed by libchrono.so.
*
* We only read the object here; we do not modify or destroy it.
  */
  static int ct_res_overlay_fake_string_get(
  const void *string_object,
  char *out,
  size_t out_size)
  {
  const unsigned char *p;
  unsigned char tag;
  size_t length;
  const char *data;

  if (!string_object || !out || out_size == 0)
  return 0;

  p =
  (const unsigned char *)string_object;

  /*

  * This is the libc++ std::__ndk1::basic_string layout used by the
  * Android Chrono Trigger binary. This matches the code in
  * DetchmanResource::GetFileInfo() at 0x55eaac:
  *
  * byte 0:
  * 
      bit 0 == 0: short string
    
  * 
      bit 0 == 1: long string
    
  *
  * short:
  * 
      length = byte0 >> 1
    
  * 
      data   = object + 1
    
  *
  * long:
  * 
      length = *(uint64_t *)(object + 8)
    
  * 
      data   = *(char **)(object + 16)
    

  */
  tag =
  p[0];

  if (tag & 1) {
  uint64_t long_length;
  uintptr_t long_data;

  
   memcpy(
       &long_length,
       p + 8,
       sizeof(long_length));

   memcpy(
       &long_data,
       p + 16,
       sizeof(long_data));

   length =
       (size_t)long_length;

   data =
       (const char *)long_data;

   if (!data)
       return 0;
  

  } else {
  length =
  (size_t)(tag >> 1);

  
   data =
       (const char *)(p + 1);
  

  }

  if (length >= out_size)
  length =
  out_size - 1;

  if (length > 0)
  memcpy(
  out,
  data,
  length);

  out[length] =
  '\0';

  return 1;
  }

/*

* Return true if this is already an overlay path.
*
* This prevents:
*
* options/ui/1/Extension/foo.png
*
* from causing another overlay lookup.
  */
  static int ct_res_overlay_is_overlay_path(
  const char *path)
  {
  if (!path)
  return 0;

  return strncmp(
  path,
  "options/",
  8) == 0;
  }

/*

* Parse the next configured option from config.resource_overlays.
*
* The configuration is stored as newline-separated:
*
* face 1
* ui 1
* music 2
*
* On success:
*
* option_name = option name
* value       = configured value
* cursor      = advanced to the next line
*
* Returns 1 when another valid option was found, otherwise 0.
  */
  static int ct_res_overlay_next_option(
  const char **cursor,
  char *option_name,
  size_t option_name_size,
  int *value)
  {
  const char *p;

  if (!cursor ||
  !*cursor ||
  !option_name ||
  option_name_size == 0 ||
  !value)
  return 0;

  p =
  *cursor;

  while (*p) {
  int consumed;
  int parsed;
  size_t name_length;

  
   /*
    * Skip blank lines.
    */
   while (*p == '\n' || *p == '\r')
       p++;

   if (!*p)
       break;

   option_name[0] =
       '\0';

   *value =
       0;

   consumed =
       0;

   parsed =
       sscanf(
           p,
           "%127s %d%n",
           option_name,
           value,
           &consumed);

   /*
    * Always advance past this line, even if it wasn't a valid
    * option. This keeps malformed/unknown lines from trapping us.
    */
   while (*p && *p != '\n')
       p++;

   while (*p == '\n' || *p == '\r')
       p++;

   *cursor =
       p;

   if (parsed != 2 ||
       option_name[0] == '\0')
       continue;

   name_length =
       strlen(option_name);

   if (name_length >= option_name_size) {
       option_name[0] =
           '\0';

       continue;
   }

   return 1;
  

  }

  *cursor =
  p;

  return 0;
  }

/*

* Build:
*
* options/<option>/<number>/<original>
*
* Example:
*
* option   = ui
* value    = 1
* original = Extension/chricon.png
*
* produces:
*
* options/ui/1/Extension/chricon.png
  */
  static int ct_res_overlay_build_path(
  char *out,
  size_t out_size,
  const char *option,
  int value,
  const char *original)
  {
  int written;


if (!out ||



    out_size == 0 ||
    !option ||
    !*option ||
    !original ||
    !*original)
    return 0;

written =
    snprintf(
        out,
        out_size,
        "options/%s/%d/%s",
        option,
        value,
        original);

if (written < 0)
    return 0;

if ((size_t)written >= out_size)
    return 0;

return 1;


}

/*

* Call the original getData() with a replacement path.
*
* The replacement path is constructed as a genuine std::string using
* libchrono.so's own constructor. This means the ResourceManager receives
* exactly the same type/layout it receives from the rest of the game.
  */
  static void *ct_res_overlay_try_path(
  const char *path,
  int *size)
  {
  uint64_t string_object[3];
  void *result;

  if (!ct_res_overlay_getdata_trampoline)
  return NULL;

  if (!path)
  return NULL;

  if (!ct_res_overlay_make_string(
  string_object,
  path))
  return NULL;

  result =
  ct_res_overlay_getdata_trampoline(
  string_object,
  size);

/*  fprintf(
*  stderr,
*  "ct: resource overlay: lookup \"%s\" returned %p size=%d\n",
*  path,
*  result,
*  size ? *size : -1);
*/
  /*

  * The original getData() has returned by this point, so the temporary
  * path string is no longer needed.
    */
    ct_res_overlay_destroy_string(
    string_object);

  return result;
  }

/*

* ResourceManager::getData hook.
*
* For each requested resource:
*
* original = Extension/chricon.png
*
* and config:
*
* face 1
* ui 1
*
* try:
*
* options/face/1/Extension/chricon.png
* options/ui/1/Extension/chricon.png
*
* in config order.
*
* The first successful lookup wins.
*
* If no configured option contains the resource, the original path is
* passed through unchanged.
  */
  static void *ct_res_overlay_getdata_hook(
  const void *path_string,
  int *size)
  {
/*  fprintf(
*  stderr,
*  "ct: resource overlay: getData called path=%p size=%p\n",
*  path_string,
*  size);
*/
  char original_path[2048];
  char option_name[128];
  char overlay_path[4096];

  const char *config_cursor;

  int option_value;
  int original_size;

  void *data;

  if (!ct_res_overlay_getdata_trampoline)
  return NULL;

  /*

  * If the string cannot be decoded, just use the original.
    */
    if (!ct_res_overlay_fake_string_get(
    path_string,
    original_path,
    sizeof(original_path))) {

/*    fprintf(
*    stderr,
*    "ct: resource overlay: getData path decode FAILED\n");
*
*    return ct_res_overlay_getdata_trampoline(
*    path_string,
*    size);
*/
    }

/*  fprintf(
*  stderr,
*  "ct: resource overlay: path_object=%p path=\"%s\"\n",
*  path_string,
*  original_path);
*/
  /*

  * Never attempt to overlay an overlay request.
    */
    if (ct_res_overlay_is_overlay_path(
    original_path)) {

    return ct_res_overlay_getdata_trampoline(
    path_string,
    size);
    }

  /*

  * Save the original size before any overlay attempts.
    */
    original_size =
    0;

  if (size)
  original_size =
  *size;

  /*

  * Walk every configured option.
  *
  * The option name corresponds directly to the directory underneath
  * "options/".
    */
    config_cursor =
    config.resource_overlays;

  while (ct_res_overlay_next_option(
  &config_cursor,
  option_name,
  sizeof(option_name),
  &option_value)) {

  
   /*
    * Restore the original size before each independent lookup.
    */
   if (size)
       *size =
           original_size;

/*   fprintf(
*       stderr,
*       "ct: resource overlay: option \"%s\" = %d\n",
*       option_name,
*       option_value);
*/
   /*
    * Build:
    *
    *   options/<option>/<value>/<original>
    */
   if (!ct_res_overlay_build_path(
           overlay_path,
           sizeof(overlay_path),
           option_name,
           option_value,
           original_path)) {

       continue;
   }

/*   fprintf(
*       stderr,
*       "ct: resource overlay: trying \"%s\"\n",
*       overlay_path);
*
*/
   /*
    * IMPORTANT:
    *
    * This calls the ORIGINAL getData() implementation with the
    * alternate path. That means the normal ResourceManager ->
    * DetchmanResource -> resources.bin path is used.
    *
    * There is still only one resources.bin.
    */
   data =
       ct_res_overlay_try_path(
           overlay_path,
           size);

   if (data) {
       fprintf(
           stderr,
           "ct: resource overlay: %s -> %s\n",
           original_path,
           overlay_path);

       return data;
   }
  

  }

  /*

  * No configured option contained this resource.
  *
  * Restore the original size and perform the normal lookup.
    */
    if (size)
    *size =
    original_size;

  return ct_res_overlay_getdata_trampoline(
  path_string,
  size);
  }

/*

* Create a trampoline containing the first 16 bytes of getData()
* followed by an absolute branch back to getData()+16.
  */
  static uintptr_t ct_res_overlay_make_trampoline(
  uintptr_t target)
  {
  unsigned char *trampoline;
  uint32_t *instructions;
  uint64_t return_address;

  trampoline =
  ct_res_overlay_trampoline;

  /*

  * getData() first 16 bytes are safe to relocate.
    */
    memcpy(
    trampoline,
    (const void *)target,
    16);

  /*
  *:
  *

  * ldr x16, #8
  * br  x16
  * .quad target + 16
    */
    instructions =
    (uint32_t *)(trampoline + 16);

  instructions[0] =
  0x58000050u;

  instructions[1] =
  0xd61f0200u;

  return_address =
  (uint64_t)(target + 16);

  memcpy(
  trampoline + 24,
  &return_address,
  sizeof(return_address));

  /*

  * Make trampoline executable.
    */
    {
    long page_size;
    uintptr_t page;

    page_size =
    sysconf(_SC_PAGESIZE);

    if (page_size <= 0)
    page_size = 4096;

    page =
    (uintptr_t)trampoline &
    ~((uintptr_t)page_size - 1);

    mprotect(
    (void *)page,
    (size_t)page_size,
    PROT_READ |
    PROT_WRITE |
    PROT_EXEC);
    }

  __builtin___clear_cache(
  (char *)trampoline,
  (char *)(trampoline + 32));

  return (uintptr_t)trampoline;
  }

/*

* Patch an AArch64 function with:
*
* ldr x16, #8
* br  x16
* .quad hook
  */
  static int ct_res_overlay_patch(
  uintptr_t target,
  uintptr_t hook)
  {
  long page_size;
  uintptr_t page;
  uint32_t *instructions;
  uint64_t absolute_hook;


if (!target || !hook)



    return 0;

page_size =
    sysconf(_SC_PAGESIZE);

if (page_size <= 0)
    page_size = 4096;

page =
    target &
    ~((uintptr_t)page_size - 1);

if (mprotect(
        (void *)page,
        (size_t)page_size,
        PROT_READ |
        PROT_WRITE |
        PROT_EXEC) != 0) {

    fprintf(
        stderr,
        "ct: resource overlay: mprotect failed\n");

    return 0;
}

instructions =
    (uint32_t *)target;

instructions[0] =
    0x58000050u;

instructions[1] =
    0xd61f0200u;

absolute_hook =
    (uint64_t)hook;

memcpy(
    (void *)(target + 8),
    &absolute_hook,
    sizeof(absolute_hook));

__builtin___clear_cache(
    (char *)target,
    (char *)(target + 16));

return 1;


}

/*

* Install the ResourceManager::getData hook.
  */
  static int ct_res_overlay_install(void)
  {
  uintptr_t getdata;
  uintptr_t trampoline;

  /*

  * Nothing configured: don't patch anything.
    */
    if (!config.resource_overlays[0]) {
    fprintf(
    stderr,
    "ct: resource overlay: none configured\n");

    return 1;
    }

  /*

  * Try the exported symbol first.
    */
    getdata =
    (uintptr_t)so_try_find_addr_rx(
    &game_mod,
    CT_RES_OVERLAY_GETDATA);

  /*

  * Confirmed fallback from objdump:
  *
  * ctr::ResourceManager::getData = 0x5be02c
    */
    if (!getdata)
    getdata =
    (uintptr_t)game_mod.load_virtbase +
    0x5be02c;

  if (!getdata) {
  fprintf(
  stderr,
  "ct: resource overlay: getData not found\n");

  
   return 0;
  

  }

  /*

  * Build trampoline before modifying getData().
    */
    trampoline =
    ct_res_overlay_make_trampoline(
    getdata);

  if (!trampoline) {
  fprintf(
  stderr,
  "ct: resource overlay: trampoline creation failed\n");

  
   return 0;
  

  }

  ct_res_overlay_getdata_trampoline =
  (CtGetDataFn)trampoline;

  /*

  * Install hook.
    */
    if (!ct_res_overlay_patch(
    getdata,
    (uintptr_t)ct_res_overlay_getdata_hook)) {

    fprintf(
    stderr,
    "ct: resource overlay: getData patch failed\n");

    ct_res_overlay_getdata_trampoline =
    NULL;

    return 0;
    }

  fprintf(
  stderr,
  "ct: resource overlay: getData hook installed at %p\n",
  (void *)getdata);

  fprintf(
  stderr,
  "ct: resource overlay: configured:\n%s",
  config.resource_overlays);

  fprintf(
  stderr,
  "ct: resource overlay: active using assets/resources.bin\n");

  return 1;
  }

/*

* Compatibility name for the older main.c call.
*
* Keep both valid while we get the runtime behavior working.
  */
  static int ct_res_overlay_initialize(void)
  {
  return ct_res_overlay_install();
  }

#endif /* CT_RESOURCE_OVERLAY_H */
