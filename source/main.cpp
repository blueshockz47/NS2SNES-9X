#include <cstdlib>
#include "branding_migration.h"
#include <cerrno>
#include "timing_policy.h"
#include "content_policy.h"
#include "rom_patch.h"
#include "save_policy.h"
#include "ui_theme.h"
#include "library_policy.h"
#include "cover_policy.h"
#include <unordered_set>
#include <unordered_map>
#include "input_policy.h"
#include "audio_policy.h"
#include "adaptive_performance.h"
// SW2SNES for hbmenu (Switch 2 homebrew, kaiHEN): SNES emulator front end.
// A port of the CivHomebrew app (app/sw2nes.js): the same screens, settings,
// save formats and defaults, drawn with OpenGL ES at 1920x1080.
#include <switch.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "audio.h"
#include "core.h"

// The active emulation core (chosen per game start; see Settings "Core").
static const CoreApi* C = &SNES9X_CORE;
#include "crt.h"
#include "uigfx.h"
#include "json.h"
#include <minizip/unzip.h>

#ifndef APP_VERSION
#define APP_VERSION "1.0.0-Stable"
#endif

using std::string;
using std::vector;
using gfx::Color;

/* ---------------- Constants (as in sw2nes.js) ---------------- */
// ROM folder: sdmc:/roms/nes by default (the shared roms/<system> layout that
// RetroArch users and frontends use); any folder can be chosen in Settings.
static const char* DEFAULT_ROM_DIR = "sdmc:/roms/snes";
static std::string romDir = DEFAULT_ROM_DIR;
static std::vector<std::string> extraRomDirs;
static bool addingRomFolder=false, pickingCoverFolder=false;
static string customCoverDir;
static const char* DATA_DIR = "sdmc:/switch/NS2xSNES9x";
static double gameRate = timing_policy::rate(false);
// Npad button bits (the same values the JS app uses).
enum { B_A = 1, B_B = 2, B_X = 4, B_Y = 8, B_L = 64, B_R = 128, B_PLUS = 1024, B_MINUS = 2048,
       B_LEFT = 4096, B_UP = 8192, B_RIGHT = 16384, B_DOWN = 32768, B_SL = 65536, B_SU = 131072, B_SR = 262144, B_SD = 524288 };
// SNES joypad bits (snes9x.h SNES_*_MASK).
enum { S_R = 1 << 4, S_L = 1 << 5, S_X = 1 << 6, S_A = 1 << 7, S_RIGHT = 1 << 8, S_LEFT = 1 << 9, S_DOWN = 1 << 10, S_UP = 1 << 11,
       S_START = 1 << 12, S_SELECT = 1 << 13, S_Y = 1 << 14, S_B = 1 << 15 };

static uint32_t C_BG = 0x0b0d12, C_PANEL = 0x151924, C_LINE = 0x2a3142, C_TEXT = 0xeef1f7, C_DIM = 0x9aa3b5, C_ACCENT = 0xe4463b, C_SEL = 0x2b3550;
static int themeIndex=3, accentIndex=0, screenBorder=0;
static const std::vector<std::string> BORDER_NAMES={"Black","Charcoal","Classic Purple","Theme accent"};
static const std::vector<std::string> THEME_NAMES={"Dark","OLED Black","Light","Classic Violet","Retro Ivory","Neon Cyber","Retro CRT","Midnight Ocean"};
static const std::vector<std::string> ACCENT_NAMES={"Theme default","Red","Blue","Green","Purple","Gold"};
static void applyTheme(){auto p=ui_theme::get(themeIndex,accentIndex);C_BG=p.bg;C_PANEL=p.panel;C_LINE=p.line;C_TEXT=p.text;C_DIM=p.dim;C_ACCENT=p.accent;C_SEL=p.sel;}
static Color col(uint32_t c, float a = 1) { return gfx::rgb(c, a); }

static const vector<string> ASPECTS = {"1:1", "8:7", "4:3", "Stretch", "Switch 2 (16:9)"}, SCALES = {"Integer", "Fit"}, CROPS = {"Show all", "Hide sides", "Hide all edges"};
static const vector<string> CRT_STYLES = {"Off", "Scanlines", "Soft scanlines", "Bold scanlines", "Aperture grille", "Scanlines + grille",
                                          "Slot mask", "Shadow mask", "Dot mask", "LCD matrix", "Screen door"};
static const vector<int> CRT_CHOICES = {0, 1, 2, 3, 4, 5, 9, 10};
// Picture effects: rounded corners and vignette (all black-only, default off).
static const vector<string> CORNER_RADII = {"Off", "Small", "Medium", "Large", "Extra large"};
static const float CORNER_RADIUS_FRAC[5] = {0, 0.04f, 0.07f, 0.11f, 0.16f};  // of picture height
static const vector<string> CORNER_SOFTNESS = {"Sharp", "Low", "Medium", "High"};
static const float CORNER_SOFT_FRAC[4] = {0, 0.006f, 0.015f, 0.03f};  // of picture height ("Sharp" = 1 px anti-aliasing)
static const vector<string> VIGNETTES = {"Off", "Light", "Medium", "Strong"};
static const float VIGNETTE_STRENGTH[4] = {0, 0.3f, 0.5f, 0.75f};
static const vector<string> VIGNETTE_SIZES = {"Small", "Medium", "Large"};
static const float VIGNETTE_START[3] = {0.75f, 0.5f, 0.2f};  // where darkening begins (0 centre .. 1 corner)
// GPU shaders (romfs:/shaders). "None" keeps exact pixels and the CRT styles.
static const vector<string> SHADERS = {"None", "Sharp bilinear", "Bilinear", "Scale2x", "CRT-Geom", "CRT-Lottes", "zfast CRT", "CRT-Easymode", "CRT-Aperture", "LCD grid", "CRT-Royale"};
static const vector<string> SHADER_FILES = {"none.glsl", "sharp_bilinear.glsl", "bilinear.glsl", "scale2x.glsl", "crt_geom.glsl", "crt_lottes.glsl", "zfast_crt.glsl", "crt_easymode.glsl", "crt_aperture.glsl", "lcd_grid.glsl", "crt_royale.glsl"};
// Brightness boost (AutoHDR-style lift in SDR): exponent k of 1-(1-c)^k.
static const vector<string> BOOSTS = {"Off", "Low", "Medium", "High"};
static const float BOOST_K[4] = {1.0f, 1.3f, 1.6f, 2.0f};

static const vector<string> SIGNALS = {"RGB", "Composite", "Composite + dot crawl", "S-Video"};
static const vector<string> CURVATURES = {"Off", "Light", "Medium", "Strong"};
static const float CURVATURE[4] = {0, 0.03f, 0.055f, 0.09f};
// Latency and playback options (all games).
static const vector<string> RUN_AHEADS = {"Off", "1 frame", "2 frames"};
static const vector<string> FRAME_DELAYS = {"Off", "4 ms", "8 ms"};  // 12 ms overran 60 Hz refreshes
static const int FRAME_DELAY_MS[3] = {0, 4, 8};
static const vector<string> AUDIO_BUFFERS = {"Small", "Normal", "Large"};
static const int AUDIO_TARGET_FRAMES[3] = {1920, 2880, 4320};  // queued audio: 40 / 60 / 90 ms at 48 kHz
static const vector<string> DISPLAY_MODES = {"60 Hz", "120 Hz / VRR"};
static const vector<string> FF_MODES = {"Off", "Hold ZR", "Toggle ZR"};
static const vector<string> FF_SPEEDS = {"1.5x", "2x", "3x", "4x"};
static const double FF_FACTOR[4] = {1.5, 2, 3, 4};
static const vector<string> STATS_LEVELS = {"Off", "FPS", "Full", "Full + graph"};
static const vector<string> OFF_ON = {"Off", "On"};
// Button mapping: Switch A, B, X, Y, L, R -> SNES buttons (same layout by
// default), turbo versions of the face buttons, or nothing.
enum { NFACE = 6 };
static const vector<string> FACE_KEYS = {"A", "B", "X", "Y", "L", "R"};
static const vector<string> FACE_CHOICES = {"SNES A", "SNES B", "SNES X", "SNES Y", "SNES L", "SNES R", "Turbo A", "Turbo B", "Turbo X", "Turbo Y", "None"};
static const int FACE_BITS[NFACE] = {B_A, B_B, B_X, B_Y, B_L, B_R}, FACE_TARGETS[6] = {S_A, S_B, S_X, S_Y, S_L, S_R}, FACE_DEFAULT[NFACE] = {0, 1, 2, 3, 4, 5};
static const vector<string> TURBO_RATES = {"10 Hz", "15 Hz", "20 Hz", "30 Hz"};
static const int TURBO_PERIOD[4] = {6, 4, 3, 2};
static const vector<string> VOLUMES = {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"};
static const vector<string> FILTERS = {"Off", "Soft", "Strong"};
static const vector<string> LIST_VIEWS = {"All", "Favourites", "Recent"};
static const int RECENT_MAX = 5, SLOT_COUNT = save_policy::slots;

/* ---------------- State ---------------- */
// Defaults (the author's chosen setup, 2026-11-14).
static int aspectMode = 4, scaleMode = 0, cropMode = 2, crtStyle = 0, paletteMode = 0, statsLevel = 1;
static int runAhead = 1, frameDelay = 2, audioBuffer = 0, displayMode = 1, ffMode = 1, ffSpeed = 1;
static bool rewindOn = true;
static int performanceProfile = 2; // Balanced
static adaptive::Governor governor;
static const vector<string> PERFORMANCE_PROFILES = {"Manual", "Battery Saver", "Balanced", "Performance", "Low Latency"};
// Advanced options: hidden by default, leaving what a console's own emulator offers.
static bool showAdvanced = false;  // default: hidden (also after Reset all settings)
static const vector<string> ADVANCED_SETTINGS = {"Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette", "Vignette size",
    "Brightness boost", "Stats", "Run-ahead", "Frame delay (60 Hz)", "Audio buffer", "Display", "Fast-forward speed", "Dim game in menu", "Audio filter", "Turbo speed"};
static const vector<string> ADVANCED_MENU = {"Run-ahead", "Stats", "Scale", "Overscan", "Signal", "Curvature", "Corner radius", "Corner softness", "Vignette",
    "Vignette size", "Brightness boost", "Settings for", "Save screenshot", "Cheats..."};
static int cornerRadius = 1, cornerSoftness = 1, vignette = 1, vignetteSize = 0, curvature = 0, shader = 3, signalMode = 0, boost = 2;
// Button mapping per player (Switch A, B, X, Y, L, R -> FACE_CHOICES).
static int faceMap[NFACE] = {0, 1, 2, 3, 4, 5}, faceMap2[NFACE] = {0, 1, 2, 3, 4, 5};
static int analogDeadzone=0;
static const vector<string> DEADZONES={"System default","15%","25%","35%","50%","Disabled"};
static const vector<string> CONTROLLER_PRESETS={"Custom / current","SNES layout","Swap A/B and X/Y"};
static void applyControllerPreset(int* map,int preset){
  if(preset==0)return;
  for(int i=0;i<NFACE;i++)map[i]=i;
  if(preset==2){std::swap(map[0],map[1]);std::swap(map[2],map[3]);}
}
static u64 analogButtons(PadState* pad){
  u64 b=padGetButtons(pad);
  if(!analogDeadzone)return b;
  b &= ~(u64)(B_SL|B_SR|B_SU|B_SD);
  if(analogDeadzone==5)return b;
  const int thresholds[]={0,4915,8192,11468,16384};
  auto stick=padGetStickPos(pad,0);
  int t=thresholds[analogDeadzone];
  if(stick.x<=-t)b|=B_SL;
  if(stick.x>=t)b|=B_SR;
  if(stick.y<=-t)b|=B_SD;
  if(stick.y>=t)b|=B_SU;
  return b;
}
static int remapPlayer = 0;  // Settings: whose buttons the mapping rows edit (not saved)
static int* curMap() { return remapPlayer ? faceMap2 : faceMap; }
static int turboRate = 1, volumeLevel = 10, audioFilter = 0;
static bool autoLoadState = false, pauseInMenu = false, dimInMenu = false;
// At most one frame in flight: input is read only after the previous frame
// finished on the GPU (the driver default let frames queue up: 100+ ms lag).
static const int frameQueue = 1;
static vector<string> recentGames, favourites;

struct Rom { string name, dir; };  // file name (.sfc, .smc or .zip) and its folder
static vector<Rom> allRoms, roms;
static int selected = 0, top = 0, listView = 0, btnIndex = 0;
static bool libraryGrid = false;
static int librarySort = 0;
static const vector<string> LIBRARY_SORTS = {"Favourites first", "Name", "Playtime", "Recently played"};
static string restoreSelection;
static int coverLoadsRemaining = 0;
struct CoverEntry {string path;gfx::Image* image;int w,h;};
static vector<CoverEntry> covers;
static void clearCovers(){for(auto& c:covers)gfx::imageDestroy(c.image);covers.clear();}
static CoverEntry& coverFor(const string& path){
  for(size_t i=0;i<covers.size();++i)if(covers[i].path==path){std::rotate(covers.begin()+i,covers.begin()+i+1,covers.end());return covers.back();}
  if(!coverLoadsRemaining){static CoverEntry pending{"",nullptr,0,0};return pending;}
  --coverLoadsRemaining;
  if(covers.size()>=cover_policy::cacheLimit){gfx::imageDestroy(covers.front().image);covers.erase(covers.begin());}
  CoverEntry c{path,nullptr,0,0};c.image=gfx::imageLoadCover(path.c_str(),c.w,c.h);covers.push_back(c);return covers.back();
}
static cover_policy::Index coverIndex;
static string coverDirectory(){return customCoverDir.empty()?string(DATA_DIR)+"/covers":customCoverDir;}
static vector<string> coverFiles, coverDirs, unmatchedCovers;
static size_t coverDirCursor=0,coverCheckCursor=0,coverRomCursor=0;
static int coverValid=0,coverInvalid=0,coverMatched=0;
static bool coverScanning=false,coverScanned=false,coverRootOK=false;
static std::unordered_set<string> validCoverNames,usedCoverNames;
static void readCoverDir(const string& rel){
  string path=coverDirectory()+(rel.empty()?"":"/"+rel);
  if(DIR* d=opendir(path.c_str())){
    if(rel.empty())coverRootOK=true;
    while(auto* e=readdir(d)){
      string n=e->d_name;if(n.empty()||n[0]=='.')continue;
      string child=rel.empty()?n:rel+"/"+n;struct stat st{};
      if(stat((coverDirectory()+"/"+child).c_str(),&st)!=0)continue;
      if(S_ISDIR(st.st_mode)){if(std::count(child.begin(),child.end(),'/')<12&&coverDirs.size()<4096)coverDirs.push_back(child);}
      else if(S_ISREG(st.st_mode)&&cover_policy::format(n)<3&&coverFiles.size()<50000){coverFiles.push_back(child);coverIndex.add(child);}
    }closedir(d);
  }
}
static void beginCoverScan(){
  clearCovers();coverIndex.clear();coverFiles.clear();coverDirs={""};unmatchedCovers.clear();validCoverNames.clear();usedCoverNames.clear();
  coverDirCursor=coverCheckCursor=coverRomCursor=0;coverValid=coverInvalid=coverMatched=0;coverRootOK=false;coverScanning=true;coverScanned=false;
}
static void indexCovers(){beginCoverScan();}
static void tickCoverScan(){
  if(!coverScanning)return;
  if(coverDirCursor<coverDirs.size()){string rel=coverDirs[coverDirCursor++];readCoverDir(rel);return;}
  if(coverCheckCursor<coverFiles.size()){
    const string& n=coverFiles[coverCheckCursor++];int w=0,h=0;auto im=gfx::imageLoadCover((coverDirectory()+"/"+n).c_str(),w,h);
    if(im){++coverValid;validCoverNames.insert(n);gfx::imageDestroy(im);}else ++coverInvalid;return;
  }
  if(coverRomCursor<allRoms.size()){
    for(int k=0;k<64&&coverRomCursor<allRoms.size();++k){const auto& r=allRoms[coverRomCursor++];bool matched=false;for(const auto& n:coverIndex.candidates(r.name))if(validCoverNames.count(n)){usedCoverNames.insert(n);matched=true;}if(matched)++coverMatched;}return;
  }
  for(const auto& n:coverFiles)if(validCoverNames.count(n)&&!usedCoverNames.count(n))unmatchedCovers.push_back(n);
  coverScanning=false;coverScanned=true;
}
static CoverEntry& gameCover(const string& dir,const string& name){
  for(const auto& n:coverIndex.candidates(name)){
    auto& c=coverFor(coverDirectory()+"/"+n);
    if(c.image||c.path.empty())return c;
  }
  // Scan sidecar names once per ROM directory during lookup; cache results until library refresh.
  static CoverEntry missing{"missing",nullptr,0,0};
  for(const char* ext:{".png",".jpg",".jpeg",".PNG",".JPG",".JPEG"}){
    string p=dir+"/"+cover_policy::stem(name)+ext;struct stat st{};if(stat(p.c_str(),&st)!=0)continue;auto& c=coverFor(p);if(c.image||c.path.empty())return c;
  }
  return missing;
}
static string libraryQuery;
static int queryLetter = 0;
static const string QUERY_LETTERS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static string focusArea = "list";
static double exitArmed = 0;

struct GameVals { int aspect, scale, crop, crt, palette, faces[NFACE], faces2p[NFACE], turbo, volume, filter, corner, cornerSoft, vignette, vignetteSize, curve, shader, signal, boost; };
struct Cheat { string code, desc; bool on; };
struct Patch { long offset; uint8_t orig; };
struct Running {
  string name, base;
  bool battery = false, perGame = false;
  uint32_t sramHash = 0;
  double sramCheck = 0;
  GameVals globalVals;
  Json slots = Json::object();
  vector<Cheat> cheats;
  vector<Patch> patches;
};
static Running* running = nullptr;
static string mode = "browser";
static bool quitApp = false;
static int stateSlot = 1, paletteApplied = -1;
static input_policy::Turbo turboP1, turboP2;
static input_policy::Reconnect reconnectP1, reconnectP2;

// UI state
static double toastUntil = 0, hintUntil = 0;
static string toastText, messageText;
static std::function<void()> messageThen;
static int settingsIndex = 0, settingsTop = 0;

static double nowMs() { return armTicksToNs(armGetSystemTick()) / 1e6; }
static FILE* logFile = nullptr;
static void record(const char* fmt, ...) {
  if (!logFile) return;
  va_list ap; va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fputc('\n', logFile); fflush(logFile);
}

/* ---------------- Files ---------------- */
static bool readFile(const string& path, vector<uint8_t>& out, long limit = 4 * 1024 * 1024) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) f = fopen((path + ".bak").c_str(), "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
  long n = ftell(f);
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
  if (n < 0 || n > limit) { fclose(f); return false; }
  out.resize(n);
  bool ok = n == 0 || fread(out.data(), 1, n, f) == (size_t)n;
  ok = fclose(f) == 0 && ok;
  if (!ok) out.clear();
  return ok;
}
static bool readText(const string& path, string& out) {
  vector<uint8_t> d;
  if (!readFile(path, d, 1024 * 1024)) return false;
  out.assign(d.begin(), d.end());
  return true;
}
// Writes via a temporary file and rename, so a crash never leaves half a save.
static bool writeFile(const string& path, const void* data, size_t n) {
  string tmp = path + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, n, f) == n;
  ok = fclose(f) == 0 && ok;
  if (!ok) { remove(tmp.c_str()); return false; }
  // Never delete the previous save before the replacement is committed.
  if (rename(tmp.c_str(), path.c_str()) == 0) return true;
  // Some filesystems reject replacement rename. Preserve a recoverable backup.
  const string bak = path + ".bak";
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || stat(bak.c_str(), &st) == 0 || rename(path.c_str(), bak.c_str()) != 0) { remove(tmp.c_str()); return false; }
  if (rename(tmp.c_str(), path.c_str()) == 0) { remove(bak.c_str()); return true; }
  rename(bak.c_str(), path.c_str());
  remove(tmp.c_str());
  return false;
}
static bool writeText(const string& path, const string& text) { return writeFile(path, text.data(), text.size()); }
static bool exists(const string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }

