// SW2SNES: the Snes9x core behind a small C interface (snes_*), and the
// platform hooks Snes9x expects from its front end (in place of the libretro
// frontend, libretro/libretro.cpp).
//
// Video: every frame is converted from RGB565 to RGBA into a 512 x 478 buffer
// (the largest SNES mode); snes_video_width/height give the frame's size.
// Audio: the SNES DSP's own output (stereo, ~32040 Hz) is resampled to 48 kHz
// with cubic interpolation, at a ratio the front end trims (audio rate control).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>

#include "../source/core_opt.h"
#include "snes9x.h"
#include "memmap.h"
#include "apu/apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "controls.h"
#include "cheats.h"
#include "display.h"
#include "ppu.h"
#include "cpuexec.h"
#include "fxemu.h"
#include "msu1.h"

#define EXPORT extern "C" __attribute__((used, visibility("default")))

enum { MAX_W = 512, MAX_H = 478, OUT_RATE = 48000, AUDIO_CAP = 8192 };

static bool inited = false, loaded = false, have_frame = false;
static uint32_t rgba[MAX_W * MAX_H];
static int frame_w = 256, frame_h = 224;
static bool render_frame = true;

static int16_t stereo[AUDIO_CAP * 2];
static int audio_fill = 0, audio_discard = 0, audio_vol = 256, audio_lp = 0;
static int lp_l = 0, lp_r = 0;
static double rs_pos = 0, rs_step = 32040.0 / OUT_RATE, rs_ratio = 1.0;
static int16_t hist[4][2];  // last four input frames for cubic interpolation

static std::string rom_name;
static uint8_t* state_buf = 0;
static long state_cap = 0;

/* ---------------- Platform hooks used by the Snes9x core ---------------- */
void S9xMessage(int, int, const char*) {}
void S9xSyncSpeed() {}
void S9xAutoSaveSRAM() {}
void S9xInitInputDevices() {}
const char* S9xStringInput(const char* in) { return in; }
const char* S9xBasename(const char* in) { return in; }
std::string S9xGetDirectory(s9x_getdirtype type) {
  if(type==ROMFILENAME_DIR){auto pos=rom_name.find_last_of("/");if(pos!=std::string::npos)return rom_name.substr(0,pos+1);}
  return ".";
}
bool8 S9xInitUpdate() { return TRUE; }
// End of a frame: convert the picture to RGBA.
bool8 S9xDeinitUpdate(int width, int height) {
  if (width > MAX_W) width = MAX_W;
  if (height > MAX_H) height = MAX_H;
  frame_w = width; frame_h = height;
  for (int y = 0; y < height; y++) {
    const uint16_t* in = (const uint16_t*)((const uint8_t*)GFX.Screen + (size_t)y * GFX.Pitch);
    uint32_t* out = rgba + y * MAX_W;
    core_opt::convert(in, out, width > 0 ? (size_t)width : 0);
  }
  have_frame = true;
  return TRUE;
}
bool8 S9xContinueUpdate(int width, int height) { return S9xDeinitUpdate(width, height); }

