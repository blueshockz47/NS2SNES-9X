// Host test for the Snes9x bridge (run under AddressSanitizer):
//   build-host/core_test <rom> [frames]
// Loads a ROM, plays scripted input, then checks picture/audio output, save
// state determinism, single-instance run-ahead audio continuity and cheats.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <chrono>
#include "../source/core.h"

extern "C" {
const char* snes_load(const uint8_t*, long, const char*);
int snes_frame(int, int, int);
int snes_is_pal();
const char* snes_chips();
int snes_audio_take();
int16_t* snes_audio();
uint32_t* snes_video();
int snes_video_width();
int snes_video_height();
long snes_state_size();
int snes_freeze(uint8_t*, long);
int snes_unfreeze(const uint8_t*, long);
void snes_set_audio_discard(int);
int snes_has_battery();
int snes_sram_size();
const char* snes_rom_title();
int snes_set_cheats(const char**, int);
void snes_audio_reset();
void snes_unload();
}

// Start pressed a few times to get past titles, then hold right with B pulses.
static int pad(int i) {
  int p = 0;
  if ((i >= 120 && i < 124) || (i >= 300 && i < 304) || (i >= 500 && i < 504)) p |= 1 << 12;
  if (i > 600) { p |= 1 << 8; if (i % 40 < 6) p |= 1 << 15; }
  return p;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: core_test rom [frames]\n"); return 2; }
  int frames = argc > 2 ? atoi(argv[2]) : 900;
  FILE* f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open\n"); return 1; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if(n<0x8000||n>12*1024*1024||frames<=0){fclose(f);return 1;}
  std::vector<uint8_t> d(n);
  if (fread(d.data(), 1, n, f) != (size_t)n) return 1;
  fclose(f);
  const char* e = snes_load(d.data(), n, argv[1]);
  if (e) { printf("LOAD ERROR: %s\n", e); return 1; }
  if(const char* region=getenv("EXPECT_PAL")){if(snes_is_pal()!=atoi(region))return 1;}
  if(const char* chip=getenv("EXPECT_CHIP")){if(!strstr(snes_chips(),chip))return 1;}
  if(getenv("SMOKE_ONLY")){for(int i=0;i<60;i++){if(snes_frame(0,0,1)<0)return 1;snes_audio_take();}long size=snes_state_size();if(size<=0||size>16*1024*1024)return 1;std::vector<uint8_t> state(size);if(!snes_freeze(state.data(),size))return 1;for(int i=0;i<64;i++){if(!snes_unfreeze(state.data(),size)||snes_frame(0,0,1)<0)return 1;snes_audio_take();}snes_unload();return 0;}
  printf("title '%s'  battery %d (%d bytes)  state %ld bytes\n", snes_rom_title(), snes_has_battery(), snes_sram_size(), snes_state_size());
  fflush(stdout);
  long audio = 0;
  for (int i = 0; i < frames; i++) {
    snes_frame(pad(i), 0, 1);
    audio += snes_audio_take();
    if (i % 300 == 299) { printf("  frame %d ok\n", i + 1); fflush(stdout); }
  }
  int w = snes_video_width(), h = snes_video_height();
  uint32_t hash = 2166136261u; for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) hash = (hash ^ snes_video()[y * 512 + x]) * 16777619u;
  if (const char* dump = getenv("DUMP")) {  // last frame as a PNG-ready raw RGBA file: <w> <h> header line + pixels
    FILE* o = fopen(dump, "wb");
    if (o) { fprintf(o, "%d %d\n", w, h); for (int y = 0; y < h; y++) fwrite(snes_video() + y * 512, 4, w, o); fclose(o); }
  }
  printf("%d frames: picture %dx%d (hash %08x), audio %.1f samples/frame (%.0f Hz)\n", frames, w, h, hash, audio / (double)frames, audio / (double)frames * 60.0988);

  long sz = snes_state_size();
  if(sz<=0||sz>16*1024*1024)return 1;
  std::vector<uint8_t> st(sz), ra(sz);
  if(!snes_freeze(st.data(), sz))return 1;
  std::vector<int16_t> b1, b2, b3;
  auto run = [&](std::vector<int16_t>& out, bool ahead) {
    snes_audio_reset();  // same resampler start for every comparison run
    for (int i = frames; i < frames + 200; i++) {
      snes_frame(pad(i), 0, ahead ? 0 : 1);
      int k = snes_audio_take(); out.insert(out.end(), snes_audio(), snes_audio() + k * 2);
      if (ahead) {
        snes_freeze(ra.data(), sz);
        snes_set_audio_discard(1); snes_frame(pad(i), 0, 1); snes_audio_take(); snes_set_audio_discard(0);
        snes_unfreeze(ra.data(), sz);
      }
    }
  };
  run(b1, false);
  std::vector<uint32_t> p1(snes_video(), snes_video() + 512 * 478);
  snes_unfreeze(st.data(), sz); run(b2, false);
  bool replayOK=memcmp(p1.data(),snes_video(),512*478*4)==0&&b1==b2;
  printf("state replay: picture %s, audio %s\n", memcmp(p1.data(), snes_video(), 512 * 478 * 4) ? "DIFFERS" : "identical", b1 == b2 ? "identical" : "DIFFERS");
  snes_unfreeze(st.data(), sz); run(b3, true);
  size_t m = b1.size() < b3.size() ? b1.size() : b3.size(), diff = 0;
  for (size_t i = 0; i < m; i++) if (b1[i] != b3[i]) diff++;
  printf("run-ahead audio vs plain: %zu vs %zu samples, %zu differ\n", b1.size(), b3.size(), diff);
  const char* codes[] = {"DD32-6DAD", "7E0019:01"};
  printf("cheats accepted %d/2\n", snes_set_cheats(codes, 2));
  // Rewind history: push 600 frames, keep exact copies of some, step back and compare.
  rewind_init(256L * 1024 * 1024);
  std::vector<std::vector<uint8_t>> truth;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 600; i++) {
    snes_frame(pad(frames + 200 + i), 0, 1); snes_audio_take();
    rewind_push();
    std::vector<uint8_t> s2(sz); snes_freeze(s2.data(), sz); truth.push_back(s2);
  }
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  printf("rewind: %d steps, %.1f KB per step on average (%.2f ms per frame incl. test copies)\n", rewind_count(), rewind_bytes() / 1024.0 / rewind_count(), ms / 600);
  int ok = 0, checked = 0;
  for (int k = 598; k >= 0; k--) {
    if (!rewind_pop()) break;
    std::vector<uint8_t> s3(sz); snes_freeze(s3.data(), sz);
    checked++; if (s3 == truth[k]) ok++;
  }
  printf("rewind restore exact: %d/%d\n", ok, checked);
  snes_unload();
  printf("done\n");
  return replayOK && b1.size()==b3.size() && diff==0 && checked>0 && ok==checked ? 0 : 1;
}