static string lower(string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }
static bool hasExt(const string& n, const char* ext) { return n.size() > 4 && lower(n.substr(n.size() - 4)) == ext; }
static bool isRomExt(const string& n) { return hasExt(n, ".sfc") || hasExt(n, ".smc") || hasExt(n, ".fig") || hasExt(n, ".swc"); }
static string stripNes(const string& n) { return isRomExt(n) || hasExt(n, ".zip") ? n.substr(0, n.size() - 4) : n; }
static string sanitize(const string& name) {
  string s = stripNes(name), out;
  for (char c : s) out += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == ' ' || c == '-') ? c : '_';
  if (out.size() > 80) out.resize(80);
  return out.empty() ? "game" : out;
}
static bool contains(const vector<string>& v, const string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static int indexOf(const vector<string>& v, const string& s) { auto it = std::find(v.begin(), v.end(), s); return it == v.end() ? -1 : (int)(it - v.begin()); }

/* ---------------- Settings ---------------- */
static string SETTINGS() { return string(DATA_DIR) + "/settings.json"; }
static GameVals gameValues() {
  GameVals g = {aspectMode, scaleMode, cropMode, crtStyle, paletteMode, {faceMap[0], faceMap[1], faceMap[2], faceMap[3], faceMap[4], faceMap[5]}, {faceMap2[0], faceMap2[1], faceMap2[2], faceMap2[3], faceMap2[4], faceMap2[5]}, turboRate, volumeLevel, audioFilter,
                cornerRadius, cornerSoftness, vignette, vignetteSize, curvature, shader, signalMode, boost};
  return g;
}
static int pick(const Json* x, int n, int d) { return x && x->isNum() && x->n >= 0 && x->n < n ? (int)x->n : d; }
static void setGameValues(const GameVals& g) {
  aspectMode = g.aspect; scaleMode = g.scale; cropMode = g.crop; crtStyle = g.crt; paletteMode = g.palette;
  for (int i = 0; i < NFACE; i++) { faceMap[i] = g.faces[i]; faceMap2[i] = g.faces2p[i]; }
  turboRate = g.turbo; volumeLevel = g.volume; audioFilter = g.filter;
  cornerRadius = g.corner; cornerSoftness = g.cornerSoft; vignette = g.vignette; vignetteSize = g.vignetteSize; curvature = g.curve; shader = g.shader; signalMode = g.signal; boost = g.boost;
}
static void setGameValuesJson(const Json& v) {
  aspectMode = pick(v.get("aspect"), ASPECTS.size(), aspectMode); scaleMode = pick(v.get("scale"), SCALES.size(), scaleMode); cropMode = pick(v.get("crop"), CROPS.size(), cropMode);
  const Json* c = v.get("crt");
  (void)c;  // the CRT strip styles were replaced by shaders (2.5.0): old values are ignored
  turboRate = pick(v.get("turbo"), TURBO_RATES.size(), turboRate);
  volumeLevel = pick(v.get("volume"), VOLUMES.size(), volumeLevel); audioFilter = pick(v.get("filter"), FILTERS.size(), audioFilter);
  cornerRadius = pick(v.get("corner"), CORNER_RADII.size(), cornerRadius); cornerSoftness = pick(v.get("cornerSoft"), CORNER_SOFTNESS.size(), cornerSoftness);
  vignette = pick(v.get("vignette"), VIGNETTES.size(), vignette); vignetteSize = pick(v.get("vignetteSize"), VIGNETTE_SIZES.size(), vignetteSize);
  curvature = pick(v.get("curvature"), CURVATURES.size(), curvature);
  shader = pick(v.get("shader"), SHADERS.size(), shader);
  signalMode = pick(v.get("signal"), SIGNALS.size(), signalMode);
  boost = pick(v.get("boost"), BOOSTS.size(), boost);
  const Json* f = v.get("faces");
  if (f && f->isArr() && f->a.size() == NFACE) for (int i = 0; i < NFACE; i++) faceMap[i] = pick(&f->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
  const Json* f2 = v.get("facesP2");
  if (f2 && f2->isArr() && f2->a.size() == NFACE) for (int i = 0; i < NFACE; i++) faceMap2[i] = pick(&f2->a[i], FACE_CHOICES.size(), FACE_DEFAULT[i]);
}
static Json gameJson(const GameVals& g) {
  Json j = Json::object();
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  j.set("crt", Json::number(g.crt)); j.set("palette", Json::number(g.palette));
  Json f = Json::array(); for (int i = 0; i < NFACE; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces", f);
  Json f2 = Json::array(); for (int i = 0; i < NFACE; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  return j;
}
static Json strings(const vector<string>& v) { Json a = Json::array(); for (auto& s : v) a.a.push_back(Json::string(s)); return a; }

static Json settingsJson(const GameVals& g) {
  Json j = Json::object();
  j.set("v", Json::number(6)); j.set("crt", Json::number(g.crt)); j.set("stats", Json::boolean(statsLevel > 0)); j.set("statsLevel", Json::number(statsLevel));
  j.set("aspect", Json::number(g.aspect)); j.set("scale", Json::number(g.scale)); j.set("crop", Json::number(g.crop));
  Json f = Json::array(); for (int i = 0; i < NFACE; i++) f.a.push_back(Json::number(g.faces[i]));
  j.set("faces2", f);
  Json f2 = Json::array(); for (int i = 0; i < NFACE; i++) f2.a.push_back(Json::number(g.faces2p[i]));
  j.set("facesP2", f2); j.set("turbo", Json::number(g.turbo)); j.set("palette", Json::number(g.palette));
  j.set("volume", Json::number(g.volume)); j.set("filter", Json::number(g.filter));
  j.set("corner", Json::number(g.corner)); j.set("cornerSoft", Json::number(g.cornerSoft)); j.set("vignette", Json::number(g.vignette)); j.set("vignetteSize", Json::number(g.vignetteSize)); j.set("curvature", Json::number(g.curve)); j.set("shader", Json::number(g.shader)); j.set("signal", Json::number(g.signal)); j.set("boost", Json::number(g.boost));
  j.set("recent", strings(recentGames)); j.set("favourites", strings(favourites));
  j.set("coverDir",Json::string(customCoverDir)); j.set("romDir", Json::string(romDir)); j.set("extraRomDirs",strings(extraRomDirs));
  j.set("librarySort",Json::number(librarySort));
  j.set("librarySelected",Json::string(selected>=0 && selected<(int)roms.size()?roms[selected].name:restoreSelection));
  j.set("libraryGrid",Json::boolean(libraryGrid));j.set("libraryView",Json::number(listView));
  j.set("advanced", Json::boolean(showAdvanced));
  j.set("screenBorder",Json::number(screenBorder));
  j.set("uiTheme",Json::number(themeIndex));j.set("uiAccent",Json::number(accentIndex));
  j.set("analogDeadzone",Json::number(analogDeadzone));
  j.set("performanceProfile", Json::number(performanceProfile)); j.set("runAhead", Json::number(runAhead)); j.set("frameDelay", Json::number(frameDelay)); j.set("audioBuffer", Json::number(audioBuffer));
  j.set("display", Json::number(displayMode)); j.set("rewind", Json::boolean(rewindOn)); j.set("ffMode", Json::number(ffMode)); j.set("ffSpeed", Json::number(ffSpeed));
  j.set("autoLoad", Json::boolean(autoLoadState)); j.set("pauseMenu", Json::boolean(pauseInMenu)); j.set("dimMenu", Json::boolean(dimInMenu));
  return j;
}
static void saveSettings() {
  GameVals g = running && running->perGame ? running->globalVals : gameValues();
  if (running && running->perGame && !writeText(running->base + ".settings.json", gameJson(gameValues()).dump())) record("Game settings save failed");
  if (!writeText(SETTINGS(), settingsJson(g).dump())) record("Settings save failed");
}
static void applySettingsJson(const Json& v) {
  Json gv = Json::object();
  if (const Json* f = v.get("faces2")) gv.set("faces", *f);
  if (const Json* f = v.get("facesP2")) gv.set("facesP2", *f);
  for (const char* k : {"turbo", "palette", "volume", "filter", "aspect", "scale", "crop", "corner", "cornerSoft", "vignette", "vignetteSize", "curvature", "shader", "signal", "boost"}) if (const Json* x = v.get(k)) gv.set(k, *x);
  setGameValuesJson(gv);
  if (const Json* r = v.get("recent")) if (r->isArr()) { recentGames.clear(); for (auto& x : r->a) if (x.isStr() && (int)recentGames.size() < RECENT_MAX) recentGames.push_back(x.s); }
  if (const Json* r = v.get("favourites")) if (r->isArr()) { favourites.clear(); for (auto& x : r->a) if (x.isStr()) favourites.push_back(x.s); }
  if (const Json* b = v.get("autoLoad")) if (b->isBool()) autoLoadState = b->b;
  if (const Json* b = v.get("pauseMenu")) if (b->isBool()) pauseInMenu = b->b;
  if (const Json* b = v.get("dimMenu")) if (b->isBool()) dimInMenu = b->b;
  statsLevel = pick(v.get("statsLevel"), STATS_LEVELS.size(), statsLevel);
  screenBorder=pick(v.get("screenBorder"),BORDER_NAMES.size(),screenBorder);
  themeIndex=pick(v.get("uiTheme"),THEME_NAMES.size(),themeIndex);accentIndex=pick(v.get("uiAccent"),ACCENT_NAMES.size(),accentIndex);applyTheme();
  if(const Json* b=v.get("libraryGrid"))if(b->isBool())libraryGrid=b->b;
  listView=pick(v.get("libraryView"),LIST_VIEWS.size(),listView);
  librarySort=pick(v.get("librarySort"),LIBRARY_SORTS.size(),librarySort);
  if(const Json* r=v.get("librarySelected"))if(r->isStr())restoreSelection=r->s;
  if (const Json* b = v.get("advanced")) if (b->isBool()) showAdvanced = b->b;
  if (const Json* r = v.get("romDir")) if(r->isStr()){auto p=content_policy::normalize(r->s);if(!p.empty())romDir=p;}
  customCoverDir.clear();if(const Json* r=v.get("coverDir"))if(r->isStr()&&!r->s.empty())customCoverDir=content_policy::normalize(r->s);
  extraRomDirs.clear();if(const Json* r=v.get("extraRomDirs"))if(r->isArr())for(const auto& x:r->a)if(x.isStr())content_policy::add(extraRomDirs,x.s);
  analogDeadzone=pick(v.get("analogDeadzone"),DEADZONES.size(),0);
  performanceProfile = pick(v.get("performanceProfile"), PERFORMANCE_PROFILES.size(), performanceProfile); governor.reset();
  runAhead = pick(v.get("runAhead"), RUN_AHEADS.size(), runAhead); frameDelay = pick(v.get("frameDelay"), FRAME_DELAYS.size(), frameDelay);
  audioBuffer = pick(v.get("audioBuffer"), AUDIO_BUFFERS.size(), audioBuffer); displayMode = pick(v.get("display"), DISPLAY_MODES.size(), displayMode);
  ffMode = pick(v.get("ffMode"), FF_MODES.size(), ffMode); ffSpeed = pick(v.get("ffSpeed"), FF_SPEEDS.size(), ffSpeed);
  if (const Json* b = v.get("rewind")) if (b->isBool()) rewindOn = b->b;
  // ("crt": the CRT strip styles were replaced by shaders in 2.5.0; old values are ignored.)
}
static Json defaultSettings;  // the built-in defaults, captured before the settings file is read
static bool displaySaved = false;  // the file chose a display mode (otherwise 120 Hz only where available)
static void loadSettings() {
  defaultSettings = settingsJson(gameValues());
  string t; Json v;
  if (!readText(SETTINGS(), t) || !Json::parse(t, v) || v.type != Json::OBJ) return;
  applySettingsJson(v);
  displaySaved = v.get("display") != nullptr;
  if (const Json* d = v.get("frameDelay")) if (d->isNum() && d->n >= FRAME_DELAYS.size()) frameDelay = (int)FRAME_DELAYS.size() - 1;  // 12 ms -> 8 ms cap
}
// Settings -> Reset all settings: back to the built-in defaults (recent games,
// favourites and per-game settings files are kept).
static void resetAllSettings() {
  vector<string> rec = recentGames, fav = favourites;
  applySettingsJson(defaultSettings);
  recentGames = rec; favourites = fav;
  saveSettings();
}

/* ---------------- ROM list ---------------- */
static bool naturalLess(const string& a0, const string& b0) {
  string a = lower(a0), b = lower(b0);
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && isdigit((unsigned char)a[i2])) i2++;
      while (j2 < b.size() && isdigit((unsigned char)b[j2])) j2++;
      double x = atof(a.substr(i, i2 - i).c_str()), y = atof(b.substr(j, j2 - j).c_str());
      if (x != y) return x < y;
      i = i2; j = j2; continue;
    }
    if (a[i] != b[j]) return a[i] < b[j];
    i++; j++;
  }
  return a.size() - i < b.size() - j;
}
static double playSeconds(const string& name);
static void orderRoms() {
  string cur = selected >= 0 && selected < (int)roms.size() ? roms[selected].name : restoreSelection;
  std::unordered_set<string> fav(favourites.begin(),favourites.end());
  std::unordered_map<string,int> recent;
  for(size_t i=0;i<recentGames.size();++i) recent.emplace(recentGames[i],(int)i);
  struct Ranked { Rom rom; int fav, recent; double seconds; };
  vector<Ranked> list;
  list.reserve(allRoms.size());
  for(const auto& r:allRoms){
    bool f=fav.count(r.name)!=0;
    auto it=recent.find(r.name);
    int rank=it==recent.end()?1000000:it->second;
    if((listView==1&&!f)||(listView==2&&it==recent.end())||!library::matches(stripNes(r.name),libraryQuery))continue;
    list.push_back({r,f?0:1,rank,librarySort==2?playSeconds(r.name):0.0});
  }
  std::stable_sort(list.begin(),list.end(),[](const Ranked& a,const Ranked& b){
    if(librarySort==2&&a.seconds!=b.seconds)return a.seconds>b.seconds;
    if((librarySort==3||(librarySort==0&&listView==2))&&a.recent!=b.recent)return a.recent<b.recent;
    if(librarySort==0&&listView==0){
      if(a.fav!=b.fav)return a.fav<b.fav;
      if(a.fav!=0&&a.recent!=b.recent)return a.recent<b.recent;
    }
    return naturalLess(a.rom.name,b.rom.name);
  });
  roms.clear();roms.reserve(list.size());selected=0;top=0;
  for(auto& r:list){if(r.rom.name==cur)selected=(int)roms.size();roms.push_back(std::move(r.rom));}
  restoreSelection.clear();
}
// Time played per game (seconds, by file name) in playtime.json: counted while
// a game runs (also behind an unpaused menu), saved every 5 minutes, when a
// game is quit and when SW2SNES exits.
static Json playtime = Json::object();
static double playAccumSec = 0, playSavedAt = 0;
static string PLAYTIME() { return string(DATA_DIR) + "/playtime.json"; }
static void loadPlaytime() {
  string t; Json j;
  if (readText(PLAYTIME(), t) && Json::parse(t, j) && j.type == Json::OBJ) playtime = j;
}
static double playSeconds(const string& name) { const Json* v = playtime.get(name); return v && v->isNum() ? v->n : 0; }
static string playLabel(const string& name) {
  double sec = playSeconds(name);
  if (sec <= 0) return "";
  if (sec < 60) return "< 1 min";
  if (sec < 3600) return std::to_string((int)(sec/60)) + " min";
  int h = (int)(sec / 3600);
  return h == 1 ? "1 hour" : std::to_string(h) + " hours";
}
static void flushPlaytime(const string& name) {
  if (playAccumSec <= 0 || name.empty()) return;
  playtime.set(name, Json::number(round(playSeconds(name) + playAccumSec)));
  playAccumSec = 0;
  if (!writeText(PLAYTIME(), playtime.dump())) record("Play time save failed");
}
static void scan() {
  clearCovers();
  indexCovers();
  allRoms.clear();
  // One entry per game name (a .nes file wins over a .zip of the same name).
  vector<string> dirs{content_policy::normalize(romDir)};for(const auto& p:extraRomDirs)content_policy::add(dirs,p);
  std::unordered_set<string> seen;
  for (const string& dir : dirs) {
    vector<Rom> found;
    if (DIR* d = opendir(dir.c_str())) {
      while (struct dirent* e = readdir(d)) {
        string n = e->d_name;
        if (e->d_type == DT_DIR || n[0] == '.') continue;
        if (isRomExt(n) || hasExt(n, ".zip")) found.push_back(Rom{n, dir});
      }
      closedir(d);
    }
    std::sort(found.begin(), found.end(), [](const Rom& a, const Rom& b) { return isRomExt(a.name) && !isRomExt(b.name); });
    for (auto& r : found) if (seen.insert(lower(stripNes(r.name))).second) allRoms.push_back(r);
  }
  orderRoms();
  if (selected >= (int)roms.size()) selected = std::max(0, (int)roms.size() - 1);
}

static void applyEmuOptions() {
  if (!running) return;
  C->set_audio((int)lround(volumeLevel * 25.6), audioFilter);
}

/* ---------------- Picture geometry and CRT ---------------- */
static gfx::Picture* picture = nullptr;
// Current frame size (256 or 512 wide, 224/239 or 448/478 tall); geometry
// works in "logical" SNES pixels (256 x 224/239), the texture in frame pixels.
static int W = 256, H = 224;
static int logW() { return W > 256 ? W / 2 : W; }
static int logH() { return H > 240 ? H / 2 : H; }
static int picX = 0, picY = 0, picW = 0, picH = 0, picCX = 0, picCY = 0, picCW = 0, picCH = 0;
static double picSX = 0, picSY = 0;
static string crtKey;
// Port of fitPicture(): Integer = whole-number pixel sizes closest to the
// aspect; Fit = largest exact aspect; crop offsets the image.
static void fitPicture() {
  int vw = gfx::SCREEN_W, vh = gfx::SCREEN_H;
  int lw = logW(), lh = logH(), kx = W / lw, ky = H / lh;
  // Fullscreen is independent of saved integer scaling and overscan settings.
  if (aspectMode == 4) {
    picX = picY = picCX = picCY = 0;
    picW = vw; picH = vh; picCW = W; picCH = H;
    picSX = (double)vw / lw; picSY = (double)vh / lh;
    return;
  }
  int cx = cropMode >= 1 ? 8 : 0, cy = cropMode >= 2 ? 8 : 0, cw = lw - 2 * cx, ch = lh - 2 * cy;
  double par = aspectMode == 1 ? 8.0 / 7.0 : aspectMode == 2 ? (4.0 / 3.0) * ch / cw : 1, sx, sy;
  if (scaleMode == 0 && vh >= ch && vw >= cw) {
    int maxX = vw / cw, maxY = vh / ch;
    if (aspectMode == 3) { sx = maxX; sy = maxY; }
    else { sy = maxY; sx = std::max(1, std::min(maxX, (int)lround(sy * par))); if (sx * cw > vw) sx = maxX; }
  } else if (aspectMode == 3) { sx = (double)vw / cw; sy = (double)vh / ch; }
  else { double fit = std::min(vw / (cw * par), (double)vh / ch); sy = fit; sx = fit * par; }
  int w = (int)lround(cw * sx), h = (int)lround(ch * sy);
  picX = (vw - w) / 2; picY = (vh - h) / 2; picW = w; picH = h; picSX = sx; picSY = sy;
  picCX = cx * kx; picCY = cy * ky; picCW = cw * kx; picCH = ch * ky;  // source rectangle in frame pixels
}
static double emuFrames = 0;
// Shows the core's latest frame; refits the picture when the SNES changes
// resolution (e.g. between 256 and 512 wide modes).
static void applyCrt();
static void showFrame() {
  if (!picture) return;
  int w = C->width(), h = C->height();
  if (w != W || h != H) { W = w; H = h; fitPicture(); crtKey.clear(); applyCrt(); }
  gfx::pictureUpload(picture, C->video(), W, H, C->video_pitch());
  gfx::pictureSetFrameCount(picture, (float)emuFrames);
}
static void applyCrt() {
  if (!picture) return;
  gfx::pictureSetShader(picture, shader);
  gfx::pictureSetBoost(picture, BOOST_K[boost]);
  {
    float dec[4] = {0.0f, 1.0f, 0.0f, 1.0f};  // NTSC decode: no hue shift, normal saturation
    gfx::pictureSetSignal(picture, signalMode, dec, 1.0f);
  }
  gfx::pictureSetEffects(picture, aspectMode == 4 ? 0.0f : picH * CORNER_RADIUS_FRAC[cornerRadius], std::max(1.0f, picH * CORNER_SOFT_FRAC[cornerSoftness]),
                         VIGNETTE_STRENGTH[vignette], VIGNETTE_START[vignetteSize], aspectMode == 4 ? 0.0f : CURVATURE[curvature]);
  char key[200];
  snprintf(key, sizeof key, "%d,%d,%d,%.6f,%.6f,%d,%d", crtStyle, picW, picH, picSX, picSY, picCX, picCY);
  if (crtKey == key) return;
  crtKey = key;
  if (crtStyle <= 0) { gfx::pictureSetCrt(picture, nullptr, 0, nullptr, 0); return; }
  vector<uint8_t> rows, cols;
  crt::strips(crtStyle, picW, picH, picSX, picSY, picCX, picCY, rows, cols);
  gfx::pictureSetCrt(picture, rows.data(), picH, cols.data(), picW);
}
static void refreshPicture() { if (running && picture) { applyEmuOptions(); showFrame(); applyCrt(); } }
static string scaleLabel(int i) {
  if (aspectMode == 4) return SCALES[i] + " (override)";
  if (i == 0 && scaleMode == 0 && picSX == (int)picSX && picSY == (int)picSY && picSX > 0) return SCALES[0] + " " + std::to_string((int)picSX) + "x" + std::to_string((int)picSY);
  return SCALES[i];
}

/* ---------------- Toasts and messages ---------------- */
static void showToast(const string& t, double ms = 1800) { toastText = t; toastUntil = nowMs() + ms; }
static void showMessage(const string& text, std::function<void()> then) { messageText = text; messageThen = then; mode = "message"; }

/* ---------------- Timing and stats ---------------- */
static double shownTimes[601];
static int shownHead = 0, shownCount = 0;
static void noteShown(double t) { shownTimes[shownHead] = t; shownHead = (shownHead + 1) % 601; if (shownCount < 601) shownCount++; }
static void videoFps(double& avg, double& low) {
  avg = low = 0;
  if (shownCount < 3) return;
  int n = shownCount, L = 601;
  double first = shownTimes[(shownHead - n + L) % L], last = shownTimes[(shownHead - 1 + L) % L];
  int k = std::max(1, (n - 1) / 100);
  vector<double> gaps;
  for (int i = 1; i < n; i++) gaps.push_back(shownTimes[(shownHead - n + i + L) % L] - shownTimes[(shownHead - n + i - 1 + L) % L]);
  std::partial_sort(gaps.begin(), gaps.begin() + k, gaps.end(), std::greater<double>());
  double worst = 0; for (int i = 0; i < k; i++) worst += gaps[i];
  if (last > first && worst > 0) { avg = (n - 1) * 1000 / (last - first); low = 1000 / (worst / k); }
}
// Frame-time graph: 240 frames, 2 px each, scrolling left; 16.7 ms in the middle.
enum { GRAPH_STEP = 2, GRAPH_FRAMES = 240, GRAPH_W = GRAPH_STEP * GRAPH_FRAMES, GRAPH_H = 40 };
static const double GRAPH_MAX_MS = 33.4;
static uint32_t graphPix[GRAPH_W * GRAPH_H];
static gfx::Image* graphImage = nullptr;
static int graphPrevY = -1;
static void resetGraph() {
  graphPrevY = -1;
  int mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  for (int i = 0; i < GRAPH_W * GRAPH_H; i++) graphPix[i] = 0;
  for (int x = 0; x < GRAPH_W; x++) graphPix[mid * GRAPH_W + x] = 0x4dffffff;
}
static void drawGraph(double ms) {
  int S = GRAPH_STEP, mid = GRAPH_H - (int)lround(16.7 / GRAPH_MAX_MS * GRAPH_H);
  int y = GRAPH_H - 1 - (int)lround(std::min(ms, GRAPH_MAX_MS) / GRAPH_MAX_MS * (GRAPH_H - 2));
  int y0 = graphPrevY < 0 ? y : graphPrevY, lo = std::min(y0, y), hi = std::max(y0, y);
  for (int r = 0; r < GRAPH_H; r++) {
    uint32_t* row = graphPix + r * GRAPH_W;
    memmove(row, row + S, (GRAPH_W - S) * 4);
    for (int x = GRAPH_W - S; x < GRAPH_W; x++) row[x] = (r >= lo && r <= hi) ? 0xff6ee26e : r == mid ? 0x4dffffff : 0;
  }
  graphPrevY = y;
  if (graphImage) gfx::imageUpload(graphImage, graphPix);
}

static double phase[5] = {0, 0, 0, 0, 0};  // per-second sums: fence wait, frame start (display wait), emulation, drawing, swap
static int phaseN = 0, sessionLog = 0;
static double swapAcc = 0, waitAcc = 0, swapMax = 0;
static int swapN = 0;
static double lastSwap = 0, tickMs = 1000.0 / 60, audioRatio = 1, pendEma = 2, statsAt = 0, workMs = 0, startedAt = 0;
static int emuCount = 0, ratioTicks = 0;
static FILE* benchmarkFile = nullptr;
static audio_policy::Recovery audioRecovery;
static unsigned long long starvationRefills = 0, droppedBase = 0;
static string statsText;
static bool comboLatch = false, frameDrawn = false;

// Rewind history: a state every frame, delta-compressed (cores.cpp; ~1 KB per
// frame), within REWIND_BUDGET bytes; stepped back one per refresh.
static const long REWIND_BUDGET = 48L * 1024 * 1024;
static bool snapsReady = false;
static bool ffLatched = false, ffPrevHeld = false, rewinding = false, fastForward = false;
static double emuAcc = 0, ffAcc = 0, audioInteg = 0, loopGapMs = 16.69, loopStartPrev = 0, vrrNext = 0;
// The screen's refresh period when the loop is not paced by the game (menus):
// 120 Hz / VRR timing is used only when the screen really runs faster than 60 Hz.
static double freeTickMs = 0;
// VI2's report of the display (polled about once a second): the 120 Hz / VRR
// timing is used only while the display really is at 120 Hz or has VRR active.
static unsigned dispHz = 0;
static bool dispVrr = false, dispCan120 = false, dispKnown = false;
static double dispPolled = 0;
static void pollDisplay(double t) {
  if (dispPolled && t - dispPolled < 1000) return;
  dispPolled = t;
  unsigned hz; bool vrr, can;
  bool ok = gfx::displayInfo(hz, vrr, can);
  if (ok && (!dispKnown || hz != dispHz || vrr != dispVrr || can != dispCan120)) record("display: %u Hz, VRR %s, 120 Hz modes %s", hz, vrr ? "on" : "off", can ? "yes" : "no");
  if (ok) { dispHz = hz; dispVrr = vrr; dispCan120 = can; }
  dispKnown = ok;
}
static bool highRefresh() {
  if (displayMode != 1) return false;
  if (dispKnown) return dispHz >= 120 || dispVrr;
  return freeTickMs > 0 && freeTickMs < 12;  // fallback: measured refresh in menus
}
static void resetRewind() { rewind_clear(); }
static void setAudioRatio(double r) { if (r != audioRatio) { audioRatio = r; C->set_audio_ratio(r); } }
static void resetClock() { shownHead = shownCount = 0; resetGraph(); lastSwap = 0; }

/* ---------------- Battery saves and save states ---------------- */
static uint32_t hashSram() {
  uint32_t h = 2166136261u; uint8_t* p = C->sram(); int n = C->sram_size();
  for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void writeSram(uint32_t h) {
  if (!C->sram() || C->sram_size() <= 0 || C->sram_size() > 65536) { record("Invalid SRAM buffer"); return; }
  const string path=running->base+".srm";
  if(h==running->sramHash){struct stat st;if(stat(path.c_str(),&st)==0 && st.st_size==C->sram_size())return;}
  vector<uint8_t> old;
  struct stat st;
  if(stat(path.c_str(),&st)==0||stat((path+".bak").c_str(),&st)==0){
    if(!readFile(path,old,65536)||old.empty()||!writeFile(path+".previous",old.data(),old.size())){
      record("SRAM backup failed");showToast("SRAM backup failed; existing save kept");return;
    }
  }
  if (writeFile(path, C->sram(), C->sram_size())) running->sramHash = h;
  else { record("SRAM save failed"); showToast("Could not save game data"); }
}
static void saveSram(bool force) {
  if (!running || !running->battery || !C->sram() || C->sram_size() <= 0 || C->sram_size() > 65536) return;
  uint32_t h = hashSram();
  if (!force && h == running->sramHash) return;
  writeSram(h);
}
// Save states: <game>.state (slot 1), <game>.state2..4.
static string statePath(int n) { return running->base + C->stateTag + (n == 1 ? ".state" : ".state" + std::to_string(n)); }
static string slotsPath() { return running->base + C->stateTag + ".slots.json"; }
static double slotTime(int n) { if(!running)return 0;struct stat st;if(stat(statePath(n).c_str(),&st)!=0&&stat((statePath(n)+".bak").c_str(),&st)!=0)return 0;const Json* t=running->slots.get(std::to_string(n));return t&&t->isNum()?t->n:(double)st.st_mtime*1000; }
static string slotLabel(int n) {
  double t = slotTime(n);
  if (!t) return std::to_string(n) + "  (empty)";
  time_t s = (time_t)(t / 1000);
  struct tm* dt=localtime(&s);if(!dt)return std::to_string(n)+"  (saved)";
  struct tm d=*dt;
  char buf[40];
  snprintf(buf,sizeof buf,"%04d-%02d-%02d %02d:%02d",d.tm_year+1900,d.tm_mon+1,d.tm_mday,d.tm_hour,d.tm_min);
  return std::to_string(n) + "  " + buf;
}
static void saveState() {
  long len = C->state_save();
  if (!save_policy::valid(len) || !C->state_buffer(len)) { showToast("State save failed: Emulator could not create a save state."); return; }
  const string path=statePath(stateSlot);
  vector<uint8_t> old;
  struct stat existing;
  bool exists=stat(path.c_str(),&existing)==0||stat((path+".bak").c_str(),&existing)==0;
  if(exists&&(!readFile(path,old,save_policy::fileLimit)||old.empty()||!writeFile(path+".previous",old.data(),old.size()))){showToast("Backup failed; save unchanged");return;}
  auto packed=save_policy::pack(C->state_buffer(len),len);
  if (packed.empty() || !writeFile(path, packed.data(), packed.size())) { showToast("State save failed"); return; }
  running->slots.set(std::to_string(stateSlot), Json::number((double)time(nullptr) * 1000));
  running->slots.set("hash"+std::to_string(stateSlot),Json::number(save_policy::checksum(C->state_buffer(len),len)));
  if(!writeText(slotsPath(), running->slots.dump())){showToast("State saved; metadata write failed");return;}
  showToast("Saved to slot " + std::to_string(stateSlot));
}
static void loadState(bool quietIfNone, int n, bool previous=false) {
  vector<uint8_t> d;
  if (!readFile(statePath(n)+(previous?".previous":""), d, save_policy::fileLimit)) { if (!quietIfNone) showToast("Slot " + std::to_string(n) + " is empty"); return; }
  size_t offset=0;auto format=save_policy::inspect(d,offset);
  if(format==save_policy::Format::invalid){showToast("State checksum or size invalid; try previous save");return;}
  const Json* hash=running->slots.get("hash"+std::to_string(n));
  if(format==save_policy::Format::legacy&&!previous&&hash&&hash->isNum()&&hash->n!=(double)save_policy::checksum(d.data(),d.size())){showToast("Legacy state checksum mismatch; try previous save");return;}
  long length=long(d.size()-offset);
  char* buf=C->state_buffer(length);
  if(!buf){showToast("State load failed");return;}
  memcpy(buf,d.data()+offset,length);
  if(!C->state_load(length)){showToast("State load failed");return;}
  resetRewind();
  C->audio_reset();
  showToast("Loaded slot " + std::to_string(n));
}
static int newestSlot() {
  int best = 0; double bestT = 0;
  for (int n = 1; n <= SLOT_COUNT; n++) { double t = slotTime(n); if (t > bestT) { bestT = t; best = n; } }
  return best ? best : 1;
}

/* ---------------- Cheats ---------------- */
// SNES Game Genie ("XXXX-XXXX") and Pro Action Replay ("XXXXXXXX") codes,
// decoded and applied by Snes9x's cheat engine.
static string normCode(const string& in) {
  string t;
  for (char c : in) if (isxdigit((unsigned char)c) || c == '-' || c == ':') t += (char)toupper((unsigned char)c);
  return t;
}
static bool codeValid(const string& c) { return !c.empty() && C->cheat_valid(c.c_str()); }
static int applyCheats() {
  if (!running) return 0;
  vector<string> on; vector<const char*> ptrs;
  for (auto& c : running->cheats) if (c.on && codeValid(c.code)) on.push_back(c.code);
  for (auto& c : on) ptrs.push_back(c.c_str());
  return C->set_cheats(ptrs.data(), (int)ptrs.size());
}
static void saveCheats() {
  Json a = Json::array();
  for (auto& c : running->cheats) { Json o = Json::object(); o.set("code", Json::string(c.code)); o.set("desc", Json::string(c.desc)); o.set("on", Json::boolean(c.on)); a.a.push_back(o); }
  if (!writeText(running->base + ".cheats.json", a.dump())) record("Cheats save failed");
}
static void loadCheats(const string& romName, const string& romDir) {
  running->cheats.clear(); running->patches.clear();
  string t; Json j;
  if (readText(running->base + ".cheats.json", t) && Json::parse(t, j) && j.isArr())
    for (auto& c : j.a) {
      const Json* code = c.get("code");
      if (!code || !code->isStr() || !codeValid(code->s)) continue;
      const Json* d = c.get("desc"); const Json* on = c.get("on");
      running->cheats.push_back(Cheat{code->s, d && d->isStr() ? d->s : "", on && on->isBool() && on->b});
    }
  // Codes next to the ROM: <game>.gg, one "CODE description" per line.
  if (readText(romDir + "/" + stripNes(romName) + ".gg", t)) {
    size_t pos = 0;
    while (pos <= t.size()) {
      size_t e = t.find('\n', pos); if (e == string::npos) e = t.size();
      string line = t.substr(pos, e - pos); pos = e + 1;
      while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
      size_t s = 0; while (s < line.size() && isspace((unsigned char)line[s])) s++;
      size_t k = s; while (k < line.size() && !isspace((unsigned char)line[k])) k++;
      string code = normCode(line.substr(s, k - s));
      if (!codeValid(code)) continue;
      string desc = line.substr(k); size_t ds = desc.find_first_not_of(" \t"); desc = ds == string::npos ? "" : desc.substr(ds);
      if (desc.size() > 40) desc.resize(40);
      bool have = false; for (auto& c : running->cheats) if (c.code == code) have = true;
      if (!have) running->cheats.push_back(Cheat{code, desc, false});
    }
  }
}

/* ---------------- ROM checks ---------------- */
// Snes9x finds the layout (LoROM/HiROM, copier headers, chips) itself; this
// only rejects files that cannot be SNES games.
static string romCheck(const vector<uint8_t>& d) {
  if (d.size() < 0x8000) return "This file is too small to be a SNES game.\n\nThe file may be damaged, or an incomplete copy: copy it to the SD card again.";
  if (d.size() > 12 * 1024 * 1024) return "This file is too large to be a SNES game.";
  return "";
}

/* ---------------- Game lifecycle ---------------- */
static void quitGame();
// Reads a .sfc/.smc file, or the first one inside a .zip.
static bool readRom(const Rom& rom, vector<uint8_t>& data, string& err) {
  string path = rom.dir + "/" + rom.name;
  if (!hasExt(rom.name, ".zip")) { if (!readFile(path, data, rom_patch::limit)) { err = "The file could not be read."; return false; } return true; }
  unzFile z = unzOpen(path.c_str());
  if (!z) { err = "This zip file could not be opened. It may be damaged."; return false; }
  bool found = false;
  for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z)) {
    char name[512]; unz_file_info info;
    if (unzGetCurrentFileInfo(z, &info, name, sizeof name, NULL, 0, NULL, 0) != UNZ_OK) continue;
    name[sizeof(name)-1]=0;
    if (info.size_filename >= sizeof(name) || !isRomExt(name)) continue;
    if (info.uncompressed_size > rom_patch::limit) { err = "The game inside this zip is too large."; unzClose(z); return false; }
    data.resize(info.uncompressed_size);
    if (unzOpenCurrentFile(z) != UNZ_OK) break;
    size_t got=0;bool ok=true;
    while(got<data.size()){int n=unzReadCurrentFile(z,data.data()+got,(unsigned)std::min<size_t>(65536,data.size()-got));if(n<=0){ok=false;break;}got+=(size_t)n;}
    int crc=unzCloseCurrentFile(z);
    found=ok&&got==data.size()&&crc==UNZ_OK;
    if (!found) { err = "The game inside this zip could not be unpacked. The zip may be damaged."; unzClose(z); return false; }
    break;
  }
  unzClose(z);
  if (!found) { err = "This zip file has no SNES game (.sfc or .smc) inside."; return false; }
  return true;
}
static void startGame(const Rom& rom, bool resume=false) {
  vector<uint8_t> data;
  string readErr;
  if (!readRom(rom, data, readErr)) { showMessage(readErr, [] { mode = "browser"; }); return; }
  string patchSuffix;
  for(const char* ext:{".ips",".bps",".ups"}){
    string path=rom.dir+"/"+stripNes(rom.name)+ext;struct stat st;
    if(stat(path.c_str(),&st)!=0)continue;
    if(!patchSuffix.empty()){showMessage("Multiple ROM patches found; keep only one.", [] {mode="browser";});return;}
    vector<uint8_t> patch;
    if(!readFile(path,patch,rom_patch::limit)){showMessage("Cannot read ROM patch.", [] {mode="browser";});return;}
    bool ok=string(ext)==".ips"?rom_patch::ips(data,patch):string(ext)==".bps"?rom_patch::bps(data,patch):rom_patch::ups(data,patch);
    if(!ok){showMessage("Invalid or incompatible ROM patch; original unchanged.", [] {mode="browser";});return;}
    patchSuffix=string(ext)+"-"+std::to_string(rom_patch::identity(patch));
    record("Applied ROM patch: %s",path.c_str());
  }
  if (string bad = romCheck(data); !bad.empty()) { record("ROM check %s: %s", rom.name.c_str(), bad.c_str()); showMessage(bad, [] { mode = "browser"; }); return; }
  uint8_t* p = C->rom_alloc((long)data.size());
  if (!p) { showMessage("Not enough memory to load this game.", [] { mode = "browser"; }); return; }
  memcpy(p, data.data(), data.size());
  const string fullRomPath=rom.dir+"/"+rom.name;
  core_rom_name = fullRomPath.c_str();
  if (const char* err = C->load()) {
    core_rom_name = "";
    C->unload();
    showMessage(err, [] { mode = "browser"; });
    return;
  }
  core_rom_name = "";
  gameRate=timing_policy::rate(C->is_pal()!=0);
  record("Enhancement chips: %s", C->chips());
  running = new Running();
  running->name = rom.name;
  running->base = string(DATA_DIR) + "/" + sanitize(rom.name) + patchSuffix;
  running->battery = C->has_battery() != 0;
  // Import only missing files for unpatched games; never overwrite local saves.
  if(patchSuffix.empty()){
    const string legacy="sdmc:/switch/sw2snes/"+sanitize(rom.name);
    auto importMissing=[&](const string& suffix,bool state){
      const string dst=running->base+suffix;struct stat st;
      if(stat(dst.c_str(),&st)==0||stat((dst+".bak").c_str(),&st)==0||stat((dst+".previous").c_str(),&st)==0)return;
      vector<uint8_t> bytes;
      if(!readFile(legacy+suffix,bytes,state?save_policy::fileLimit:65536)||bytes.empty())return;
      if(state){size_t offset=0;if(save_policy::inspect(bytes,offset)==save_policy::Format::invalid)return;}
      else if(bytes.size()>(size_t)C->sram_size())return;
      if(writeFile(dst,bytes.data(),bytes.size()))record("Imported legacy save: %s",suffix.c_str());
    };
    if(running->battery)importMissing(".srm",false);
    for(int n=1;n<=4;++n)importMissing(string(C->stateTag)+(n==1?".state":".state"+std::to_string(n)),true);
  }
  if (running->battery) {
    vector<uint8_t> s;
    if (readFile(running->base + ".srm", s, 65536)) {
      if ((int)s.size() <= C->sram_size()) memcpy(C->sram(), s.data(), s.size());
      else record("Save file has the wrong size");
    }
    running->sramHash = hashSram();
  }
  C->power();
  snapsReady = rewind_init(REWIND_BUDGET);
  W = C->width(); H = C->height();
  record("Core %s, battery %d (%d bytes)", C->name, running->battery ? 1 : 0, C->sram_size());
  string t; Json j;
  if (readText(slotsPath(), t) && Json::parse(t, j) && j.type == Json::OBJ) running->slots = j;
  stateSlot = (autoLoadState || resume) ? newestSlot() : 1;
  running->globalVals = gameValues(); running->perGame = false;
  if (readText(running->base + ".settings.json", t) && Json::parse(t, j) && j.type == Json::OBJ) { setGameValuesJson(j); running->perGame = true; }
  recentGames.erase(std::remove(recentGames.begin(), recentGames.end(), rom.name), recentGames.end());
  recentGames.insert(recentGames.begin(), rom.name);
  if ((int)recentGames.size() > RECENT_MAX) recentGames.resize(RECENT_MAX);
  saveSettings();
  applyEmuOptions();
  loadCheats(rom.name, rom.dir);
  if (applyCheats()) showToast("Cheats active");
  fitPicture(); crtKey.clear(); applyCrt();
  hintUntil = nowMs() + 6000;
  if (autoLoadState || resume) loadState(true, stateSlot);
  resetClock(); mode = "running"; comboLatch = true;
  resetRewind(); emuAcc = 0; ffLatched = false; fastForward = false;
  audioRecovery.reset(); setAudioRatio(1); audioInteg = 0; pendEma = AUDIO_TARGET_FRAMES[audioBuffer];
  audio::prefill(AUDIO_TARGET_FRAMES[audioBuffer]); ratioTicks = 0; emuCount = 0; workMs = 0; statsAt = nowMs(); startedAt = statsAt; governor.reset(); turboP1.reset(); turboP2.reset();
  frameDrawn = false; statsText.clear(); sessionLog = 0;
  starvationRefills = 0; droppedBase = audio::droppedBuffers();
  for (double& x : phase) x = 0; phaseN = 0;
  string benchDir=string(DATA_DIR)+"/benchmarks";mkdir(benchDir.c_str(),0777);
  string benchBase=benchDir+"/"+sanitize(rom.name)+"-"+std::to_string((long long)time(nullptr));
  for(int i=0;i<100&&!benchmarkFile;i++){string p=benchBase+"-"+std::to_string(i)+".csv";struct stat st;if(stat(p.c_str(),&st)!=0){benchmarkFile=fopen(p.c_str(),"w");break;}}
  if (benchmarkFile) fputs("elapsed_s,emu_fps,video_fps,low_1pct_fps,speed_pct_region_reference,emu_ms,draw_ms,gpu_ms,audio_queue_ms,starvation_refills,dropped_buffers,rewind_bytes,rewind_states,fast_forward,rewinding,target_fps\n", benchmarkFile);
  running->sramCheck = nowMs();
  playSavedAt = nowMs(); playAccumSec = 0;
  record("Started %s", rom.name.c_str());
}
static void quitGame() {
  if (!running) return;
  if (benchmarkFile) { fclose(benchmarkFile); benchmarkFile = nullptr; }
  flushPlaytime(running->name);
  saveSram(true);
  resetRewind();
  C->audio_reset();
  C->unload();
  if (running->perGame) setGameValues(running->globalVals);
  delete running; running = nullptr;
  orderRoms();
  mode = "browser";
}

