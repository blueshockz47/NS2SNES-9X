// SW2SNES: Snes9x adapter (core table), run-ahead and rewind history.
#include "core.h"
#include "rewind_delta.h"
#include "core_opt.h"

#include <stdlib.h>
#include <string.h>
#include <deque>
#include <vector>

extern "C" {
const char* snes_load(const uint8_t*, long, const char*);
void snes_unload();
void snes_reset(int);
int snes_frame(int, int, int);
void snes_set_audio(int, int);
void snes_set_audio_ratio(double);
int snes_audio_take();
int16_t* snes_audio();
void snes_audio_reset();
void snes_set_audio_discard(int);
uint32_t* snes_video();
int snes_video_pitch();
int snes_video_width();
int snes_video_height();
int snes_has_battery();
uint8_t* snes_sram();
int snes_sram_size();
long snes_state_size();
long snes_state_save();
char* snes_state_buffer(long);
int snes_state_load(long);
int snes_freeze(uint8_t*, long);
int snes_unfreeze(const uint8_t*, long);
int snes_set_cheats(const char**, int);
int snes_cheat_valid(const char*);
int snes_is_pal();
const char* snes_chips();
}

const char* core_rom_name = "";
static std::vector<uint8_t> rom;
static uint8_t* romAlloc(long n) { rom.assign(n, 0); return rom.data(); }
static const char* load() { return snes_load(rom.data(), (long)rom.size(), core_rom_name); }
static void noop() {}

// Run-ahead in one instance: Snes9x keeps all state in globals, so the real
// frame is saved, `n` frames run ahead with audio discarded (the last one
// drawn), and the saved state restored. Host tests show the audio stays
// bit-identical to plain play.
static std::vector<uint8_t> raState;
static int runahead(int p1, int p2, int n) {
  long sz = snes_state_size();
  if (sz <= 0 || n <= 0) return -1;
  if ((long)raState.size() != sz) raState.assign(sz, 0);
  if (!snes_freeze(raState.data(), sz)) return -1;
  snes_set_audio_discard(1);
  for (int i = 0; i < n; i++) snes_frame(p1, p2, i == n - 1 ? 1 : 0);
  snes_audio_take();
  snes_set_audio_discard(0);
  return snes_unfreeze(raState.data(), sz) ? 0 : -1;
}

const CoreApi SNES9X_CORE = {
    "Snes9x", "",
    romAlloc, snes_unload, load, noop, snes_reset, noop, snes_frame,
    snes_set_audio, snes_set_audio_ratio, snes_audio_take, snes_audio, snes_audio_reset, snes_set_audio_discard,
    snes_video, snes_video_pitch, snes_video_width, snes_video_height,
    snes_has_battery, snes_sram, snes_sram_size, snes_state_save, snes_state_buffer, snes_state_load,
    runahead, snes_set_cheats, snes_cheat_valid, snes_is_pal, snes_chips,
};

/* ---------------- Rewind history ----------------
 * A SNES save state is ~820 KB, so the history keeps the current state in
 * full and, for every step back, the XOR difference to the previous state,
 * run-length encoded (most of memory does not change between frames: a few
 * KB per frame). Stepping back XORs the newest difference into the current
 * state. The oldest steps are dropped to stay within the byte budget. */
static std::vector<uint8_t> cur, scratch;
static std::deque<std::vector<uint8_t>> deltas;
static long budget = 0, used = 0;

// RLE of an XOR difference: [zero run u32][literal count u32][literal bytes]...
static void encode(const uint8_t* a, const uint8_t* b, size_t n, std::vector<uint8_t>& out) {
  out.clear();
  size_t i = 0;
  auto put32 = [&](uint32_t v) { out.insert(out.end(), (uint8_t*)&v, (uint8_t*)&v + 4); };
  while (i < n) {
    size_t z = i + core_opt::equalPrefix(a+i,b+i,n-i);
    size_t l = z;
    while (l < n && (a[l] != b[l] || (l + 1 < n && a[l + 1] != b[l + 1]) || (l + 2 < n && a[l + 2] != b[l + 2]))) l++;  // literal runs absorb short gaps
    put32((uint32_t)(z - i)); put32((uint32_t)(l - z));
    size_t offset = out.size();
    out.resize(offset + l - z);
    for (size_t k = z; k < l; k++) out[offset + k - z] = a[k] ^ b[k];
    i = l;
  }
}
bool rewind_init(long budgetBytes) { budget = budgetBytes > 0 ? budgetBytes : 0; rewind_clear(); return true; }
void rewind_clear() { deltas.clear(); used = 0; cur.clear(); }
bool rewind_push() {
  long sz = snes_state_size();
  if (sz <= 0 || budget <= 0) return false;
  if ((long)scratch.size() != sz) scratch.assign(sz, 0);
  if (!snes_freeze(scratch.data(), sz)) return false;
  if ((long)cur.size() != sz) rewind_clear();
  if ((long)cur.size() == sz && budget > 0) {
    deltas.emplace_back();
    encode(scratch.data(), cur.data(), sz, deltas.back());
    used += (long)deltas.back().size();
    while (used > budget && !deltas.empty()) { used -= (long)deltas.front().size(); deltas.pop_front(); }
  }
  cur.swap(scratch);
  return true;
}
bool rewind_pop() {
  if (deltas.empty() || cur.empty()) return false;
  scratch = cur;
  if (!rewind_detail::applyDelta(scratch, deltas.back()) || !snes_unfreeze(scratch.data(), (long)scratch.size())) {
    snes_unfreeze(cur.data(), (long)cur.size());
    rewind_clear();
    return false;
  }
  cur.swap(scratch);
  used -= (long)deltas.back().size();
  deltas.pop_back();
  return true;
}
bool rewind_reload() { return !cur.empty() && snes_unfreeze(cur.data(), (long)cur.size()) != 0; }
int rewind_count() { return (int)deltas.size(); }
long rewind_bytes() { return used; }