/* ---------------- Audio ---------------- */
static inline int16_t clamp16(float v) { return (int16_t)(v > 32767.f ? 32767 : v < -32768.f ? -32768 : v); }
// Catmull-Rom cubic between hist[1] and hist[2] at t (0..1).
static inline float cubic(float y0, float y1, float y2, float y3, float t) {
  return y1 + 0.5f * t * (y2 - y0 + t * (2.f * y0 - 5.f * y1 + 4.f * y2 - y3 + t * (3.f * (y1 - y2) + y3 - y0)));
}
static void audioFrom(const int16_t* in, int frames) {
  if (audio_discard) return;
  for (int i = 0; i < frames; i++) {
    int l = in[i * 2], r = in[i * 2 + 1];
    if (audio_vol != 256) { l = (l * audio_vol) >> 8; r = (r * audio_vol) >> 8; }
    if (audio_lp) { lp_l += (l - lp_l) >> audio_lp; lp_r += (r - lp_r) >> audio_lp; l = lp_l; r = lp_r; }
    memmove(hist[0], hist[1], sizeof(int16_t) * 6);
    hist[3][0] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
    hist[3][1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    // Output samples falling between hist[1] and hist[2].
    while (rs_pos < 1.0) {
      if (audio_fill >= AUDIO_CAP) { audio_fill = 0; }  // consumer stalled: drop backlog
      float t = (float)rs_pos;
      stereo[audio_fill * 2] = clamp16(cubic(hist[0][0], hist[1][0], hist[2][0], hist[3][0], t));
      stereo[audio_fill * 2 + 1] = clamp16(cubic(hist[0][1], hist[1][1], hist[2][1], hist[3][1], t));
      audio_fill++;
      rs_pos += rs_step;
    }
    rs_pos -= 1.0;
  }
}
static void updateStep() { rs_step = (S9xGetAudioSampleRate() > 0 ? (double)S9xGetAudioSampleRate() : 32040.0) / OUT_RATE / rs_ratio; }

/* ---------------- C interface ---------------- */
static bool initCore() {
  if (inited) return true;
  memset(&Settings, 0, sizeof(Settings));
  Settings.FrameTimePAL = 20000;
  Settings.FrameTimeNTSC = 16667;
  Settings.Stereo = TRUE;
  Settings.Transparency = TRUE;
  Settings.HDMATimingHack = 100;
  Settings.BlockInvalidVRAMAccessMaster = TRUE;
  Settings.SeparateEchoBuffer = FALSE;
  Settings.DontSaveOopsSnapshot = TRUE;
  for (int i = 0; i < 9; i++) Settings.ChannelsVolumePercent[i] = 100;
  // CPU timing and accuracy defaults (the libretro core's defaults, which its
  // option handler sets; without them the CPU clock never advances).
  Settings.OneClockCycle = 6;
  Settings.OneSlowClockCycle = 8;
  Settings.TwoClockCycles = 12;
  Settings.MaxSpriteTilesPerLine = 34;
  Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
  Settings.UpAndDown = FALSE;
  Settings.SuperFXClockMultiplier = 100;
  SuperFXSpeedPerLineHz = 6255000;  // 100%: the GSU speed snes9x2010's executor expects
  SuperFXHwTimingPct = 100;
  CPU.Flags = 0;
  if (!Memory.Init() || !S9xInitAPU()) return false;
  S9xInitSound(32);
  S9xSetSoundMute(FALSE);
  S9xSetSamplesAvailableCallback(NULL, NULL);
  S9xGraphicsInit();
  S9xInitInputDevices();
  S9xSetController(0, CTL_JOYPAD, 0, 0, 0, 0);
  S9xSetController(1, CTL_JOYPAD, 1, 0, 0, 0);
  S9xUnmapAllControls();
  S9xSuperFXRecomputeSpeedPerLine();
  inited = true;
  return true;
}

EXPORT const char* snes_load(const uint8_t* data, long size, const char* name) {
  if (!initCore()) return "The SNES core could not start (out of memory).";
  loaded = false;
  rom_name = name ? name : "";
  if (!data || size < 0x8000) return "This file is too small to be a SNES ROM.";
  if (!Memory.LoadROMMem(data, (uint32)size, rom_name.c_str())) return "This ROM could not be loaded. The file may be damaged, or it needs a special chip or BIOS that is not supported.";
  Memory.ClearSRAM();
  loaded = true; have_frame = false;
  audio_fill = 0; rs_pos = 0; memset(hist, 0, sizeof hist); lp_l = lp_r = 0;
  updateStep();
  return 0;
}
EXPORT void snes_unload() { S9xMSU1DeInit(); loaded = false; have_frame = false; S9xDeleteCheats(); }
EXPORT void snes_reset(int hard) { if (!loaded) return; if (hard) S9xReset(); else S9xSoftReset(); }

// One frame. pad bits: SNES_*_MASK (snes9x.h). render=0 skips drawing.
EXPORT int snes_frame(int pad1, int pad2, int render) {
  if (!loaded) return -1;
  S9xSetJoypadButtons(0, (uint16)pad1);
  S9xSetJoypadButtons(1, (uint16)pad2);
  IPPU.RenderThisFrame = render != 0;
  S9xMainLoop();
  int count = 0;
  const int16_t* src = S9xDrainAudio(&count);
  if (count > 0) audioFrom(src, count / 2);
  return 0;
}
EXPORT void snes_set_audio(int volume, int lowpass) {
  audio_vol = volume < 0 ? 0 : volume > 512 ? 512 : volume;
  audio_lp = lowpass < 0 ? 0 : lowpass > 3 ? 3 : lowpass;
}
EXPORT void snes_set_audio_ratio(double ratio) {
  if (ratio < 0.99) ratio = 0.99; if (ratio > 1.01) ratio = 1.01;
  rs_ratio = ratio; updateStep();
}
EXPORT void snes_set_audio_discard(int on) { audio_discard = on != 0; }
EXPORT int snes_audio_take() { int n = audio_fill; audio_fill = 0; return n; }
EXPORT int16_t* snes_audio() { return stereo; }
EXPORT void snes_audio_reset() { audio_fill = 0; rs_pos = 0; memset(hist, 0, sizeof hist); lp_l = lp_r = 0; }

EXPORT uint32_t* snes_video() { return rgba; }   // MAX_W pixels per row
EXPORT int snes_video_pitch() { return MAX_W; }
EXPORT int snes_video_width() { return frame_w; }
EXPORT int snes_video_height() { return frame_h; }
EXPORT int snes_has_frame() { return have_frame ? 1 : 0; }

EXPORT int snes_has_battery() { return loaded && Memory.SRAMSize ? 1 : 0; }
EXPORT uint8_t* snes_sram() { return Memory.SRAM; }
EXPORT int snes_sram_size() { return loaded && Memory.SRAMSize ? (1 << (Memory.SRAMSize + 3)) * 128 : 0; }

// Save states (uncompressed, in memory).
EXPORT long snes_state_size() { return loaded ? (long)S9xFreezeSize() : 0; }
EXPORT char* snes_state_buffer(long size) {
  if (size > state_cap) { free(state_buf); state_buf = (uint8_t*)malloc(size); state_cap = state_buf ? size : 0; }
  return (char*)state_buf;
}
EXPORT long snes_state_save() {
  long n = snes_state_size();
  if (!n || !snes_state_buffer(n)) return 0;
  return S9xFreezeGameMem(state_buf, (uint32)n) ? n : 0;
}
EXPORT int snes_state_load(long size) {
  if (!loaded || !state_buf || size <= 0 || size > state_cap) return 0;
  return S9xUnfreezeGameMem(state_buf, (uint32)size) == SUCCESS ? 1 : 0;
}
// Saves/loads a state to/from caller memory (rewind and run-ahead).
EXPORT int snes_freeze(uint8_t* buf, long size) { return loaded && S9xFreezeGameMem(buf, (uint32)size) ? 1 : 0; }
EXPORT int snes_unfreeze(const uint8_t* buf, long size) { return loaded && S9xUnfreezeGameMem(buf, (uint32)size) == SUCCESS ? 1 : 0; }

// Cheats: Game Genie ("XXXX-XXXX"), Pro Action Replay ("XXXXXXXX") or raw
// codes, decoded by Snes9x. Returns how many were accepted.
EXPORT int snes_set_cheats(const char** codes, int n) {
  if (!loaded) return 0;
  S9xDeleteCheats();
  int ok = 0;
  for (int i = 0; i < n; i++)
    if (S9xAddCheatGroup(std::string("c") + std::to_string(i), std::string(codes[i])) >= 0) { S9xEnableCheatGroup(Cheat.group.size() - 1); ok++; }
  S9xCheatsEnable();
  return ok;
}
EXPORT int snes_cheat_valid(const char* code) {
  // Validate by adding and removing again (Snes9x has no separate parser entry point).
  int before = (int)Cheat.group.size();
  int idx = S9xAddCheatGroup("check", code);
  if (idx < 0) return 0;
  S9xDeleteCheatGroup(idx);
  return (int)Cheat.group.size() == before;
}
EXPORT const char* snes_rom_title() { return loaded ? Memory.ROMName : ""; }

EXPORT int snes_is_pal() { return loaded && Settings.PAL ? 1 : 0; }

EXPORT const char* snes_chips(){static std::string s;s.clear();if(!loaded)return "None";auto add=[&](const char* name){if(!s.empty())s+=", ";s+=name;};if(Settings.MSU1)add("MSU-1");if(Settings.SuperFX)add("Super FX");if(Settings.SA1)add("SA-1");if(Settings.DSP)add("DSP");if(Settings.SDD1)add("S-DD1");return s.empty()?"None":s.c_str();}