/* ---------------- Input ---------------- */
static int snesPad(u64 b, const int* map = faceMap) {
  auto& pulse = map == faceMap2 ? turboP2 : turboP1;
  int p = 0;
  for (int f = 0; f < NFACE; f++) {
    int t = map[f]; bool held = (b & FACE_BITS[f]) != 0;
    bool turboOn = pulse.step(f, held && t >= 6 && t < 10, TURBO_PERIOD[turboRate]);
    if (held && t >= 0 && t < 6) p |= FACE_TARGETS[t];
    else if (held && t >= 6 && t < 10 && turboOn) p |= FACE_TARGETS[t - 6];
  }
  if (b & B_PLUS) p |= S_START;
  if (b & B_MINUS) p |= S_SELECT;
  if (b & (B_UP | B_SU)) p |= S_UP;
  if (b & (B_DOWN | B_SD)) p |= S_DOWN;
  if (b & (B_LEFT | B_SL)) p |= S_LEFT;
  if (b & (B_RIGHT | B_SR)) p |= S_RIGHT;
  p = input_policy::clearOpposites(p,S_UP,S_DOWN);
  p = input_policy::clearOpposites(p,S_LEFT,S_RIGHT);
  return p;
}
// In-game menu: ZL + ZR (L and R are SNES buttons), as on Nintendo's own SNES app.
static const u64 MENU_COMBO = HidNpadButton_ZL | HidNpadButton_ZR;
static u64 lastBits = 0;
static int heldDirs = 0;
static double repeatAt = 0;
struct Nav { u64 pressed; bool up, down, left, right; };
static Nav navEvents(u64 bits) {
  Nav ev; ev.pressed = bits & ~lastBits;
  double t = nowMs(); int out = 0;
  int dirs = (bits & (B_UP | B_SU) ? 1 : 0) | (bits & (B_DOWN | B_SD) ? 2 : 0);
  if (mode == "browser" && libraryGrid) dirs |= (bits & (B_LEFT | B_SL) ? 4 : 0) | (bits & (B_RIGHT | B_SR) ? 8 : 0);
  if (dirs != heldDirs) { heldDirs = dirs; repeatAt = t + 380; out |= dirs; }
  else if (dirs && t >= repeatAt) { repeatAt = t + 110; out |= dirs; }
  lastBits = bits;
  ev.up = out & 1; ev.down = out & 2; ev.left = out & 4; ev.right = out & 8;
  return ev;
}

