// SW2SNES: the Snes9x core (native/bridge_snes9x.cpp, snes_*) behind the same
// kind of function table the SW2NES front end used, so the front end keeps
// its structure. See cores.cpp for run-ahead and the rewind history.
#pragma once
#include <stdint.h>

struct CoreApi {
  const char* name;
  const char* stateTag;  // save-state file tag
  uint8_t* (*rom_alloc)(long);
  void (*unload)();
  const char* (*load)();  // the ROM in rom_alloc's buffer; rom_name set first
  void (*power)();
  void (*reset)(int);
  void (*reconvert)();
  int (*frame)(int, int, int);
  void (*set_audio)(int, int);
  void (*set_audio_ratio)(double);
  int (*audio_take)();
  int16_t* (*audio)();
  void (*audio_reset)();
  void (*set_audio_discard)(int);
  uint32_t* (*video)();     // RGBA, video_pitch() pixels per row
  int (*video_pitch)();
  int (*width)();           // size of the last frame (256/512 x 224/239/448/478)
  int (*height)();
  int (*has_battery)();
  uint8_t* (*sram)();
  int (*sram_size)();
  long (*state_save)();
  char* (*state_buffer)(long);
  int (*state_load)(long);
  int (*runahead)(int, int, int);
  int (*set_cheats)(const char** codes, int n);
  int (*cheat_valid)(const char* code);
  int (*is_pal)();
  const char* (*chips)();
};
extern const CoreApi SNES9X_CORE;
extern const char* core_rom_name;  // file name, for the core's own heuristics

// Rewind history (delta-compressed states; see cores.cpp).
bool rewind_init(long budgetBytes);
void rewind_clear();
bool rewind_push();      // saves the current state
bool rewind_pop();       // steps back one state and loads it
bool rewind_reload();    // loads the current state again (after drawing it)
int rewind_count();
long rewind_bytes();