/* ---------------- In-game menu ---------------- */
struct MenuItem {
  string label;
  std::function<void()> action;                  // plain action (A)
  std::function<vector<string>()> choices;       // option: choices, get, set, after
  std::function<int()> get;
  std::function<void(int)> set;
  std::function<void()> after;
  bool option() const { return (bool)choices; }
};
static vector<MenuItem> menuItems;
static string menuTitle;
static int menuIndex = 0, menuTop = 0;
static MenuItem act(const string& l, std::function<void()> f) { MenuItem m; m.label = l; m.action = f; return m; }
static MenuItem opt(const string& l, std::function<vector<string>()> c, std::function<int()> g, std::function<void(int)> s, std::function<void()> a = nullptr) {
  MenuItem m; m.label = l; m.choices = c; m.get = g; m.set = s; m.after = a; return m;
}
static void showMenuItems(const string& title, const vector<MenuItem>& items) { menuItems = items; menuTitle = title; menuIndex = 0; menuTop = 0; }
static bool captureRequested = false;
static void closeMenu() { if (running) { mode = "running"; if (pauseInMenu) resetClock(); } }
static void pictureChanged() { fitPicture(); applyCrt(); saveSettings(); }
static void setPerGame(bool on) {
  if (on == running->perGame) return;
  if (on) { running->perGame = true; saveSettings(); showToast("Settings saved for this game"); }
  else { running->perGame = false; remove((running->base + ".settings.json").c_str()); setGameValues(running->globalVals); showToast("Using settings for all games"); }
}
static int crtChoiceIndex() { for (size_t i = 0; i < CRT_CHOICES.size(); i++) if (CRT_CHOICES[i] == crtStyle) return (int)i; return 0; }
static vector<string> crtChoiceNames() { vector<string> v; for (int id : CRT_CHOICES) v.push_back(CRT_STYLES[id]); return v; }

static void openMenu();
static void openCodeEditor();
static void openCheatsMenu() {
  vector<MenuItem> items;
  for (size_t i = 0; i < running->cheats.size(); i++) {
    Cheat& c = running->cheats[i];
    items.push_back(opt(c.code + (c.desc.empty() ? "" : "  " + c.desc), [] { return OFF_ON; },
                        [i] { return running->cheats[i].on ? 1 : 0; }, [i](int v) { running->cheats[i].on = v != 0; },
                        [] { applyCheats(); saveCheats(); }));
  }
  items.push_back(act("Add code", [] { openCodeEditor(); }));
  if (!running->cheats.empty()) items.push_back(act("Remove last code", [] { running->cheats.pop_back(); applyCheats(); saveCheats(); openCheatsMenu(); }));
  items.push_back(act("Back", [] { openMenu(); }));
  showMenuItems("Cheats", items);
}
static void openMenu() {
  vector<MenuItem> items = {
      act("Resume", [] { closeMenu(); }),
      act("Save state", [] { saveState(); closeMenu(); }),
      act("Load state", [] { closeMenu(); loadState(false, stateSlot); }),
      act("Load previous backup", [] { closeMenu(); loadState(false,stateSlot,true); }),
      opt("Slot", [] { vector<string> a; for (int n = 1; n <= SLOT_COUNT; n++) a.push_back(slotLabel(n)); return a; },
          [] { return stateSlot - 1; }, [](int v) { stateSlot = v + 1; }),
      act("Reset", [] { C->reset(0); closeMenu(); showToast("Reset"); }),
      opt("Performance profile", [] { return PERFORMANCE_PROFILES; }, [] { return performanceProfile; }, [](int v) { performanceProfile = v; governor.reset(); }, saveSettings),
      opt("Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }, saveSettings),
      opt("Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }, [] { resetGraph(); saveSettings(); }),
      opt("Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }, pictureChanged),
      opt("Scale", [] { return vector<string>{scaleLabel(0), scaleLabel(1)}; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }, pictureChanged),
      opt("Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }, pictureChanged),
      opt("Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }, pictureChanged),
      opt("Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }, pictureChanged),
      opt("Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }, pictureChanged),
      opt("Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }, pictureChanged),
      opt("Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }, pictureChanged),
      opt("Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }, pictureChanged),
      opt("Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }, pictureChanged),
      opt("Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }, pictureChanged),
      opt("Favourite", [] { return OFF_ON; }, [] { return contains(favourites, running->name) ? 1 : 0; },
          [](int v) { favourites.erase(std::remove(favourites.begin(), favourites.end(), running->name), favourites.end()); if (v) favourites.push_back(running->name); }, saveSettings),
      opt("Settings for", [] { return vector<string>{"All games", "This game"}; }, [] { return running->perGame ? 1 : 0; }, [](int v) { setPerGame(v != 0); },
          [] { pictureChanged(); refreshPicture(); }),
      act("Save screenshot", [] { closeMenu(); captureRequested = true; }),
      act("Cheats...", [] { openCheatsMenu(); }),
      act("Quit game", [] { quitGame(); }),
  };
  if (!showAdvanced) {
    vector<MenuItem> basic;
    for (auto& m : items) if (!contains(ADVANCED_MENU, m.label)) basic.push_back(m);
    items = basic;
  }
  showMenuItems(stripNes(running->name), items);
  mode = "menu";
  saveSram(false);
}
static void cycleMenuOption(int dir) {
  MenuItem& o = menuItems[menuIndex];
  if (!o.option()) return;
  int n = (int)o.choices().size();
  o.set((o.get() + dir + n) % n);
  if (o.after) o.after();
}

/* ---------------- Cheat code editor ---------------- */
// Eight hex digits; Y switches between Game Genie (XXXX-XXXX) and Pro Action
// Replay (XXXXXXXX). Left/Right move, Up/Down change the digit.
static const char* HEX = "0123456789ABCDEF";
static int ggLetters[8], ggPos = 0, ggType = 0;  // 0 Game Genie, 1 Pro Action Replay
static void openCodeEditor() { for (int i = 0; i < 8; i++) ggLetters[i] = 0; ggPos = 0; ggType = 0; mode = "ggedit"; }
static string ggEditCode() {
  string s; for (int i = 0; i < 8; i++) { if (ggType == 0 && i == 4) s += '-'; s += HEX[ggLetters[i]]; }
  return s;
}
static void closeCodeEditor() { mode = "menu"; openCheatsMenu(); }
static void handleCodeEditor(const Nav& ev) {
  if (ev.pressed & B_LEFT) ggPos = std::max(0, ggPos - 1);
  if (ev.pressed & B_RIGHT) ggPos = std::min(7, ggPos + 1);
  if (ev.up) ggLetters[ggPos] = (ggLetters[ggPos] + 1) % 16;
  if (ev.down) ggLetters[ggPos] = (ggLetters[ggPos] + 15) % 16;
  if (ev.pressed & B_Y) ggType ^= 1;
  if (ev.pressed & B_A) {
    string code = ggEditCode();
    if (!codeValid(code)) { showToast("That code is not valid"); return; }
    running->cheats.push_back(Cheat{code, "", true}); applyCheats(); saveCheats(); showToast("Code " + code + " added");
    closeCodeEditor();
  } else if (ev.pressed & B_B) closeCodeEditor();
}

/* ---------------- ROM folder picker ---------------- */
static string pickPath;
static vector<string> pickDirs;
static int pickSel = 0, pickTop = 0, pickGames = 0;
static string parentDir(const string& p) {
  size_t k = p.find_last_of('/');
  if (k == string::npos || k <= 5) return "sdmc:/";
  return p.substr(0, k);
}
static void pickLoad(const string& path) {
  pickPath = path; pickDirs.clear(); pickSel = 0; pickTop = 0; pickGames = 0;
  string open = path == "sdmc:" ? "sdmc:/" : path;
  if (DIR* d = opendir(open.c_str())) {
    while (struct dirent* e = readdir(d)) {
      string n = e->d_name;
      if (n.empty() || n[0] == '.') continue;
      if (e->d_type == DT_DIR) pickDirs.push_back(n);
      else if (pickingCoverFolder?cover_policy::format(n)<3:(isRomExt(n) || hasExt(n, ".zip"))) pickGames++;
    }
    closedir(d);
  }
  std::sort(pickDirs.begin(), pickDirs.end(), [](const string& a, const string& b) { return naturalLess(a, b); });
}
static bool pickAtRoot() { return pickPath == "sdmc:/" || pickPath == "sdmc:"; }
// Rows: "Use this folder", then ".." (unless at the root), then subfolders.
static int pickRows() { return 1 + (pickAtRoot() ? 0 : 1) + (int)pickDirs.size(); }
static void openFolderPicker(bool add=false) {
  pickingCoverFolder=false;addingRomFolder=add;
  struct stat st;
  pickLoad(stat(romDir.c_str(), &st) == 0 ? romDir : "sdmc:/");
  mode = "folders";
}

/* ---------------- Settings screen ---------------- */
struct SettingItem { string label; std::function<vector<string>()> choices; std::function<int()> get; std::function<void(int)> set; };
static vector<SettingItem> settingsItems() {
  return {
      {"ROM folder", [] { return vector<string>{romDir.rfind("sdmc:", 0) == 0 ? romDir.substr(5) : romDir}; }, [] { return 0; }, [](int) { openFolderPicker(); }},
      {"Add ROM folder", [] {return vector<string>{std::to_string(extraRomDirs.size())+" extra"};}, [] {return 0;}, [](int){openFolderPicker(true);}},
      {"Clear extra ROM folders", [] {return vector<string>{"Clear list"};}, [] {return 0;}, [](int){extraRomDirs.clear();saveSettings();scan();showToast("Extra folder list cleared; files unchanged");}},
      {"Aspect", [] { return ASPECTS; }, [] { return aspectMode; }, [](int v) { aspectMode = v; }},
      {"Scale", [] { return SCALES; }, [] { return scaleMode; }, [](int v) { scaleMode = v; }},
      {"Overscan", [] { return CROPS; }, [] { return cropMode; }, [](int v) { cropMode = v; }},
      {"Signal", [] { return SIGNALS; }, [] { return signalMode; }, [](int v) { signalMode = v; }},
      {"Shader", [] { return SHADERS; }, [] { return shader; }, [](int v) { shader = v; }},
      {"Brightness boost", [] { return BOOSTS; }, [] { return boost; }, [](int v) { boost = v; }},
      {"Curvature", [] { return CURVATURES; }, [] { return curvature; }, [](int v) { curvature = v; }},
      {"Corner radius", [] { return CORNER_RADII; }, [] { return cornerRadius; }, [](int v) { cornerRadius = v; }},
      {"Corner softness", [] { return CORNER_SOFTNESS; }, [] { return cornerSoftness; }, [](int v) { cornerSoftness = v; }},
      {"Vignette", [] { return VIGNETTES; }, [] { return vignette; }, [](int v) { vignette = v; }},
      {"Vignette size", [] { return VIGNETTE_SIZES; }, [] { return vignetteSize; }, [](int v) { vignetteSize = v; }},
      {"Stats", [] { return STATS_LEVELS; }, [] { return statsLevel; }, [](int v) { statsLevel = v; }},
      {"Performance profile", [] { return PERFORMANCE_PROFILES; }, [] { return performanceProfile; }, [](int v) { performanceProfile = v; governor.reset(); }},
      {"Run-ahead", [] { return RUN_AHEADS; }, [] { return runAhead; }, [](int v) { runAhead = v; }},
      {"Frame delay (60 Hz)", [] { return FRAME_DELAYS; }, [] { return frameDelay; }, [](int v) { frameDelay = v; }},
      {"Audio buffer", [] { return AUDIO_BUFFERS; }, [] { return audioBuffer; }, [](int v) { audioBuffer = v; }},
      {"Display", [] { return DISPLAY_MODES; }, [] { return displayMode; }, [](int v) { displayMode = v; gfx::setHighRefresh(v == 1); }},
      {"Rewind (hold ZL)", [] { return OFF_ON; }, [] { return rewindOn ? 1 : 0; }, [](int v) { rewindOn = v != 0; }},
      {"Fast-forward", [] { return FF_MODES; }, [] { return ffMode; }, [](int v) { ffMode = v; }},
      {"Fast-forward speed", [] { return FF_SPEEDS; }, [] { return ffSpeed; }, [](int v) { ffSpeed = v; }},
      {"Auto-load save state", [] { return OFF_ON; }, [] { return autoLoadState ? 1 : 0; }, [](int v) { autoLoadState = v != 0; }},
      {"Pause in menu", [] { return OFF_ON; }, [] { return pauseInMenu ? 1 : 0; }, [](int v) { pauseInMenu = v != 0; }},
      {"Dim game in menu", [] { return OFF_ON; }, [] { return dimInMenu ? 1 : 0; }, [](int v) { dimInMenu = v != 0; }},
      {"Volume", [] { return VOLUMES; }, [] { return volumeLevel; }, [](int v) { volumeLevel = v; }},
      {"Audio filter", [] { return FILTERS; }, [] { return audioFilter; }, [](int v) { audioFilter = v; }},
      {"Turbo speed", [] { return TURBO_RATES; }, [] { return turboRate; }, [](int v) { turboRate = v; }},
      {"Button mapping for", [] { return vector<string>{"Player 1", "Player 2"}; }, [] { return remapPlayer; }, [](int v) { remapPlayer = v; }},
      {"Controller preset", [] {return CONTROLLER_PRESETS;}, [] {return 0;}, [](int v){applyControllerPreset(curMap(),v);}},
      {"Analog deadzone", [] {return DEADZONES;}, [] {return analogDeadzone;}, [](int v){analogDeadzone=v;}},
      {"Switch A button", [] { return FACE_CHOICES; }, [] { return curMap()[0]; }, [](int v) { curMap()[0] = v; }},
      {"Switch B button", [] { return FACE_CHOICES; }, [] { return curMap()[1]; }, [](int v) { curMap()[1] = v; }},
      {"Switch X button", [] { return FACE_CHOICES; }, [] { return curMap()[2]; }, [](int v) { curMap()[2] = v; }},
      {"Switch Y button", [] { return FACE_CHOICES; }, [] { return curMap()[3]; }, [](int v) { curMap()[3] = v; }},
      {"Switch L button", [] { return FACE_CHOICES; }, [] { return curMap()[4]; }, [](int v) { curMap()[4] = v; }},
      {"Switch R button", [] { return FACE_CHOICES; }, [] { return curMap()[5]; }, [](int v) { curMap()[5] = v; }},
  };
}
static vector<SettingItem> SETTINGS_ITEMS;
static bool resetArmed = false;
static void rebuildSettings() {
  SETTINGS_ITEMS.clear();
  SETTINGS_ITEMS.push_back({"Advanced options", [] { return vector<string>{"Hidden", "Shown"}; }, [] { return showAdvanced ? 1 : 0; },
                            [](int v) { showAdvanced = v != 0; rebuildSettings(); }});
  SETTINGS_ITEMS.push_back({"Resume last game", [] {return vector<string>{"Press A"};}, [] {return 0;}, [](int){if(!recentGames.empty())for(const auto& r:allRoms)if(r.name==recentGames.front()){Rom copy=r;startGame(copy,true);return;}showToast("No recent game available");}});
  SETTINGS_ITEMS.push_back({"Library sort", [] {return LIBRARY_SORTS;}, [] {return librarySort;}, [](int v){librarySort=v;orderRoms();}});
  SETTINGS_ITEMS.push_back({"Screen border", [] {return BORDER_NAMES;}, [] {return screenBorder;}, [](int v){screenBorder=v;}});
  SETTINGS_ITEMS.push_back({"UI theme", [] {return THEME_NAMES;}, [] {return themeIndex;}, [](int v){themeIndex=v;applyTheme();saveSettings();}});
  SETTINGS_ITEMS.push_back({"Accent colour", [] {return ACCENT_NAMES;}, [] {return accentIndex;}, [](int v){accentIndex=v;applyTheme();saveSettings();}});
  for (auto& it : settingsItems())
    if (showAdvanced || !contains(ADVANCED_SETTINGS, it.label)) SETTINGS_ITEMS.push_back(it);
  if (showAdvanced)
    SETTINGS_ITEMS.push_back({"Reset all settings", [] { return vector<string>{"Press A", "Press A again to confirm"}; }, [] { return resetArmed ? 1 : 0; },
                              [](int v) { if (resetArmed) { resetArmed = false; resetAllSettings(); gfx::setHighRefresh(displayMode == 1); rebuildSettings(); settingsIndex = 0; settingsTop = 0; showToast("All settings reset to defaults"); } else resetArmed = true; }});
  if (settingsIndex > (int)SETTINGS_ITEMS.size()) settingsIndex = (int)SETTINGS_ITEMS.size();
}

/* ---------------- Drawing helpers ---------------- */
static const float LH = 1.15f;  // CSS "normal" line height, approximately
// Text vertically centred in a box.
static float textMid(const string& s, float x, float y, float h, float size, Color c, gfx::Font f = gfx::REGULAR) {
  return gfx::text(s, x, y + (h - size * 1.25f) / 2, size, c, f);
}
// "< value >" selector, right-aligned at xr; returns its width.
static float valueBox(const string& t, float xr, float y, float h, float size, bool active) {
  Color c = col(active ? C_TEXT : C_DIM);
  float wl = gfx::textWidth("<", size, gfx::BOLD), wt = gfx::textWidth(t, size), wr = gfx::textWidth(">", size, gfx::BOLD);
  float total = wl + 10 + wt + 10 + wr, x = xr - total;
  textMid("<", x, y, h, size, c, gfx::BOLD);
  textMid(t, x + wl + 10, y, h, size, c);
  textMid(">", x + wl + 10 + wt + 10, y, h, size, c, gfx::BOLD);
  return total;
}
static vector<string> wrap(const string& text, float width, float size, gfx::Font f = gfx::REGULAR) {
  vector<string> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos); if (e == string::npos) e = text.size();
    string para = text.substr(pos, e - pos), line;
    size_t i = 0;
    // Keep leading spaces (indented lines in messages).
    while (i < para.size() && para[i] == ' ') { line += ' '; i++; }
    string lead = line;
    while (i <= para.size()) {
      size_t sp = para.find(' ', i); if (sp == string::npos) sp = para.size();
      string word = para.substr(i, sp - i);
      string next = line.size() > lead.size() ? line + " " + word : line + word;
      if (line.size() > lead.size() && gfx::textWidth(next, size, f) > width) { out.push_back(line); line = word; }
      else line = next;
      i = sp + 1;
    }
    out.push_back(line);
    pos = e + 1;
  }
  return out;
}
static void panelHeader(float x, float y, const string& title, const string& note) {
  gfx::rect(x, y + 4, 10, 26, col(C_ACCENT), 2);
  float w = textMid(title, x + 20, y, 34, 26, col(C_TEXT), gfx::BOLD);
  if (!note.empty()) textMid(note, x + 20 + w + 12, y + 3, 34, 16, col(C_DIM));
}
static void scrollbar(float x, float y, float h, int total, int vis, int topIndex) {
  if (total <= vis) return;
  gfx::rect(x, y, 8, h, col(C_LINE), 4);
  float th = std::max(24.0f, roundf(h * vis / total));
  gfx::rect(x, y + roundf((h - th) * topIndex / (total - vis)), 8, th, col(C_DIM), 4);
}

/* ---------------- Screens ---------------- */
static const vector<string> BROWSER_BUTTONS = {"Settings", "Refresh", "Exit"};
static const char* BROWSER_KEYS[] = {"Y", "X", "B"};  // gamepad shortcut for each button
static const int ROW_H = 43;
static const float LIST_HEAD = 26;  // column headings above the game list
static int maxRows() { return std::max(3, (int)((gfx::UI_H - 36 - 92 - LIST_HEAD - 12 - 40 - 18 - 6) / ROW_H)); }

static u64 logoButtons=0;
static constexpr float LIB_EXTRA=18;
static constexpr float NAV_Y=76, GRID_TOP=118;
static constexpr float LIST_TOP=122+26;
static constexpr float GRID_GAP=12;
static constexpr float SEARCH_X=530, SEARCH_Y=14, SEARCH_W=680, SEARCH_H=42, CLEAR_W=176;
static constexpr float VIEW_X=gfx::UI_W-232;
static bool touchHeld=true;
static bool inside(float x,float y,float l,float t,float w,float h){return x>=l&&x<l+w&&y>=t&&y<t+h;}
static void clearLibrarySearch(){libraryQuery.clear();orderRoms();selected=top=0;focusArea=roms.empty()?"tabs":"list";exitArmed=0;showToast("Search cleared");}
static void libraryLogo(float x,float y,const string& note){
  gfx::rect(x,y+5,54,30,gfx::rgb(0xd5cfe1),12);
  auto d=gfx::rgb((logoButtons&(B_UP|B_DOWN|B_LEFT|B_RIGHT|B_SU|B_SD|B_SL|B_SR))?0x7960b8:0x484253);
  gfx::rect(x+10,y+12,6,16,d,1);gfx::rect(x+5,y+17,16,6,d,1);
  const uint32_t colors[]={0x73ae78,0xdc777e,0x7b8bd5,0xe2c56e};
  const u64 keys[]={B_X,B_A,B_Y,B_B};
  const float bx[]={37,44,30,37},by[]={9,16,16,23};
  int pulse=int(nowMs()/420.0)%4;
  for(int i=0;i<4;i++){
    bool active=(logoButtons&keys[i])||pulse==i;
    if(active)gfx::rect(x+bx[i]-2,y+by[i]-2,10,10,gfx::rgb(colors[i],0.38f),5);
    gfx::rect(x+bx[i],y+by[i],6,6,gfx::rgb(colors[i]),3);
  }
  float tx=x+64;
  float w=gfx::text("NS2xSNES",tx,y-2,26,col(C_TEXT),gfx::BOLD,0.18f);
  float tail=gfx::text("9x",tx+w,y-2,26,col(C_ACCENT),gfx::BOLD,0.18f);
  float stripe=(w+tail)/4;
  const uint32_t bands[]={0x7b8bd5,0x73ae78,0xe2c56e,0xdc777e};
  for(int i=0;i<4;i++)gfx::rect(tx+i*stripe,y+30,stripe-2,3,gfx::rgb(bands[i]),1);
  gfx::text("SUPER 16-BIT EMULATION",tx,y+35,12,col(C_DIM));
  if(!note.empty())gfx::text(note,gfx::UI_W-20-gfx::textWidth(note,16),46,16,col(C_DIM));
}
static void librarySearchBar(){
  const float x=SEARCH_X,y=SEARCH_Y,w=SEARCH_W,h=SEARCH_H;
  gfx::rect(x,y,w,h,col(C_BG),8,libraryQuery.empty()?1:2,col(libraryQuery.empty()?C_LINE:C_ACCENT));
  gfx::rect(x+6,y+5,32,32,col(C_SEL),5);
  textMid("-",x+6+(32-gfx::textWidth("-",24,gfx::BOLD))/2,y+5,32,24,col(C_TEXT),gfx::BOLD);
  gfx::rect(x+48,y+10,16,16,col(C_BG),8,2,col(C_DIM));
  for(int i=0;i<7;i++)gfx::rect(x+60+i,y+23+i,3,3,col(C_DIM),1);
  string q=libraryQuery.empty()?"Search games...":libraryQuery;
  textMid(gfx::ellipsize(q,w-88-(libraryQuery.empty()?0:CLEAR_W),24),x+78,y,h,24,col(libraryQuery.empty()?C_DIM:C_TEXT),gfx::BOLD);
  if(!libraryQuery.empty()){
    float cx=x+w-CLEAR_W;
    gfx::rect(cx,y+4,CLEAR_W-4,h-8,col(C_SEL),5,1,col(C_ACCENT));
    string label="ZL + - Clear Search";
    textMid(label,cx+(CLEAR_W-4-gfx::textWidth(label,16,gfx::BOLD))/2,y+4,h-8,16,col(C_TEXT),gfx::BOLD);
  }
}
static void libraryToolbar(bool grid){
  float x=VIEW_X;
  gfx::rect(x,NAV_Y,100,34,col(grid?C_BG:C_SEL),5);
  gfx::rect(x+104,NAV_Y,108,34,col(grid?C_SEL:C_BG),5);
  textMid("+ List",x+(100-gfx::textWidth("+ List",20))/2,NAV_Y,34,20,col(C_TEXT));
  textMid("+ Grid",x+104+(108-gfx::textWidth("+ Grid",20))/2,NAV_Y,34,20,col(C_TEXT));
}
static float categoryTabs(float x,float y){
  gfx::rect(x,y,42,34,col(C_BG),6,1,col(C_LINE));textMid("ZL",x+9,y,34,17,col(C_TEXT));x+=50;
  for(size_t i=0;i<LIST_VIEWS.size();++i){
    bool on=(int)i==listView;float w=gfx::textWidth(LIST_VIEWS[i],17)+32;
    gfx::rect(x,y,w,34,col(on?C_SEL:C_BG),16,1,col(on?C_ACCENT:C_LINE));
    textMid(LIST_VIEWS[i],x+16,y,34,17,col(on?C_TEXT:C_DIM));x+=w+8;
  }
  gfx::rect(x,y,42,34,col(C_BG),6,1,col(C_LINE));textMid("ZR",x+9,y,34,17,col(C_TEXT));return x+50;
}
static void pageLabel(float right,float y,int size){
  string p="Page "+std::to_string(roms.empty()?0:selected/size+1)+" / "+std::to_string((roms.size()+size-1)/size);
  float pw=gfx::textWidth(p,24,gfx::BOLD),x=right-pw-104;
  gfx::rect(x,y,42,34,col(C_SEL),6,1,col(C_ACCENT));textMid("L",x+15,y,34,22,col(C_TEXT));
  textMid(p,x+52,y,34,24,col(C_TEXT),gfx::BOLD);
  gfx::rect(right-42,y,42,34,col(C_SEL),6,1,col(C_ACCENT));textMid("R",right-28,y,34,22,col(C_TEXT));
}
static void drawGridFooter(){
  const char* labels[]={"Settings","Refresh","Exit"};
  float bw=(gfx::UI_W-70)/3.0f;
  for(int i=0;i<3;i++){float x=25+i*(bw+10);string label=i==2&&exitArmed?"Confirm exit":labels[i];gfx::rect(x,gfx::UI_H-48,bw,40,col(i==2&&exitArmed?C_SEL:C_BG),8,1,col(C_LINE));textMid(label,x+(bw-gfx::textWidth(label,22))/2,gfx::UI_H-48,40,22,col(C_TEXT));}
}
static void drawGridBrowser() {
  coverLoadsRemaining=1;
  const float x=0,y=0,w=gfx::UI_W,h=gfx::UI_H;
  const float gap=12,tw=(w-40-3*gap)/4,th=(h-150-LIB_EXTRA-2*gap)/3,aw=tw-12,ah=th-34;
  gfx::rect(x,y,w,h,col(C_PANEL));
  libraryLogo(x+20,y+18,std::to_string(roms.size())+" games");
  gfx::text("v" APP_VERSION,w-20-gfx::textWidth("v" APP_VERSION,18),y+26,18,col(C_DIM));
  float tx=categoryTabs(20,NAV_Y);
  libraryToolbar(true);
  pageLabel(gfx::UI_W-250,NAV_Y,12);
  librarySearchBar();
  int start=library::pageTop(selected,4,3);
  for(int i=0;i<12 && start+i<(int)roms.size();++i){
    const Rom& r=roms[start+i];float cx=x+20+(i%4)*(tw+gap),cy=y+GRID_TOP+(i/4)*(th+gap);
    bool active=start+i==selected;
    gfx::rect(cx,cy,tw,th,col(active?C_SEL:C_BG),8,active?3:1,col(active?C_ACCENT:C_LINE));
    auto& cover=gameCover(r.dir,r.name);
    if(cover.image){auto f=cover_policy::fit(cover.w,cover.h,aw,ah);gfx::imageDraw(cover.image,cx+6+f.x,cy+4+f.y,f.w,f.h);}
    else {gfx::rect(cx+6,cy+4,aw,ah,col(C_PANEL),6,1,col(C_LINE));gfx::text("SNES",cx+(tw-gfx::textWidth("SNES",30,gfx::BOLD))/2,cy+4+ah/2-15,30,col(C_DIM),gfx::BOLD);}
    gfx::text(gfx::ellipsize(stripNes(r.name),aw,20),cx+6,cy+ah+8,20,col(C_TEXT));
  }
  if(roms.empty())gfx::text("No matching games",x+20,y+130+LIB_EXTRA,24,col(C_TEXT));
  drawGridFooter();
}
static void drawBrowser() {
  if(libraryGrid){drawGridBrowser();return;}
  const float winW = gfx::UI_W, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
  top=roms.empty()?0:(selected/maxRows())*maxRows();
  bool scroll = (int)roms.size() > VIS;
  string empty;
  if (roms.empty()) {
    if (!libraryQuery.empty()) empty = "No matching games. Press Minus to edit search.";
    else if (listView == 1) empty = "No favourites yet.\n\nWhile playing, open the menu (ZL + ZR) and set Favourite to On.";
    else if (listView == 2) empty = "No recently played games yet.";
    else empty = "No games found in\n\n    " + romDir + "\n\nCopy .sfc/.smc (or zipped) games there, for example over FTP with OunceShell, then choose Refresh. The folder can be changed in Settings.";
  }
  float listInnerW = innerW * 0.65f - (scroll ? 16 : 0);
  coverLoadsRemaining=1;
  vector<string> emptyLines = empty.empty() ? vector<string>() : wrap(empty, listInnerW - 32, 17);
  float listH = gfx::UI_H - padY * 2 - 46 - 46 - (roms.empty() ? 0 : LIST_HEAD) - 12 - 40 - LIB_EXTRA;
  float head = roms.empty() ? 0 : LIST_HEAD;
  float x = 0, y = 0;
  gfx::rect(x, y, winW, gfx::UI_H, col(C_PANEL));
  float cx = x + padX, cy = y + padY;
  libraryLogo(cx,cy,std::to_string(roms.size())+(roms.size()==1?" game":" games"));
  cy = NAV_Y;
  libraryToolbar(false);
  float tx=categoryTabs(cx,cy);
  pageLabel(cx+listInnerW-18,cy,maxRows());
  librarySearchBar();
  cy += 46;
  // Column headings
  // Columns from the right: Time played, Recent, Favourite (centred tags).
  const float TIME_W = 105, REC_W = 80, FAV_W = 100;
  const float colR = cx + listInnerW - 18, recC = colR - TIME_W - REC_W / 2, favC = recC - REC_W / 2 - FAV_W / 2;
  if (!roms.empty()) {
    gfx::text("Game", cx + 22, cy + 1, 14, col(C_DIM), gfx::BOLD);
    float hw = gfx::textWidth("Time played", 14, gfx::BOLD);
    gfx::text("Time played", colR - hw, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Recent", recC - gfx::textWidth("Recent", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    gfx::text("Favourite", favC - gfx::textWidth("Favourite", 14, gfx::BOLD) / 2, cy + 1, 14, col(C_DIM), gfx::BOLD);
    cy += head;
  }
  // List
  gfx::rect(cx, cy, listInnerW, listH, col(C_BG));
  if (roms.empty()) {
    float ly = cy + 16;
    for (auto& l : emptyLines) { gfx::text(l, cx + 16, ly, 17, col(C_TEXT)); ly += 17 * 1.45f; }
  } else {
    gfx::clip((int)cx, (int)cy, (int)listInnerW, (int)listH);
    for (int i = 0; i < VIS && top + i < (int)roms.size(); i++) {
      const Rom& r = roms[top + i];
      float ry = cy + 6 + i * ROW_H, rx = cx + 6, rw = listInnerW - 12;
      bool active = top + i == selected && focusArea == "list";
      if (!active && i%2) gfx::rect(rx, ry, rw, ROW_H, col(C_PANEL));
      if (active) { gfx::rect(rx, ry, rw, ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, ROW_H, col(C_ACCENT), 2); }
      bool fav = contains(favourites, r.name), rec = contains(recentGames, r.name);
      string played = playLabel(r.name);
      if (!played.empty()) { float pw = gfx::textWidth(played, 16); textMid(played, rx + rw - 12 - pw, ry, ROW_H, 16, col(C_DIM)); }
      if (fav) {
        float tw = gfx::textWidth("Favourite", 14) + 16;
        gfx::rect(favC - tw / 2, ry + 9, tw, 22, gfx::rgb(0xf2c94c), 4);
        textMid("Favourite", favC - tw / 2 + 8, ry + 9, 22, 14, gfx::rgb(0x1a1300));
      }
      if (rec) {
        float tw = gfx::textWidth("Recent", 14) + 16;
        gfx::rect(recC - tw / 2, ry + 9, tw, 22, Color{0, 0, 0, 0}, 4, 1, col(C_LINE));
        textMid("Recent", recC - tw / 2 + 8, ry + 9, 22, 14, col(C_DIM));
      }
      textMid(gfx::ellipsize(stripNes(r.name), favC - FAV_W / 2 - rx - 28, 24), rx + 16, ry, ROW_H, 24, col(C_TEXT));
    }
    gfx::noClip();
    scrollbar(cx + listInnerW + 8, cy, listH, (int)roms.size(), VIS, std::min(top,std::max(0,(int)roms.size()-VIS)));
  }
  // Selected game cover preview, using the shared bounded cache.
  float px=cx+innerW*0.65f+12, pw=innerW*0.35f-12;
  gfx::rect(px,cy,pw,listH,col(C_BG),8);
  if(selected>=0 && selected<(int)roms.size()){
    const Rom& r=roms[selected];
    float aw=pw-32, ah=std::max(1.0f,listH-144);
    auto& cover=gameCover(r.dir,r.name);
    float dy=cy+ah+32;
    if(cover.image){auto f=cover_policy::fit(cover.w,cover.h,aw,ah);gfx::imageDraw(cover.image,px+16+f.x,cy+16,f.w,f.h);dy=cy+16+f.h+16;}
    else {gfx::rect(px+16,cy+16,aw,ah,col(C_PANEL),6,1,col(C_LINE));gfx::text("SNES",px+(pw-gfx::textWidth("SNES",32,gfx::BOLD))/2,cy+16+ah/2-16,32,col(C_DIM),gfx::BOLD);}
    gfx::text(gfx::ellipsize(stripNes(r.name),aw,24),px+16,dy,24,col(C_TEXT));
    gfx::text(contains(favourites,r.name)?"Favourite":"Super Nintendo",px+16,dy+38,20,col(C_DIM));
    string played=playLabel(r.name);
    gfx::text(played.empty()?"Not played":"Playtime: "+played,px+16,dy+70,20,col(C_DIM));
  }
  cy += listH + 12;
  // Buttons
  float bw = (innerW - 30) / 3;
  for (int i = 0; i < 3; i++) {
    bool on = focusArea == "buttons" && i == btnIndex;
    float bx = cx + 5 + i * (bw + 10);
    string label = i == 2 && exitArmed ? "Confirm exit" : BROWSER_BUTTONS[i];
    gfx::rect(bx, cy, bw, 40, col(on ? C_SEL : C_BG), 8, 1, col(on ? C_ACCENT : C_LINE));
    float tw = gfx::textWidth(label, 18), kw = 24, lx = bx + (bw - tw - kw - 8) / 2;
    gfx::rect(lx, cy + 8, kw, kw, col(C_LINE), kw / 2);  // shortcut button badge
    float kt = gfx::textWidth(BROWSER_KEYS[i], 15);
    textMid(BROWSER_KEYS[i], lx + (kw - kt) / 2, cy + 8, kw, 15, col(C_TEXT));
    textMid(label, lx + kw + 8, cy, 40, 18, col(i == 2 && exitArmed ? C_ACCENT : C_TEXT));
  }
  gfx::text("v" APP_VERSION, x + winW - padX - gfx::textWidth("v" APP_VERSION, 18), y + padY + 8, 18, col(C_DIM));
}

static const int SET_ROW_H = 44;
static void drawFolderPicker() {
  const float winW = 960, padX = 20, padY = 18, rowH = 34;
  int total = pickRows();
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H * 0.9 - 36 - 50 - 40 - 14) / rowH)));
  if (pickSel < pickTop) pickTop = pickSel;
  if (pickSel >= pickTop + vis) pickTop = pickSel - vis + 1;
  bool scroll = total > vis;
  float listW = winW - 2 * padX - (scroll ? 16 : 0), listH = vis * rowH + 12;
  float winH = padY * 2 + 34 + 12 + 30 + listH;
  float x = (gfx::UI_W - winW) / 2, y = roundf((gfx::UI_H - winH) / 2);
  gfx::rect(x, y, winW, winH, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + padX, cy = y + padY;
  panelHeader(cx, cy, pickingCoverFolder?"Cover art folder":"ROM folder", "A: open   B: cancel");
  cy += 34 + 12;
  string shown = pickPath.rfind("sdmc:", 0) == 0 ? pickPath.substr(5) : pickPath;
  if (shown.empty()) shown = "/";
  string note = std::to_string(pickGames) + (pickingCoverFolder?" images here":(pickGames == 1 ? " game here" : " games here"));
  textMid(gfx::ellipsize(shown, winW - 2 * padX - 200, 18, gfx::MONO), cx + 4, cy, 26, 18, col(C_TEXT), gfx::MONO);
  textMid(note, cx + winW - 2 * padX - gfx::textWidth(note, 16) - 4, cy, 26, 16, col(C_DIM));
  cy += 30;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis && pickTop + k < total; k++) {
    int i = pickTop + k;
    bool active = i == pickSel;
    float rx = cx + 6, ry = cy + 6 + k * rowH, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, rowH, col(C_SEL), 6); gfx::rect(rx, ry, 4, rowH, col(C_ACCENT), 2); }
    string label; Color c = col(C_TEXT);
    int up = pickAtRoot() ? 0 : 1;
    if (i == 0) { label = addingRomFolder ? "Add this folder" : "Use this folder"; c = gfx::rgb(0x6ee26e); }
    else if (up && i == 1) label = ".. (up one level)";
    else label = pickDirs[i - 1 - up] + "/";
    textMid(gfx::ellipsize(label, rw - 32, 18), rx + 16, ry, rowH, 18, c, i == 0 ? gfx::BOLD : gfx::REGULAR);
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, pickTop);
}
static int settingsTab=0;
static void settingsTabs(){
  const char* names[]={"Settings","Cover Art","Credits"};
  float x=20;gfx::rect(x,18,42,40,col(C_BG),6);textMid("L",x+15,18,40,22,col(C_TEXT));x+=54;
  for(int i=0;i<3;i++){float w=200;gfx::rect(x,18,w,40,col(i==settingsTab?C_SEL:C_BG),8,1,col(i==settingsTab?C_ACCENT:C_LINE));textMid(names[i],x+(w-gfx::textWidth(names[i],24))/2,18,40,24,col(C_TEXT));x+=w+10;}
  gfx::rect(x,18,42,40,col(C_BG),6);textMid("R",x+14,18,40,22,col(C_TEXT));
}
static void drawSettings() {
  const float winW = gfx::UI_W, padX = 20, padY = 18, innerW = winW - 2 * padX;
  int n = (int)SETTINGS_ITEMS.size(), total = n + 1;
  int vis = std::max(5, std::min(total, (int)((gfx::UI_H - 36 - 46 - 12 - 44) / SET_ROW_H)));
  if (settingsIndex < settingsTop) settingsTop = settingsIndex;
  if (settingsIndex >= settingsTop + vis) settingsTop = settingsIndex - vis + 1;
  bool scroll = total > vis;
  float listW = innerW - (scroll ? 16 : 0), listH = vis * SET_ROW_H + 12;
  settingsTop=std::max(0,std::min(settingsTop,total-vis));
  float x=0,y=0;
  gfx::rect(x,y,winW,gfx::UI_H,col(C_PANEL));
  float cx = x + padX, cy = y + padY;
  settingsTabs();
  cy += 34 + 12;
  gfx::rect(cx, cy, listW, listH, col(C_BG), 10, 1, col(C_LINE));
  for (int k = 0; k < vis; k++) {
    int i = settingsTop + k;
    if (i >= total) break;
    bool active = i == settingsIndex;
    float rx = cx + 6, ry = cy + 6 + k * SET_ROW_H, rw = listW - 12;
    if (active) { gfx::rect(rx, ry, rw, SET_ROW_H, col(C_SEL), 6); gfx::rect(rx, ry, 4, SET_ROW_H, col(C_ACCENT), 2); }
    if (i < n) {
      vector<string> ch = SETTINGS_ITEMS[i].choices();
      float vw=0;
      if(!ch.empty()){int v=std::max(0,std::min((int)ch.size()-1,SETTINGS_ITEMS[i].get())); string val=gfx::ellipsize(ch[v],rw*0.48f-64,23); vw=valueBox(val,rx+rw-16,ry,SET_ROW_H,23,active);}
      textMid(gfx::ellipsize(SETTINGS_ITEMS[i].label,std::max(20.0f,rw-vw-48),23),rx+16,ry,SET_ROW_H,23,col(C_TEXT));
    } else textMid("Back", rx + 16, ry, SET_ROW_H, 23, col(C_TEXT));
  }
  scrollbar(cx + listW + 8, cy, listH, total, vis, settingsTop);
  const string hint="Tap or swipe to navigate   |   Left/Right Change   |   Tap here / B: Back";
  textMid(hint,(winW-gfx::textWidth(hint,20))/2,gfx::UI_H-38,34,20,col(C_DIM));
}

static void drawCredits(){
  gfx::rect(0,0,gfx::UI_W,gfx::UI_H,col(C_PANEL));settingsTabs();
  gfx::text("NS2xSNES9x  v" APP_VERSION,44,110,32,col(C_TEXT),gfx::BOLD);
  gfx::text("Development: blueshockz47",44,180,26,col(C_TEXT));
  gfx::text("Original SW2SNES: Rodpad (MIT, copyright 2026)",44,240,26,col(C_TEXT));
  gfx::text("Emulation: Snes9x and its contributors (non-commercial license)",44,300,24,col(C_TEXT));
  gfx::text("Switch 2 platform: libounce, ounce-devkit, kaiHEN and contributors",44,360,24,col(C_TEXT));
  gfx::text("Toolchain: devkitPro and open-source contributors",44,420,24,col(C_TEXT));
  gfx::text("Full license notices: LICENSE and licenses/ in the project source.",44,500,22,col(C_DIM));
  gfx::text("Tap tabs   |   Tap here / B: Back to Library",20,gfx::UI_H-34,22,col(C_DIM));
}
static int coverAction=0,unmatchedTop=0;
static bool showUnmatched=false;
static void coverCommand(int action){
  coverAction=action;
  if(action==0){pickingCoverFolder=true;addingRomFolder=false;struct stat st{};pickLoad(stat(coverDirectory().c_str(),&st)==0?coverDirectory():"sdmc:/");mode="folders";}
  else if(action==1){showUnmatched=false;beginCoverScan();}
  else {showUnmatched=!showUnmatched;unmatchedTop=0;}
}
static void drawCoverGuide(){
  float w=gfx::UI_W,h=gfx::UI_H;gfx::rect(0,0,w,h,col(C_PANEL));settingsTabs();
  gfx::text("Cover Art Manager",32,78,32,col(C_TEXT),gfx::BOLD);
  gfx::text(gfx::ellipsize(coverDirectory(),w-64,24),32,128,24,col(C_DIM));
  const char* labels[]={"Change Folder","Scan Cover Art","Unmatched Artwork"};
  for(int i=0;i<3;i++){float x=32+i*490;gfx::rect(x,180,472,58,col(i==coverAction?C_SEL:C_BG),8,2,col(i==coverAction?C_ACCENT:C_LINE));textMid(labels[i],x+(472-gfx::textWidth(labels[i],25))/2,180,58,25,col(C_TEXT));}
  gfx::rect(32,258,w-64,530,col(C_BG),10,1,col(C_LINE));
  string status=coverScanning?"Scanning: "+std::to_string(coverCheckCursor)+"/"+std::to_string(coverFiles.size())+" images":coverScanned?(coverRootOK?"Scan complete":"Folder not found"):"Ready to scan";
  gfx::text(status,56,280,26,col(C_ACCENT),gfx::BOLD);
  if(showUnmatched){gfx::text("Unmatched valid artwork: "+std::to_string(unmatchedCovers.size()),56,328,24,col(C_TEXT));for(int i=0;i<9&&unmatchedTop+i<(int)unmatchedCovers.size();i++)gfx::text(gfx::ellipsize(unmatchedCovers[unmatchedTop+i],w-112,22),56,370+i*42,22,col(C_TEXT));}
  else{
    const string rows[]={"ROMs in library: "+std::to_string(allRoms.size()),"Artwork detected: "+std::to_string(coverFiles.size()),"Valid images: "+std::to_string(coverValid),"Matched ROMs: "+std::to_string(coverMatched),"Missing matches: "+std::to_string(coverScanned?allRoms.size()-coverMatched:0),"Invalid / oversized images: "+std::to_string(coverInvalid)};
    for(int i=0;i<6;i++)gfx::text(rows[i],56,332+i*48,26,col(C_TEXT));
    gfx::text("Smart matching enabled | PNG, JPG, JPEG | Max 1024 x 1024",56,660,24,col(C_ACCENT));
    gfx::text("Subfolders included. Original files are never changed.",56,710,22,col(C_DIM));
  }
  textMid("Left/Right: Select   |   A: Open   |   Up/Down: Unmatched list   |   B / Tap here: Back",32,h-44,40,22,col(C_DIM));
}

static void drawStatsBar() {
  if (statsLevel <= 0 || !running) return;
  string t = statsText.empty() ? " " : statsText;
  float tw = gfx::textWidth(t, 18, gfx::MONO), gw = statsLevel == 3 ? GRAPH_W + 12 : 0;
  float h = std::max(18 * LH, statsLevel == 3 ? (float)GRAPH_H : 0) + 8;
  gfx::rect(16, 12, tw + gw + 20, h, gfx::Color{0, 0, 0, 0.6f}, 6);
  textMid(t, 26, 12, h, 18, gfx::rgb(0x9dfc9d), gfx::MONO);
  if (statsLevel == 3 && graphImage) gfx::imageDraw(graphImage, 26 + tw + 12, 12 + (h - GRAPH_H) / 2, GRAPH_W, GRAPH_H);
}

static void drawGame(bool dim) {
  if(running&&screenBorder){uint32_t c=screenBorder==1?0x202020:screenBorder==2?0x302a40:C_ACCENT;gfx::rect(0,0,gfx::UI_W,gfx::UI_H,col(c));}
  if (running) gfx::pictureDraw(picture, picX, picY, picW, picH, picCX, picCY, picCW, picCH);
  drawStatsBar();
  if (hintUntil && !dim) {
    string hint="ZL + ZR: Open Menu";
    float w=gfx::textWidth(hint,28,gfx::BOLD)+40;
    float x=(gfx::UI_W-w)/2,y=gfx::UI_H*0.5f-28;
    gfx::rect(x,y,w,56,gfx::rgb(0x101020,0.9f),8);
    gfx::text(hint,x+20,y+12,28,gfx::rgb(0xffffff),gfx::BOLD);
  }
}

static void drawMenu() {
  if (dimInMenu) gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  const float panelW = 460, pad = 26, rowH = 21 * LH + 16, gap = 3, headH = 26 * LH + 14;
  // As many rows as fit in 90% of the screen; the rest scroll.
  int n = (int)menuItems.size();
  int vis = std::max(4, std::min(n, (int)((gfx::UI_H * 0.9f - pad * 2 - headH) / (rowH + gap))));
  if (menuIndex < menuTop) menuTop = menuIndex;
  if (menuIndex >= menuTop + vis) menuTop = menuIndex - vis + 1;
  if (menuTop > n - vis) menuTop = std::max(0, n - vis);
  bool scroll = n > vis;
  float listH = vis * (rowH + gap);
  float h = pad * 2 + headH + listH;
  float x = (gfx::UI_W - panelW) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, panelW, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cx = x + pad, cy = y + pad, w = panelW - 2 * pad - (scroll ? 14 : 0);
  gfx::text(gfx::ellipsize(menuTitle, panelW - 2 * pad, 26, gfx::BOLD), cx, cy - 2, 26, col(C_TEXT), gfx::BOLD);
  cy += headH;
  for (int k = 0; k < vis && menuTop + k < n; k++) {
    int i = menuTop + k;
    bool active = i == menuIndex;
    float ry = cy + gap + k * (rowH + gap);
    if (active) { gfx::rect(cx, ry, w, rowH, col(C_SEL), 8); gfx::rect(cx, ry, 4, rowH, col(C_ACCENT), 2); }
    const MenuItem& m = menuItems[i];
    float vw = 0;
    if (m.option()) { vector<string> ch = m.choices(); int v = m.get(); vw = valueBox(gfx::ellipsize(ch[std::max(0, std::min((int)ch.size() - 1, v))],w*0.48f-42,18), cx + w - 14, ry, rowH, 18, active) + 16; }
    textMid(gfx::ellipsize(m.label, w - 32 - vw, 21), cx + 18, ry, rowH, 21, col(C_TEXT));
  }
  if (scroll) scrollbar(cx + w + 6, cy + gap, listH - gap, n, vis, menuTop);
}

static void drawCodeEditor() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.6f});
  string code = ggEditCode();
  string info = string(ggType == 0 ? "Game Genie" : "Pro Action Replay") + "   " + code + (codeValid(code) ? "" : "   (not valid)");
  string help = "Left/Right: move   Up/Down: digit   Y: code type   A: save   B: cancel";
  float cellsW = 8 * 52, w = std::max(cellsW, gfx::textWidth(help, 16)) + 56, h = 24 + 24 * LH + 16 + 54 + 14 + 2 * 16 * LH + 24;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  string title = "Enter cheat code";
  gfx::text(title, x + (w - gfx::textWidth(title, 24, gfx::BOLD)) / 2, y + 22, 24, col(C_TEXT), gfx::BOLD);
  float cy = y + 24 + 24 * LH + 16, cx = x + (w - cellsW) / 2;
  for (int i = 0; i < 8; i++) {
    float bx = cx + i * 52 + 4;
    gfx::rect(bx, cy, 44, 54, col(C_BG), 6, 2, col(i == ggPos ? C_ACCENT : C_LINE));
    string s(1, HEX[ggLetters[i]]);
    textMid(s, bx + (44 - gfx::textWidth(s, 30, gfx::MONO)) / 2, cy, 54, 30, col(C_TEXT), gfx::MONO);
  }
  cy += 54 + 14;
  gfx::text(info, x + (w - gfx::textWidth(info, 16)) / 2, cy, 16, col(C_DIM));
  gfx::text(help, x + (w - gfx::textWidth(help, 16)) / 2, cy + 16 * LH, 16, col(C_DIM));
}

static void drawMessage() {
  gfx::rect(0, 0, gfx::UI_W, gfx::UI_H, gfx::Color{0, 0, 0, 0.75f});
  vector<string> lines = wrap(messageText, 700, 22);
  float lh = 22 * 1.45f, w = 760, h = 30 * 2 + lines.size() * lh + 18 + 18 * LH;
  float x = (gfx::UI_W - w) / 2, y = roundf((gfx::UI_H - h) / 2);
  gfx::rect(x, y, w, h, col(C_PANEL), 14, 1, col(C_LINE));
  float cy = y + 30;
  for (auto& l : lines) { textMid(l, x + 30, cy, lh, 22, col(C_TEXT)); cy += lh; }
  gfx::text("Press A or B to continue", x + 30, cy + 18 - 2, 18, col(C_DIM));
}

static void drawToast() {
  if (!toastUntil) return;
  float w = gfx::textWidth(toastText, 20) + 36, h = 20 * LH + 20;
  float x = roundf((gfx::UI_W - w) / 2), y = gfx::UI_H - 28 - h;
  gfx::rect(x, y, w, h, gfx::rgb(0x0a0c12, 0.85f), 8);
  textMid(toastText, x + 18, y, h, 20, col(C_TEXT));
}

/* ---------------- Main loop ---------------- */

// One real (shown) SNES frame with input p/p2, run-ahead applied: the real frame
// runs without drawing and keeps its audio; then `ahead` frames run from a
// snapshot with the same input, the last one drawn, audio discarded, and the
// snapshot is restored. The picture shows the game `ahead` frames early.
// One real (shown) SNES frame. With run-ahead, the real frame runs without
// drawing (its audio kept) and a second emulator instance runs `ahead` frames
// from it with the same input; the picture comes from that instance.
static bool emulateShown(int p, int p2, int ahead, bool keepAudio) {
  if (!keepAudio) C->set_audio_discard(1);
  bool ok = C->frame(p, p2, ahead > 0 ? 0 : 1) >= 0;
  C->set_audio_discard(0);
  int n = C->audio_take();
  if (n && keepAudio) audio::push(C->audio(), n);
  if (ok && ahead > 0 && C->runahead(p, p2, ahead) < 0) { C->reconvert(); }
  return ok;
}

static void runFrame(u64 bits, u64 bits2, bool behindMenu) {
  double t0 = nowMs();
  if (!behindMenu) {
    bool combo = (bits & MENU_COMBO) == MENU_COMBO;
    if (combo && !comboLatch) { comboLatch = true; lastBits = bits; openMenu(); return; }
    if (!combo) comboLatch = false;
  }
  // Fast-forward (ZR hold or toggle) and rewind (hold ZL); both together open
  // the menu, so each acts only while the other is not held.
  bool both = (bits & MENU_COMBO) == MENU_COMBO;
  bool zr = !behindMenu && !both && (bits & HidNpadButton_ZR), zl = !behindMenu && !both && (bits & HidNpadButton_ZL);
  if (ffMode == 2 && zr && !ffPrevHeld) ffLatched = !ffLatched;
  ffPrevHeld = zr;
  if (ffMode != 2) ffLatched = false;
  bool wasFast = fastForward;
  fastForward = ffMode == 1 ? zr : ffMode == 2 ? ffLatched : false;
  if (wasFast != fastForward && !fastForward) emuAcc = 0;
  bool wasRewinding = rewinding;
  rewinding = rewindOn && zl && snapsReady;
  if (wasRewinding && !rewinding) C->audio_reset();  // resume sound cleanly from the rewound point

  if (rewinding) {
    // Step back one state per refresh and show it: the stepped-back state is
    // run for one frame (audio discarded) to draw its picture, then restored.
    if (rewind_count() > 0 && rewind_pop()) {
      C->set_audio_discard(1);
      C->frame(0, 0, 1);
      C->audio_take();
      C->set_audio_discard(0);
      rewind_reload();
      showFrame();
      frameDrawn = true;
    }
    workMs += nowMs() - t0;
    return;
  }

  // How many SNES frames this refresh: one at 60 Hz (locked to the display, audio
  // stretched by the rate difference); by the clock at 120 Hz / VRR.
  // At 120 Hz / VRR the display does not tick at the NES rate, so emulation
  // follows the clock: the exact time since the previous refresh (no
  // smoothing; with VRR long and short refreshes alternate and a smoothed
  // average ran games too fast).
  // 120 Hz / VRR: the main loop already waits for each SNES frame's time slot
  // (see vrrWait), so exactly one frame per loop. A 60 Hz display that is
  // not ticking near 60 Hz falls back to following the clock.
  int due = 1;
  bool hz120 = !highRefresh() && (C->is_pal() || tickMs < 15.5 || tickMs > 18);
  if (hz120) {
    due=timing_policy::due(emuAcc,loopGapMs,gameRate);
  }
  if (fastForward) {
    ffAcc += FF_FACTOR[ffSpeed] * (hz120 ? std::min(loopGapMs, 50.0) * gameRate / 1000 : 1);  // per shown frame
    due = (int)ffAcc; ffAcc -= due;
  }
  if (due <= 0) { workMs += nowMs() - t0; return; }

  int p = 0, p2 = 0;
  for (int i = 0; i < due; i++) {
    p = snesPad(bits); p2 = snesPad(behindMenu ? 0 : bits2, faceMap2);
    bool last = i == due - 1;
    bool ok;
    if (last) ok = emulateShown(p, p2, fastForward ? 0 : governor.ahead(performanceProfile, runAhead), !fastForward);
    else { C->set_audio_discard(1); ok = C->frame(p, p2, 0) >= 0; C->audio_take(); C->set_audio_discard(0); }
    if (!ok) { quitGame(); showMessage("Something went wrong:\nEmulation stopped.", [] { mode = "browser"; }); return; }
    emuCount++;
    // Rewind history: every frame.
    if (rewindOn && snapsReady) rewind_push();
  }
  {
    // Audio rate control (every frame): the queued amount, measured to the
    // sample and smoothed, is steered to the target by a PI loop around the
    // base ratio (60 Hz: display/NES rate difference; 120 Hz: 1, emulation
    // follows the clock). Corrections stay within +-0.5%, inaudible as pitch.
    double target = audioRecovery.target(AUDIO_TARGET_FRAMES[audioBuffer]);
    double queued = audio::queuedFrames();
    pendEma += (queued - pendEma) * 0.05;
    double err = (target - pendEma) / target;
    audioInteg = std::max(-0.004, std::min(0.004, audioInteg + err * 0.00002));
    double base = (hz120 || highRefresh()) ? 1.0 : gameRate / std::max(59.5, std::min(60.5, 1000.0 / tickMs));
    double r = base * (1 + std::max(-0.005, std::min(0.005, err * 0.004 + audioInteg)));
    if (++ratioTicks >= 4) { ratioTicks = 0; setAudioRatio(round(r * 1e5) / 1e5); }
    // Ran dry (a long stall): refill with a little silence instead of crackling.
    if (audio::available() && !fastForward && !rewinding && !behindMenu) {
      if (queued < 200) { ++starvationRefills; audioRecovery.starved(); audioInteg = 0; audio::prefill(audioRecovery.target(AUDIO_TARGET_FRAMES[audioBuffer]) / 2); pendEma = audio::queuedFrames(); }
      else audioRecovery.tick();
    }
  }
  emuFrames += 1;
  showFrame();
  frameDrawn = true;
  double t1 = nowMs();
  workMs += t1 - t0;
  // Battery save: every 4 s, write when the save RAM changed.
  if (running && running->battery && t1 - running->sramCheck >= 4000) { running->sramCheck = t1; saveSram(false); }
}

static string dispLabel() {
  char b[48];
  if (dispKnown) snprintf(b, sizeof b, "%u Hz%s", dispHz, dispVrr ? " VRR" : "");
  else snprintf(b, sizeof b, "%.0f Hz", 1000.0 / tickMs);
  return b;
}
// CPU load of the core this thread runs on, from the kernel's idle-tick
// counter (all work on that core, not only ours); -1 if unavailable.
static u64 idleLast = 0, tickLast = 0;
static double coreLoad() {
  u64 idle = 0, tick = armGetSystemTick();
  if (R_FAILED(svcGetInfo(&idle, InfoType_IdleTickCount, INVALID_HANDLE, svcGetCurrentProcessorNumber()))) return -1;
  double load = -1;
  if (tickLast && tick > tickLast && idle >= idleLast) load = 1.0 - (double)(idle - idleLast) / (double)(tick - tickLast);
  idleLast = idle; tickLast = tick;
  return load < 0 ? -1 : std::min(1.0, load);
}
static void updateStats(double t) {
  if (!running) return;
  if (mode != "running" && !(mode == "menu" && !pauseInMenu)) { statsAt = t; emuCount = 0; workMs = 0; for (double& x : phase) x = 0; phaseN = 0; return; }
  if (t - statsAt < 1000) return;
  double secs = (t - statsAt) / 1000, avg, low;
  videoFps(avg, low);
  const double divisor = std::max(1, phaseN);
  if (benchmarkFile) {
    fprintf(benchmarkFile, "%.3f,%.3f,%.3f,%.3f,%.2f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%ld,%d,%d,%d,%.5f\n", (t-startedAt)/1000, emuCount/secs, avg, low, 100*emuCount/secs/gameRate, phase[2]/divisor, phase[3]/divisor, gfx::gpuMs(), audio::available() ? pendEma/48.0 : -1.0, starvationRefills, (unsigned long long)(audio::droppedBuffers()-droppedBase), (long)rewind_bytes(), (int)rewind_count(), (int)fastForward, (int)rewinding, gameRate);
    fflush(benchmarkFile);
  }
  governor.sample((phase[2]+phase[3])/divisor/(1000.0/gameRate), mode == "running" && !fastForward && !rewinding && phaseN > 0);
  char buf[200];
  if (statsLevel == 1) snprintf(buf, sizeof buf, "%.1f fps", avg);
  else {
    double cpu = coreLoad(), gpu = gfx::gpuMs();
    char cpuS[24], gpuS[24];
    if (cpu >= 0) snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(cpu * 100)); else snprintf(cpuS, sizeof cpuS, "%d%%", (int)lround(100 * workMs / (t - statsAt)));
    if (gpu >= 0) snprintf(gpuS, sizeof gpuS, "%d%% (%.1f ms)", (int)lround(100 * gpu / tickMs), gpu); else snprintf(gpuS, sizeof gpuS, "n/a");
    snprintf(buf, sizeof buf, "Emu %.1f fps   Video avg %.1f fps   1%% low %.1f fps   CPU %s   GPU %s   %s", emuCount / secs, avg, low, cpuS, gpuS, dispLabel().c_str());
  }
  if (sessionLog < 120) { sessionLog++; record("t%d %s | wait %.2f block %.2f emu %.2f draw %.2f swap %.2f ms, audio q %.0f ms ratio %.4f", sessionLog, buf,
                                               phase[0] / divisor, phase[1] / divisor, phase[2] / divisor, phase[3] / divisor, phase[4] / divisor, pendEma / 48.0, audioRatio); }
  for (double& x : phase) x = 0; phaseN = 0;
  swapAcc = waitAcc = swapMax = 0; swapN = 0;
  statsText = buf;
  emuCount = 0; workMs = 0; statsAt = t;
}

static bool keyboardRequested=false;
static void searchWithKeyboard(){
  SwkbdConfig config{};
  Result rc=swkbdCreate(&config,0);
  if(R_SUCCEEDED(rc)){
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config,"Search SNES games");
    swkbdConfigSetInitialText(&config,libraryQuery.c_str());
    swkbdConfigSetStringLenMax(&config,40);
    char result[161]={};
    rc=swkbdShow(&config,result,sizeof(result));
    swkbdClose(&config);
    if(R_SUCCEEDED(rc)){
      libraryQuery=result;orderRoms();selected=top=0;
      focusArea=roms.empty()?"tabs":"list";
      return;
    }
    // This SDK reports cancellation and abnormal applet exits identically.
    if(R_VALUE(rc)==MAKERESULT(Module_Libnx,LibnxError_LibAppletBadExit)){
      record("Keyboard canceled or closed: 0x%x",(unsigned)rc);
      return;
    }
  }
  record("Keyboard unavailable: 0x%x",(unsigned)rc);
  mode="librarysearch";queryLetter=0;
}
static library::PageRepeat pageRepeat;
// Touch actions share the controller's library state and deferred keyboard path.
static void touchCategory(int v){
  if(v==listView)return;
  listView=v;roms.clear();orderRoms();selected=top=0;focusArea=roms.empty()?"tabs":"list";exitArmed=0;
}
static float touchStartX=0,touchStartY=0,touchEndX=0,touchEndY=0;
static bool touchTracking=false;
static string touchMode;
static float touchTravel=0,touchCarry=0;
static double touchTick=0;
static float touchVelocity=0;
static int touchPageDirection=0;
static int lastTouchGame=-1;
static double lastTouchTime=0;
static int touchGameAt(float x,float y){
  if(roms.empty())return -1;
  int hit=-1;
  if(libraryGrid){
    float tw=(gfx::UI_W-40-3*GRID_GAP)/4,th=(gfx::UI_H-150-LIB_EXTRA-2*GRID_GAP)/3;
    for(int i=0;i<12;i++)if(inside(x,y,20+(i%4)*(tw+GRID_GAP),GRID_TOP+(i/4)*(th+GRID_GAP),tw,th)){hit=library::pageTop(selected,4,3)+i;break;}
  }else{
    int size=maxRows();float w=(gfx::UI_W-40)*0.65f-(roms.size()>(size_t)size?16:0);
    if(inside(x,y,26,LIST_TOP+6,w-12,size*ROW_H))hit=(selected/size)*size+int((y-LIST_TOP-6)/ROW_H);
  }
  return hit>=0&&hit<(int)roms.size()?hit:-1;
}
static void libraryTap(float x,float y){
  if(inside(x,y,SEARCH_X,SEARCH_Y,SEARCH_W,SEARCH_H)){
    if(!libraryQuery.empty()&&x>=SEARCH_X+SEARCH_W-CLEAR_W)clearLibrarySearch();
    else keyboardRequested=true;
    return;
  }
  if(inside(x,y,VIEW_X,NAV_Y,212,34)){
    libraryGrid=x>=VIEW_X+104;focusArea="list";saveSettings();return;
  }
  float tx=20;
  if(inside(x,y,tx,NAV_Y,42,34)){touchCategory((listView+(int)LIST_VIEWS.size()-1)%LIST_VIEWS.size());return;}
  tx+=50;
  for(size_t i=0;i<LIST_VIEWS.size();i++){
    float w=gfx::textWidth(LIST_VIEWS[i],17)+32;
    if(inside(x,y,tx,NAV_Y,w,34)){touchCategory((int)i);return;}
    tx+=w+8;
  }
  if(inside(x,y,tx,NAV_Y,42,34)){touchCategory((listView+1)%LIST_VIEWS.size());return;}
  int size=libraryGrid?12:maxRows();
  float right=libraryGrid?gfx::UI_W-250:20+(gfx::UI_W-40)*0.65f-(roms.size()>(size_t)size?16:0)-18;
  string p="Page "+std::to_string(roms.empty()?0:selected/size+1)+" / "+std::to_string((roms.size()+size-1)/size);
  float left=right-gfx::textWidth(p,24,gfx::BOLD)-104;
  int step=inside(x,y,left,NAV_Y,42,34)?-1:inside(x,y,right-42,NAV_Y,42,34)?1:0;
  if(step&&!roms.empty()){selected=library::movePage(selected,(int)roms.size(),size,step);focusArea="list";return;}
  float footerY=libraryGrid?gfx::UI_H-48:gfx::UI_H-64;
  float bw=(gfx::UI_W-70)/3.0f;
  for(int i=0;i<3;i++)if(inside(x,y,25+i*(bw+10),footerY,bw,libraryGrid?40:64)){
    lastTouchGame=-1;
    if(i==0){exitArmed=0;mode="settings";settingsTab=0;settingsIndex=settingsTop=0;}
    else if(i==1){exitArmed=0;scan();showToast("Library and covers refreshed");}
    else {double t=nowMs();if(exitArmed>t)quitApp=true;else{focusArea="buttons";btnIndex=2;exitArmed=t+3000;}}
    return;
  }
  int hit=touchGameAt(x,y);
  if(hit>=0){double t=nowMs();bool launch=hit==selected&&hit==lastTouchGame&&t-lastTouchTime<350;selected=hit;focusArea="list";lastTouchGame=hit;lastTouchTime=t;if(launch){lastTouchGame=-1;startGame(roms[selected]);}}else lastTouchGame=-1;
 }
static void activateFolderRow(){
  int up=pickAtRoot()?0:1;
      if (pickSel == 0) {
        string chosen=content_policy::normalize(pickPath);
        if(chosen.empty()){showToast("Invalid SD folder");return;}
        if(pickingCoverFolder){customCoverDir=chosen;pickingCoverFolder=false;saveSettings();beginCoverScan();mode="settings";return;}
        if(addingRomFolder){if(chosen==content_policy::normalize(romDir)||extraRomDirs.size()>=15||!content_policy::add(extraRomDirs,chosen)){showToast("Folder already added or limit reached");return;}}else romDir=chosen;
        saveSettings(); scan(); mode = "settings";
        showToast(std::to_string(allRoms.size()) + (allRoms.size() == 1 ? " game in " : " games in ") + (romDir.size() > 5 ? romDir.substr(5) : string("/")));
      } else if (up && pickSel == 1) pickLoad(parentDir(pickPath));
      else pickLoad((pickAtRoot() ? string("sdmc:") : pickPath) + "/" + pickDirs[pickSel - 1 - up]);
}
static void folderTap(float x,float y){
  int total=pickRows(),vis=std::max(5,std::min(total,(int)((gfx::UI_H*0.9-36-50-40-14)/34)));
  float winH=36+46+30+vis*34+12,top=roundf((gfx::UI_H-winH)/2)+100;
  if(y<top-70||y>=gfx::UI_H-38){mode="settings";return;}
  if(x>=308&&x<1228&&y>=top&&y<top+vis*34){int i=pickTop+int((y-top)/34);if(i<total){pickSel=i;activateFolderRow();}}
}
static void settingsTap(float x,float y){
  if(y>=18&&y<=58){if(x>=74&&x<704){int tab=int((x-74)/210);if(tab<3){settingsTab=tab;resetArmed=false;}}return;}
  if(y>=gfx::UI_H-48){saveSettings();mode="browser";return;}
  if(settingsTab==1){if(y>=180&&y<238&&x>=32&&x<1484){int i=int((x-32)/490);if(i<3)coverCommand(i);}return;}
  if(settingsTab!=0)return;
  int total=(int)SETTINGS_ITEMS.size()+1,vis=std::max(5,std::min(total,(int)((gfx::UI_H-138)/SET_ROW_H)));
  if(y<70||y>=70+vis*SET_ROW_H)return;
  int i=settingsTop+int((y-70)/SET_ROW_H);if(i<0||i>=total)return;
  if(i!=settingsIndex)resetArmed=false;settingsIndex=i;
  if(i==total-1){saveSettings();mode="browser";return;}
  if(x<gfx::UI_W*0.52f)return;
  SettingItem it=SETTINGS_ITEMS[i];int n=(int)it.choices().size();if(n>0){float right=gfx::UI_W-20-(total>vis?16:0)-22;string val=gfx::ellipsize(it.choices()[std::max(0,std::min(n-1,it.get()))],(gfx::UI_W-52-(total>vis?16:0))*0.48f-64,23);float left=right-gfx::textWidth(val,23)-gfx::textWidth("<",23,gfx::BOLD)-gfx::textWidth(">",23,gfx::BOLD)-20;it.set((it.get()+n+(x<left+35?-1:1))%n);}
}
static void touchScrollRows(int step){
  int total=(int)SETTINGS_ITEMS.size()+1,vis=std::max(5,std::min(total,(int)((gfx::UI_H-138)/SET_ROW_H)));
  settingsTop=std::max(0,std::min(total-vis,settingsTop+step));
  settingsIndex=std::max(settingsTop,std::min(settingsTop+vis-1,settingsIndex));
}
static void handleLibraryTouch(){
  HidTouchScreenState state{};
  if(!hidGetTouchScreenStates(&state,1))return;
  double now=nowMs();
  if(touchMode!=mode){touchMode=mode;touchTracking=false;touchHeld=state.count!=0;touchVelocity=touchCarry=0;lastTouchGame=-1;}
  if(state.count==0){
    touchHeld=false;
    if(!touchTracking){
      if(mode=="settings"&&settingsTab==0&&fabsf(touchVelocity)>0.02f){float dt=std::min(40.0f,float(now-touchTick));touchCarry+=touchVelocity*dt;int rows=int(touchCarry/SET_ROW_H);if(rows){touchScrollRows(rows);touchCarry-=rows*SET_ROW_H;}touchVelocity*=expf(-dt/120.0f);}
      touchTick=now;return;
    }
    touchTracking=false;
    float dx=touchEndX-touchStartX,dy=touchEndY-touchStartY;
    if(mode=="browser"&&touchStartY>=(libraryGrid?GRID_TOP:LIST_TOP)&&!roms.empty()&&((libraryGrid&&fabsf(dx)>70&&fabsf(dx)>fabsf(dy))||(!libraryGrid&&fabsf(dy)>70&&fabsf(dy)>fabsf(dx)))){
      int step=(libraryGrid?dx:dy)<0?1:-1;
      selected=library::movePage(selected,(int)roms.size(),libraryGrid?12:maxRows(),step);focusArea="list";lastTouchGame=-1;return;
    }
    if(touchTravel<24){if(mode=="folders")folderTap(touchEndX,touchEndY);else if(mode=="settings")settingsTap(touchEndX,touchEndY);else libraryTap(touchEndX,touchEndY);}
    else {lastTouchGame=-1;if(mode=="folders"){int step=int(-dy/34);pickSel=std::max(0,std::min(pickRows()-1,pickSel+step));}if(mode=="settings"&&settingsTab==1&&showUnmatched)unmatchedTop=std::max(0,std::min(std::max(0,(int)unmatchedCovers.size()-9),unmatchedTop+int(-dy/42)));}
    touchTick=now;return;
  }
  if(state.count!=1){touchHeld=true;touchTracking=false;lastTouchGame=-1;touchVelocity=touchCarry=0;return;}
  float x=state.touches[0].x*(gfx::UI_W/1280.0f),y=state.touches[0].y*(gfx::UI_H/720.0f);
  if(!touchHeld){touchHeld=true;touchTracking=true;touchStartX=touchEndX=x;touchStartY=touchEndY=y;touchTravel=touchCarry=touchVelocity=0;touchTick=now;}
  if(touchTracking){
    touchTravel=std::max(touchTravel,std::max(fabsf(x-touchStartX),fabsf(y-touchStartY)));
    if(mode=="settings"&&settingsTab==0&&touchStartY>=70&&touchStartY<gfx::UI_H-48&&touchTravel>=24){
      float delta=touchEndY-y;float dt=std::max(1.0f,float(now-touchTick));touchVelocity=0.65f*touchVelocity+0.35f*std::max(-2.0f,std::min(2.0f,delta/dt));touchCarry+=delta;int rows=int(touchCarry/SET_ROW_H);if(rows){touchScrollRows(rows);touchCarry-=rows*SET_ROW_H;}
    }
    touchEndX=x;touchEndY=y;touchTick=now;
  }
}
static void handleUi(u64 bits) {
  logoButtons=bits;
  Nav ev = navEvents(bits);
  u64 pressed = ev.pressed;
  double t = nowMs();
  int shoulder=((bits&B_R)?1:0)-((bits&B_L)?1:0);
  bool pageContext=mode=="browser" && !roms.empty() && !((bits&B_L)&&(bits&B_R)) && !(pressed&(B_PLUS|B_MINUS|B_Y|B_X|B_A|B_B|HidNpadButton_ZL|HidNpadButton_ZR));
  int pageStep=pageRepeat.update(shoulder,((pressed&(B_L|B_R))!=0),pageContext,t);
  if (mode == "browser") {
    if((pressed & B_A) && (bits & B_Y)){
      if(!recentGames.empty())for(const auto& r:allRoms)if(r.name==recentGames.front()){Rom copy=r;startGame(copy,true);return;}
      showToast("No recent game available");return;
    }
    if(pressed & B_PLUS){libraryGrid=!libraryGrid;focusArea="list";saveSettings();return;}
    if((bits & HidNpadButton_ZL) && (bits & B_MINUS) && (pressed & (HidNpadButton_ZL|B_MINUS))){
      if(!libraryQuery.empty())clearLibrarySearch();
      return;
    }
    if(pressed & B_MINUS){if(bits&B_Y){mode="librarysearch";queryLetter=0;}else keyboardRequested=true;return;}
    int tabStep = ((pressed & HidNpadButton_ZR)?1:0)-((pressed & HidNpadButton_ZL)?1:0);
    if (tabStep) {
      int n = (int)LIST_VIEWS.size();
      listView = (listView + tabStep + n) % n; roms.clear(); orderRoms(); selected = top = 0;
      if (focusArea == "list" && roms.empty()) focusArea = "tabs";
    }
    // Y / X: the Settings and Refresh buttons (B is Exit, below).
    if (pressed & B_Y) { exitArmed = 0; mode = "settings"; settingsTab=0; settingsIndex = 0; settingsTop = 0; return; }
    if (pressed & B_X) { exitArmed = 0; scan(); showToast("List refreshed"); return; }
    if (exitArmed && (t > exitArmed || (pressed & ~(u64)(B_A | B_B)) || ev.up || ev.down || focusArea != "buttons" || btnIndex != 2)) exitArmed = 0;
    if(libraryGrid){
      if(!roms.empty()){
        if(pageStep){selected=library::movePage(selected,(int)roms.size(),12,pageStep);return;}
        if(ev.up)selected=std::max(0,selected-4);
        if(ev.down)selected=std::min((int)roms.size()-1,selected+4);
        if(ev.left)selected=std::max(0,selected-1);
        if(ev.right)selected=std::min((int)roms.size()-1,selected+1);
        if(pressed&B_A){startGame(roms[selected]);return;}
      }
      if(pressed&B_B){libraryGrid=false;focusArea="buttons";btnIndex=2;exitArmed=t+3000;}return;
    }
    int VIS = std::max(1, std::min((int)roms.size(), maxRows()));
    if(pageStep){selected=library::movePage(selected,(int)roms.size(),maxRows(),pageStep);focusArea="list";return;}
    if (focusArea == "tabs") {
      if (pressed & B_LEFT) { int v = std::max(0, listView - 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (pressed & B_RIGHT) { int v = std::min((int)LIST_VIEWS.size() - 1, listView + 1); if (v != listView) { listView = v; roms.clear(); orderRoms(); } }
      if (ev.down) { focusArea = roms.empty() ? "buttons" : "list"; selected = 0; }
    } else if (focusArea == "list" && !roms.empty()) {
      if (ev.up) { if (selected == 0) focusArea = "tabs"; else selected--; }
      if (ev.down) { if (selected == (int)roms.size() - 1) focusArea = "buttons"; else selected++; }
      if (pressed & B_LEFT) selected = std::max(0, selected - VIS);
      if (pressed & B_RIGHT) selected = std::min((int)roms.size() - 1, selected + VIS);
      if ((pressed & B_A) && selected < (int)roms.size()) { startGame(roms[selected]); return; }
    } else {
      if (ev.up) { focusArea = roms.empty() ? "tabs" : "list"; if (!roms.empty()) selected = (int)roms.size() - 1; }
      if (pressed & B_LEFT) btnIndex = std::max(0, btnIndex - 1);
      if (pressed & B_RIGHT) btnIndex = std::min(2, btnIndex + 1);
      if (pressed & B_A) {
        if (btnIndex == 0) { mode = "settings"; settingsTab=0; settingsIndex = 0; settingsTop = 0; }
        else if (btnIndex == 1) { scan(); showToast("List refreshed"); }
        else if (exitArmed) quitApp = true;
        else exitArmed = t + 3000;
        return;
      }
    }
    if (pressed & B_B) { if (exitArmed) quitApp = true; else { focusArea = "buttons"; btnIndex = 2; exitArmed = t + 3000; } }
    if (roms.empty() && focusArea == "list") focusArea = "buttons";
  } else if(mode=="librarysearch"){
    int n=(int)QUERY_LETTERS.size();
    if(pressed&B_LEFT)queryLetter=(queryLetter+n-1)%n;
    if(pressed&B_RIGHT)queryLetter=(queryLetter+1)%n;
    if((pressed&B_A)&&libraryQuery.size()<40)libraryQuery+=QUERY_LETTERS[queryLetter];
    if((pressed&B_X)&&!libraryQuery.empty())libraryQuery.pop_back();
    if(pressed&B_Y)libraryQuery.clear();
    if(pressed&(B_PLUS|B_B)){orderRoms();mode="browser";focusArea=roms.empty()?"tabs":"list";}
  } else if (mode == "folders") {
    int total = pickRows(), up = pickAtRoot() ? 0 : 1;
    if (ev.up) pickSel = (pickSel - 1 + total) % total;
    if (ev.down) pickSel = (pickSel + 1) % total;
    if (pressed & B_A) {
      activateFolderRow();
    } else if (pressed & B_B) mode = "settings";
  } else if (mode == "settings") {
    if(pressed&(B_L|B_R)){settingsTab=(settingsTab+3+((pressed&B_R)?1:0)-((pressed&B_L)?1:0))%3;resetArmed=false;return;}
    if(settingsTab==1){if(ev.left)coverAction=(coverAction+2)%3;if(ev.right)coverAction=(coverAction+1)%3;if(pressed&B_A)coverCommand(coverAction);if(showUnmatched){if(ev.up)unmatchedTop=std::max(0,unmatchedTop-1);if(ev.down)unmatchedTop=std::min(std::max(0,(int)unmatchedCovers.size()-9),unmatchedTop+1);}if(pressed&B_B){saveSettings();mode="browser";}return;}
    if(settingsTab!=0){if(pressed&B_B){saveSettings();mode="browser";}return;}
    int nItems = (int)SETTINGS_ITEMS.size();
    if (ev.up || ev.down || (pressed & B_B)) resetArmed = false;
    if (ev.up) settingsIndex = (settingsIndex - 1 + nItems + 1) % (nItems + 1);
    if (ev.down) settingsIndex = (settingsIndex + 1) % (nItems + 1);
    if (settingsIndex < nItems) {
      SettingItem it = SETTINGS_ITEMS[settingsIndex];  // copy: the advanced toggle rebuilds the list
      int n = (int)it.choices().size();
      if (pressed & B_LEFT) it.set((it.get() - 1 + n) % n);
      else if (pressed & (B_RIGHT | B_A)) it.set((it.get() + 1) % n);
    } else if (pressed & B_A) { saveSettings(); mode = "browser"; return; }
    if (pressed & B_B) { saveSettings(); mode = "browser"; }
  } else if (mode == "menu") {
    bool lr = (bits & MENU_COMBO) == MENU_COMBO;
    if (lr && !comboLatch) { comboLatch = true; closeMenu(); return; }
    if (!lr) comboLatch = false;
    int n = (int)menuItems.size();
    if (ev.up) menuIndex = (menuIndex - 1 + n) % n;
    if (ev.down) menuIndex = (menuIndex + 1) % n;
    MenuItem item = menuItems[menuIndex];  // copy: actions may replace the list
    if (item.option()) {
      if (pressed & B_LEFT) cycleMenuOption(-1);
      if (pressed & (B_RIGHT | B_A)) cycleMenuOption(1);
    } else if (pressed & B_A) item.action();
    if ((pressed & B_B) && mode == "menu") closeMenu();
  } else if (mode == "ggedit") {
    handleCodeEditor(ev);
  } else if (mode == "message") {
    if (pressed & (B_A | B_B)) { mode = "browser"; auto then = messageThen; messageThen = nullptr; if (then) then(); }
  }
}

static void traceRuntimeExit() {
  FILE* f = fopen("sdmc:/switch/NS2xSNES9x/log.txt", "a");
  if (f) { fputs("runtime: atexit callback reached\n", f); fclose(f); }
}

static bool ensureDirectory(const char* path){
  struct stat st;
  if(stat(path,&st)==0)return S_ISDIR(st.st_mode);
  if(mkdir(path,0777)==0)return true;
  return stat(path,&st)==0 && S_ISDIR(st.st_mode);
}
static void startupScreen(int stage,const string& error=""){
  gfx::waitPrevious();gfx::beginFrame(col(C_BG));
  const char* stages[]={"Creating application and cover directories","Creating ROM directory","Preparing configuration","Initializing emulator","Scanning game library"};
  gfx::text("NS2xSNES9x",440,100,42,col(C_TEXT),gfx::BOLD);
  gfx::text("First-Time Setup",440,170,32,col(C_TEXT));
  for(int i=0;i<5;i++)gfx::text(string(i<stage?"[Done] ":i==stage?"> ":"    ")+stages[i],440,250+i*62,26,col(i==stage?C_ACCENT:C_TEXT));
  gfx::rect(440,590,650,12,col(C_LINE),6);gfx::rect(440,590,650*stage/5.0f,12,col(C_ACCENT),6);
  if(!error.empty()){gfx::text(gfx::ellipsize(error,1100,24),220,650,24,col(C_TEXT));gfx::text("Press B to exit and check your SD card.",440,700,22,col(C_DIM));}
  gfx::endFrame(frameQueue);gfx::waitPrevious();
}
int main(int argc, char** argv) {
  romfsInit();
  bool migrationReady=branding_migration::migrate("sdmc:/switch/ns2snes-9x", DATA_DIR);
  bool firstLaunch=!exists(SETTINGS());
  string error;
  if (!gfx::init(error)) { romfsExit(); return 1; }
  applyTheme();
  if(firstLaunch)startupScreen(0);
  bool dirs=migrationReady && ensureDirectory("sdmc:/switch") && ensureDirectory(DATA_DIR);
  bool coverDirReady=dirs && ensureDirectory(coverDirectory().c_str());
  if(firstLaunch && dirs){startupScreen(1);dirs=ensureDirectory("sdmc:/roms") && ensureDirectory(DEFAULT_ROM_DIR);}
  if(firstLaunch && dirs){startupScreen(2);saveSettings();dirs=exists(SETTINGS());}
  if(!dirs){
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState setupPad;padInitialize(&setupPad,HidNpadIdType_No1,HidNpadIdType_Handheld);
    startupScreen(0,"Unable to migrate existing data or create required directories/settings.");
    while(appletMainLoop()){padUpdate(&setupPad);if(padGetButtonsDown(&setupPad)&B_B)break;svcSleepThread(16000000);}
    gfx::shutdown();romfsExit();return 1;
  }
  if(firstLaunch)startupScreen(3);
  const string logPath = string(DATA_DIR) + "/log.txt";
  const string previousLog = string(DATA_DIR) + "/log.previous.txt";
  if (exists(logPath)) { remove(previousLog.c_str()); rename(logPath.c_str(), previousLog.c_str()); }
  logFile = fopen(logPath.c_str(), "a");
  std::atexit(traceRuntimeExit);
  record("NS2xSNES9x %s", APP_VERSION);
  if(!coverDirReady)record("Cover directory unavailable; ROM sidecars remain supported");

  string audioError;
  bool sound = audio::init(audioError);
  if (!sound) record("audio: %s", audioError.c_str());

  padConfigureInput(2, HidNpadStyleSet_NpadStandard);
  PadState pad1, pad2;
  padInitialize(&pad1, HidNpadIdType_No1, HidNpadIdType_Handheld);
  padInitialize(&pad2, HidNpadIdType_No2);
  hidInitializeTouchScreen();

  W = 256; H = 224;
  picture = gfx::pictureCreate(512, 478);  // the largest SNES frame (512 x 478 interlaced)
  {
    string slog;
    int n = gfx::loadShaders(SHADER_FILES, slog);
    record("gpu timer: %s", gfx::gpuTimerAvailable() ? "yes" : "no");
  record("shaders: %d/%d compiled%s%s", n, (int)SHADER_FILES.size(), slog.empty() ? "" : " - ", slog.c_str());
  }
  graphImage = gfx::imageCreate(GRAPH_W, GRAPH_H);
  resetGraph();
  // (built after loading settings: the advanced toggle decides which are shown)

  loadSettings();
  loadPlaytime();
  rebuildSettings();
  if(firstLaunch)startupScreen(4);
  scan();
  if (!sound) showToast("Audio unavailable", 4000);
  padUpdate(&pad1);
  lastBits = padGetButtons(&pad1);  // ignore the A press that launched the app

  gfx::setHighRefresh(displayMode == 1);

  while (appletMainLoop() && !quitApp) {
    // Order for low latency: wait for the previous frame, start this frame
    // (the display wait happens here), optional frame delay, THEN read input
    // and emulate, so the picture shows input read as late as possible.
    double ta = nowMs();
    if (frameQueue == 1) gfx::waitPrevious();
    double tb = nowMs();
    loopGapMs = loopStartPrev ? tb - loopStartPrev : 16.69;  // time since the previous refresh
    loopStartPrev = tb;
    bool gameView = running && (mode == "running" || mode == "menu" || mode == "ggedit");
    gfx::beginFrame(gameView ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    double tc = nowMs();
    // 120 Hz / VRR: present once per SNES frame (60.0988 Hz, the same rate as the NES) on a clock, so a
    // VRR screen refreshes exactly with the game (even frame times, no audio
    // stretch); input is read right after the wait (late, like frame delay).
    if (highRefresh() && running && (mode == "running" || (mode == "menu" && !pauseInMenu))) {
      double period = 1000.0 / gameRate, tnow = nowMs();
      if (!vrrNext || tnow - vrrNext > period * 2) vrrNext = tnow;  // start or resync after a stall
      for (;;) {
        double remaining = vrrNext - nowMs() - 0.25;
        if (remaining <= 0) break;
        svcSleepThread((int64_t)(std::min(remaining, 4.0) * 1000000.0));
      }
      vrrNext += period;
    } else vrrNext = 0;
    if (mode == "running" && !highRefresh() && FRAME_DELAY_MS[governor.delay(performanceProfile, frameDelay)] && tickMs > 12) {
      double until = tc + FRAME_DELAY_MS[governor.delay(performanceProfile, frameDelay)];
      for (;;) {
        double remaining = until - nowMs();
        if (remaining <= 0) break;
        svcSleepThread((int64_t)(std::min(remaining, 2.0) * 1000000.0));
      }
    }
    padUpdate(&pad1); padUpdate(&pad2);
    u64 bits = reconnectP1.filter(padIsConnected(&pad1), analogButtons(&pad1)), bits2 = reconnectP2.filter(padIsConnected(&pad2), analogButtons(&pad2));
    if (!padIsConnected(&pad1)) turboP1.reset();
    if (!padIsConnected(&pad2)) turboP2.reset();
    frameDrawn = false;
    if (mode == "running") runFrame(bits, bits2, false);
    else {
      if (mode == "menu" && running && !pauseInMenu) runFrame(0, 0, true);
      handleUi(bits);
      if((mode=="browser"||mode=="settings"||mode=="folders")&&!running&&!keyboardRequested){if(bits){touchVelocity=touchCarry=0;touchTracking=false;touchHeld=true;}handleLibraryTouch();}else {touchHeld=true;touchTracking=false;lastTouchGame=-1;touchMode.clear();touchVelocity=touchCarry=0;}
    }
    if(!running&&mode=="settings"&&settingsTab==1)tickCoverScan();
    else if(!running&&coverScanning&&coverDirCursor<coverDirs.size()){string rel=coverDirs[coverDirCursor++];readCoverDir(rel);}
    double t = nowMs();
    if (toastUntil && t > toastUntil) toastUntil = 0;
    if (hintUntil && t > hintUntil) hintUntil = 0;
    if (gameView != (running && (mode == "running" || mode == "menu" || mode == "ggedit"))) gfx::beginFrame(running ? gfx::Color{0, 0, 0, 1} : col(C_BG));
    if (running && (mode == "running" || mode == "menu" || mode == "ggedit")) {
      drawGame(mode != "running");
      if (mode == "menu") drawMenu();
      if (mode == "ggedit") drawCodeEditor();
    } else if (mode == "settings") {if(settingsTab==1)drawCoverGuide();else if(settingsTab==2)drawCredits();else drawSettings();}
    else if (mode == "folders") drawFolderPicker();
    else if(mode=="librarysearch"){
      float x=(gfx::UI_W-800)/2,y=(gfx::UI_H-260)/2;
      gfx::rect(x,y,800,260,col(C_PANEL),14,1,col(C_LINE));
      gfx::text("Search games",x+24,y+24,26,col(C_TEXT),gfx::BOLD);
      gfx::text(libraryQuery+"_",x+24,y+80,24,col(C_TEXT));
      gfx::text(string("<  ")+QUERY_LETTERS[queryLetter]+"  >",x+24,y+130,24,col(C_ACCENT));
      gfx::text("Left/Right letter | A Add | X Delete | Y Clear | + / B Done",x+24,y+205,17,col(C_DIM));
    } else drawBrowser();
    if (mode == "message") drawMessage();
    if (running && mode == "running" && (rewinding || fastForward)) {
      string tag = rewinding ? (rewind_count() ? "<< Rewind" : "<< Rewind (start)") : ">> " + FF_SPEEDS[ffSpeed];
      float w = gfx::textWidth(tag, 22, gfx::BOLD) + 28;
      gfx::rect(gfx::UI_W - 16 - w, 12, w, 40, gfx::Color{0, 0, 0, 0.6f}, 8);
      textMid(tag, gfx::UI_W - 16 - w + 14, 12, 40, 22, col(C_TEXT), gfx::BOLD);
    }
    if (captureRequested && mode == "running") {
      captureRequested = false;
      char name[64]; time_t tt = time(nullptr); struct tm lt = *localtime(&tt);
      snprintf(name, sizeof name, "%04d%02d%02d-%02d%02d%02d.png", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
      string dir = string(DATA_DIR) + "/screenshots";
      mkdir(dir.c_str(), 0777);
      bool ok = gfx::screenshot((dir + "/" + name).c_str());
      record("screenshot %s: %s", name, ok ? "saved" : "failed");
      showToast(ok ? string("Screenshot saved: ") + name : "Screenshot failed");
    }
    drawToast();
    double td = nowMs();
    gfx::endFrame(frameQueue);
    if(keyboardRequested){touchHeld=true;touchTracking=false;lastTouchGame=-1;keyboardRequested=false;gfx::waitPrevious();searchWithKeyboard();lastBits=~(u64)0;heldDirs=0;resetClock();}
    double te = nowMs();
    phase[0] += tb - ta; phase[1] += tc - tb; phase[2] += t - tc; phase[3] += td - t; phase[4] += te - td; phaseN++;

    double after = nowMs();
    double lastGapForFree = lastSwap ? after - lastSwap : 0;
    if (lastSwap) {
      double gap = after - lastSwap;
      if (gap > 5 && gap < 21) tickMs += (gap - tickMs) * 0.01;
      if (frameDrawn) { noteShown(after); if (statsLevel == 3 && shownCount >= 2) drawGraph(gap); }
    } else if (frameDrawn) noteShown(after);
    lastSwap = after;
    if (!vrrNext && lastGapForFree > 3 && lastGapForFree < 40) freeTickMs = freeTickMs ? freeTickMs + (lastGapForFree - freeTickMs) * 0.05 : lastGapForFree;
    pollDisplay(after);
    if (running && (mode == "running" || (mode == "menu" && !pauseInMenu))) playAccumSec += std::min(loopGapMs, 100.0) / 1000.0;
    if (running && after - playSavedAt >= 300000) { playSavedAt = after; flushPlaytime(running->name); }
    swapAcc += gfx::swapMs; waitAcc += gfx::waitMs; swapN++; if (gfx::swapMs > swapMax) swapMax = gfx::swapMs;
    updateStats(after);
  }

  record("shutdown: begin");
  if (running) quitGame();
  saveSettings();
  record("shutdown: textures");
  clearCovers();
  gfx::imageDestroy(graphImage); graphImage=nullptr;
  gfx::pictureDestroy(picture); picture=nullptr;
  record("shutdown: audio");
  audio::shutdown();
  record("shutdown: EGL");
  gfx::shutdown();
  record("shutdown: romfs begin");
  romfsExit();
  record("shutdown: romfs complete");
  record("shutdown: clearing UI containers");
  messageThen = nullptr;
  menuItems.clear(); SETTINGS_ITEMS.clear();
  allRoms.clear(); roms.clear(); pickDirs.clear();
  record("shutdown: returning from main");
  if (logFile) { fclose(logFile); logFile=nullptr; }
  return 0;
}
