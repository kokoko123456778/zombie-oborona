// =====================================================================
//  ЗОМБИ-ОБОРОНА — башни против волн зомби (tower defense)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 800, LH = 480;      // логический (горизонтальный) размер экрана
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  gWin = SDL_CreateWindow("ZombieDefense", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "zombie_defense.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  V3 v[32];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}

// =====================================================================
//  ЗОМБИ-ОБОРОНА: игра
// =====================================================================
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}

// ---------- карты: путь зомби по клеткам 16x9 ----------
enum { GW = 16, GH = 9, NMAP = 3, NWAVE = 30 };
struct MapDef { const char *name; int n; int wx[12], wy[12]; u32 grass, path; };
static const MapDef MAPS[NMAP] = {
  {"Деревня", 8, {-1, 3, 3, 8, 8, 12, 12, 16}, {2, 2, 6, 6, 2, 2, 6, 6}, RGB(86, 140, 62), RGB(176, 150, 104)},
  {"Город", 10, {-1, 2, 2, 6, 6, 10, 10, 13, 13, 16}, {7, 7, 1, 1, 7, 7, 2, 2, 5, 5}, RGB(98, 110, 92), RGB(120, 120, 128)},
  {"Завод", 12, {-1, 14, 14, 1, 1, 13, 13, 4, 4, 10, 10, 16}, {1, 1, 3, 3, 5, 5, 7, 7, 8, 8, 6, 6}, RGB(110, 100, 80), RGB(90, 84, 78)},
};
static unsigned char cellPath[GH][GW], cellBlock[GH][GW];
static float pathX[64], pathY[64], pathLen[64], pathTotal = 0; static int pathN = 0;

// ---------- башни и зомби ----------
struct TowDef { const char *name; int cost; float range, rate, dmg; int kind; u32 col; };
// kind: 0 пуля, 1 дробь, 2 огонь, 3 ракета, 4 сетка, 5 снайпер, 6 тесла, 7 миномёт, 8 лазер, 9 ферма, 10 штаб
enum { NTOW = 11, K_FARM = 9, K_HQ = 10 };
static const TowDef TD[NTOW] = {
  {"Пулемёт", 50, 2.6f, 4.0f, 6.0f, 0, RGB(90, 120, 160)},
  {"Дробовик", 80, 1.9f, 1.1f, 9.0f, 1, RGB(150, 110, 70)},
  {"Огнемёт", 120, 1.6f, 8.0f, 2.2f, 2, RGB(220, 100, 40)},
  {"Ракеты", 200, 3.4f, 0.55f, 55.0f, 3, RGB(110, 140, 90)},
  {"Сетка", 90, 2.0f, 1.0f, 1.0f, 4, RGB(90, 170, 190)},
  {"Снайпер", 150, 5.0f, 0.5f, 70.0f, 5, RGB(70, 90, 60)},
  {"Тесла", 220, 2.2f, 1.2f, 18.0f, 6, RGB(120, 160, 255)},
  {"Миномёт", 180, 4.6f, 0.4f, 45.0f, 7, RGB(110, 100, 80)},
  {"Лазер", 260, 2.8f, 10.0f, 2.0f, 8, RGB(200, 60, 80)},
  {"Ферма", 100, 0.0f, 0.0f, 0.0f, 9, RGB(230, 190, 60)},
  {"Штаб", 160, 2.2f, 0.0f, 0.0f, 10, RGB(150, 150, 170)},
};
struct ZDef { const char *name; float hp, spd, armor; int reward, lives; u32 col; float size; };
enum { NZT = 10 };
static const ZDef ZD[NZT] = {
  {"Зомби", 30, 1.0f, 0.0f, 4, 1, RGB(110, 160, 90), 0.28f},
  {"Бегун", 18, 1.9f, 0.0f, 4, 1, RGB(160, 180, 80), 0.24f},
  {"Толстяк", 130, 0.6f, 0.0f, 10, 2, RGB(90, 130, 70), 0.38f},
  {"Броня", 260, 0.55f, 0.5f, 18, 2, RGB(120, 120, 130), 0.34f},
  {"Громила", 1800, 0.45f, 0.3f, 150, 5, RGB(150, 40, 60), 0.48f},
  {"Охотник", 700, 0.9f, 0.1f, 80, 3, RGB(140, 70, 170), 0.40f},          // рывками ускоряется
  {"Матка", 1500, 0.4f, 0.0f, 120, 5, RGB(210, 120, 150), 0.52f},         // умирая, выпускает шесть бегунов
  {"Некромант", 1400, 0.45f, 0.0f, 140, 5, RGB(60, 120, 120), 0.45f},     // поднимает зомби
  {"Щитоносец", 2200, 0.45f, 0.4f, 160, 5, RGB(90, 120, 170), 0.48f},     // зомби рядом получают вдвое меньше урона
  {"Король зомби", 3200, 0.35f, 0.3f, 500, 10, RGB(120, 30, 40), 0.62f},  // лечится, зовёт бегунов, сетка не держит
};
static bool isBoss(int t) { return t >= 4; }
struct Zom { float d, hp, hpMax, slow, burn, abil, dash; int type; bool alive, shield; };
struct Tow { int x, y, type, lvl; float cd, ang; int target; float heat; };
struct Fx { float x0, y0, x1, y1, t, dur; int kind; };
static std::vector<Zom> zs; static std::vector<Tow> tw; static std::vector<Fx> fxs;
static std::vector<float> pendingBrood;   // где погибла матка - там появятся бегуны
static int money = 150, lives = 20, wave = 0, mapId = 0, selType = 0, selTow = -1, speedX = 1;
static bool waveOn = false, paused = false;
static float strikeCd = 0; static bool aiming = false;          // авиаудар: перезарядка и режим выбора точки
static float spawnT = 0; static int spawnLeft = 0, spawnIdx = 0;
static int spawnList[400];
enum { ZS_TITLE, ZS_PLAY, ZS_WIN, ZS_LOSE };
static int zstate = ZS_TITLE;
static int unlocked = 1, bestWave[NMAP] = {0, 0, 0};
static char msg[96] = ""; static float msgT = 0;
static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 2.5f; }

static void saveProg() {
  FILE *f = fopen(pth("zombie_defense.sav"), "w"); if (!f) return;
  fprintf(f, "%d %d %d %d\n", unlocked, bestWave[0], bestWave[1], bestWave[2]); fclose(f);
}
static void loadProg() {
  FILE *f = fopen(pth("zombie_defense.sav"), "r"); if (!f) return;
  int u = 1, a = 0, b = 0, c = 0; if (fscanf(f, "%d %d %d %d", &u, &a, &b, &c) == 4) { unlocked = clampi(u, 1, NMAP); bestWave[0] = a; bestWave[1] = b; bestWave[2] = c; }
  fclose(f);
}
static void buildMap(int m) {
  memset(cellPath, 0, sizeof cellPath); memset(cellBlock, 0, sizeof cellBlock);
  const MapDef &M = MAPS[m];
  pathN = 0; pathTotal = 0;
  for (int i = 0; i < M.n; i++) {
    pathX[pathN] = M.wx[i] + 0.5f; pathY[pathN] = M.wy[i] + 0.5f; pathLen[pathN] = pathTotal;
    if (pathN > 0) { pathTotal += fabsf(pathX[pathN] - pathX[pathN - 1]) + fabsf(pathY[pathN] - pathY[pathN - 1]); pathLen[pathN] = pathTotal; }
    pathN++;
    if (i > 0) {   // отмечаем клетки дороги
      int x0 = M.wx[i - 1], y0 = M.wy[i - 1], x1 = M.wx[i], y1 = M.wy[i];
      int sx = x1 > x0 ? 1 : (x1 < x0 ? -1 : 0), sy = y1 > y0 ? 1 : (y1 < y0 ? -1 : 0), x = x0, y = y0;
      while (true) { if (x >= 0 && x < GW && y >= 0 && y < GH) cellPath[y][x] = 1; if (x == x1 && y == y1) break; x += sx; y += sy; }
    }
  }
  srand(77 + m * 13);   // деревья и камни, на которых строить нельзя
  for (int k = 0; k < 9; k++) { int x = rand() % GW, y = rand() % GH; if (!cellPath[y][x]) cellBlock[y][x] = 1 + rand() % 2; }
  srand((unsigned)time(NULL));
}
static void posAt(float d, float &x, float &y) {
  if (d <= 0) { x = pathX[0]; y = pathY[0]; return; }
  for (int i = 1; i < pathN; i++) if (d <= pathLen[i]) {
    float k = (d - pathLen[i - 1]) / fmaxv(0.001f, pathLen[i] - pathLen[i - 1]);
    x = pathX[i - 1] + (pathX[i] - pathX[i - 1]) * k; y = pathY[i - 1] + (pathY[i] - pathY[i - 1]) * k; return;
  }
  x = pathX[pathN - 1]; y = pathY[pathN - 1];
}
static const float MAPHARD[NMAP] = {1.0f, 1.25f, 1.9f};   // Город и Завод труднее
static float waveHp(int w) { return (1.0f + 0.16f * (w - 1) + 0.011f * (w - 1) * (w - 1)) * MAPHARD[mapId]; }
static void startGame(int m) {
  mapId = m; buildMap(m); zs.clear(); tw.clear(); fxs.clear(); pendingBrood.clear();
  money = 200; lives = 20; wave = 0; waveOn = false; paused = false; selTow = -1; selType = 0; speedX = 1; zstate = ZS_PLAY; strikeCd = 0; aiming = false;
  say("Ставь башни у дороги и жми ВОЛНА!");
}
static void startWave() {
  if (waveOn || wave >= NWAVE) return;
  wave++; waveOn = true; spawnIdx = 0; spawnT = 0;
  int n = imin(380, 6 + (int)(wave * 1.7f)); spawnLeft = 0;
  for (int i = 0; i < n; i++) {
    int t = 0, r = rand() % 100;
    if (wave >= 3 && r < 25) t = 1;
    if (wave >= 5 && r >= 25 && r < 25 + imin(30, wave * 2)) t = 2;
    if (wave >= 9 && r >= 70 && r < 70 + imin(25, wave)) t = 3;
    spawnList[spawnLeft++] = t;
  }
  int boss = -1;
  switch (wave) { case 5: boss = 5; break; case 10: boss = 4; break; case 15: boss = 6; break; case 20: boss = 7; break; case 25: boss = 8; break; case 30: boss = 9; break; }
  if (boss >= 0) spawnList[spawnLeft++] = boss;
  char b[80]; if (boss >= 0) snprintf(b, sizeof b, "ВОЛНА %d: БОСС %s!", wave, ZD[boss].name); else snprintf(b, sizeof b, "ВОЛНА %d", wave); say(b);
}
static int upCost(const Tow &t) { return (int)(TD[t.type].cost * 0.8f * t.lvl); }
static float tRange(const Tow &t) { return TD[t.type].range + 0.3f * (t.lvl - 1); }
static float hqBuff(const Tow &t) {     // штаб рядом усиливает урон
  float b = 1.0f;
  for (size_t k = 0; k < tw.size(); k++) { const Tow &h = tw[k]; if (TD[h.type].kind != K_HQ) continue; float dx = h.x - t.x, dy = h.y - t.y; float R = TD[h.type].range + 0.3f * (h.lvl - 1); if (dx * dx + dy * dy <= R * R) b += 0.2f + 0.1f * (h.lvl - 1); }
  return b;
}
static float tDmg(const Tow &t) { return TD[t.type].dmg * powf(1.6f, (float)(t.lvl - 1)) * hqBuff(t); }
static int farmPay(const Tow &t) { return t.lvl == 1 ? 25 : (t.lvl == 2 ? 50 : 90); }
static void hurt(Zom &z, float dmg, int kind) {
  if (!z.alive) return;
  if (kind <= 1) dmg *= 1.0f - ZD[z.type].armor;     // броня держит пули и дробь
  if (z.shield) dmg *= 0.5f;                         // под щитом щитоносца
  z.hp -= dmg;
  if (z.hp <= 0) { z.alive = false; money += (int)(ZD[z.type].reward * (1.0f + wave * 0.06f)); if (z.type == 6) pendingBrood.push_back(z.d); }
}
static void zxy(const Zom &z, float &x, float &y) { posAt(z.d, x, y); }
static void spawnAt(int t, float d, float hpk) { Zom z; z.d = fmaxv(0.0f, d); z.type = t; z.hpMax = z.hp = ZD[t].hp * waveHp(wave) * hpk; z.slow = 0; z.burn = 0; z.abil = 3; z.dash = 0; z.shield = false; z.alive = true; zs.push_back(z); }
static void stepGame(float dt) {
  if (zstate != ZS_PLAY || paused) return;
  if (strikeCd > 0) strikeCd -= dt;
  // появление
  if (waveOn && spawnIdx < spawnLeft) {
    spawnT -= dt;
    if (spawnT <= 0) {
      int t = spawnList[spawnIdx++]; Zom z; z.d = 0; z.type = t; z.hpMax = z.hp = ZD[t].hp * waveHp(wave); z.slow = 0; z.burn = 0; z.abil = 3.0f; z.dash = 0; z.shield = false; z.alive = true; zs.push_back(z);
      spawnT = t == 1 ? 0.45f : (isBoss(t) ? 1.5f : 0.8f);
    }
  }
  // движение
  // щит: зомби рядом с живым щитоносцем
  for (size_t i = 0; i < zs.size(); i++) zs[i].shield = false;
  for (size_t j = 0; j < zs.size(); j++) if (zs[j].alive && zs[j].type == 8) {
    float sx, sy; zxy(zs[j], sx, sy);
    for (size_t i = 0; i < zs.size(); i++) { if (i == j || !zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - sx) * (x - sx) + (y - sy) * (y - sy) < 4.0f) zs[i].shield = true; }
  }
  size_t n0 = zs.size();
  for (size_t i = 0; i < n0; i++) {
    if (!zs[i].alive) continue;
    int ty = zs[i].type; zs[i].abil -= dt;
    if (ty == 5) { if (zs[i].dash > 0) zs[i].dash -= dt; else if (zs[i].abil <= 0) { zs[i].dash = 1.0f; zs[i].abil = 4.0f; } }
    else if (ty == 7 && zs[i].abil <= 0) { zs[i].abil = 5.0f; float d = zs[i].d; for (int k = 0; k < 3; k++) spawnAt(0, d - 0.4f - k * 0.3f, 0.8f); say("НЕКРОМАНТ ПОДНИМАЕТ ЗОМБИ!"); }
    else if (ty == 9) { zs[i].hp = fminv(zs[i].hpMax, zs[i].hp + zs[i].hpMax * 0.006f * dt * (zs[i].burn > 0 ? 0.0f : 1.0f));
                        if (zs[i].abil <= 0) { zs[i].abil = 6.0f; float d = zs[i].d; for (int k = 0; k < 2; k++) spawnAt(1, d - 0.5f - k * 0.4f, 1.0f); } }
  }
  for (size_t i = 0; i < zs.size(); i++) {
    Zom &z = zs[i]; if (!z.alive) continue;
    bool noSlow = z.type == 9;
    float sp = ZD[z.type].spd * (z.slow > 0 && !noSlow ? 0.5f : 1.0f) * (z.type == 5 && z.dash > 0 ? 3.0f : 1.0f);
    if (z.slow > 0) z.slow -= dt;
    if (z.burn > 0) { z.burn -= dt; hurt(z, 3.0f * waveHp(wave) * 0.4f * dt, 2); }
    z.d += sp * dt;
    if (z.d >= pathTotal) { z.alive = false; lives -= ZD[z.type].lives; if (lives <= 0) { lives = 0; zstate = ZS_LOSE; if (wave > bestWave[mapId]) bestWave[mapId] = wave; saveProg(); } }
  }
  // башни
  for (size_t k = 0; k < tw.size(); k++) {
    Tow &t = tw[k]; t.cd -= dt;
    int kind0 = TD[t.type].kind;
    if (kind0 == K_FARM || kind0 == K_HQ) continue;                  // ферма и штаб не стреляют
    float tx = t.x + 0.5f, ty = t.y + 0.5f, R = tRange(t), best = -1; int bi = -1;
    float minR = kind0 == 7 ? 1.5f : 0.0f;                              // миномёт не бьёт вплотную
    for (size_t i = 0; i < zs.size(); i++) {
      if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); float dd = (x - tx) * (x - tx) + (y - ty) * (y - ty);
      if (dd > R * R || dd < minR * minR) continue;
      float score = kind0 == 5 ? zs[i].hp : zs[i].d;                   // снайпер целится в самого живучего
      if (score > best) { best = score; bi = (int)i; }
    }
    if (bi != t.target) t.heat = 0;
    t.target = bi;
    if (bi < 0) continue;
    float zx, zy; zxy(zs[bi], zx, zy); t.ang = atan2f(zy - ty, zx - tx);
    if (t.cd > 0) continue;
    t.cd = 1.0f / TD[t.type].rate;
    int kind = TD[t.type].kind; float dmg = tDmg(t);
    if (kind == 0) { hurt(zs[bi], dmg, 0); Fx f = {tx, ty, zx, zy, 0, 0.06f, 0}; fxs.push_back(f); }
    else if (kind == 1) {      // дробь: до трёх ближайших в радиусе
      int hits = 0;
      for (size_t i = 0; i < zs.size() && hits < 3; i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - tx) * (x - tx) + (y - ty) * (y - ty) <= R * R) { hurt(zs[i], dmg, 1); Fx f = {tx, ty, x, y, 0, 0.08f, 1}; fxs.push_back(f); hits++; } }
    } else if (kind == 2) {    // огонь: всех в радиусе + поджог
      for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - tx) * (x - tx) + (y - ty) * (y - ty) <= R * R) { hurt(zs[i], dmg, 2); zs[i].burn = 1.5f; } }
      Fx f = {tx, ty, zx, zy, 0, 0.12f, 2}; fxs.push_back(f);
    } else if (kind == 3) {    // ракета: взрыв по площади
      for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - zx) * (x - zx) + (y - zy) * (y - zy) <= 1.2f * 1.2f) hurt(zs[i], dmg, 3); }
      Fx f = {tx, ty, zx, zy, 0, 0.35f, 3}; fxs.push_back(f);
    } else if (kind == 5) {    // снайпер: один мощный выстрел, броня не спасает
      hurt(zs[bi], dmg, 5); Fx f = {tx, ty, zx, zy, 0, 0.1f, 5}; fxs.push_back(f);
    } else if (kind == 6) {    // тесла: молния перескакивает на 3 соседних зомби
      int cur = bi; float d2 = dmg; float px = tx, py = ty;
      bool used[512] = {false};
      for (int j = 0; j < 4 && cur >= 0; j++) {
        float x, y; zxy(zs[cur], x, y); hurt(zs[cur], d2, 6); used[cur & 511] = true;
        Fx f = {px, py, x, y, 0, 0.15f, 6}; fxs.push_back(f); px = x; py = y; d2 *= 0.8f;
        int nx = -1; float nd = 1.6f * 1.6f;
        for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive || used[i & 511]) continue; float x2, y2; zxy(zs[i], x2, y2); float dd = (x2 - x) * (x2 - x) + (y2 - y) * (y2 - y); if (dd < nd) { nd = dd; nx = (int)i; } }
        cur = nx;
      }
    } else if (kind == 7) {    // миномёт: навесом, большой взрыв
      for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - zx) * (x - zx) + (y - zy) * (y - zy) <= 1.5f * 1.5f) hurt(zs[i], dmg, 7); }
      Fx f = {tx, ty, zx, zy, 0, 0.6f, 7}; fxs.push_back(f);
    } else if (kind == 8) {    // лазер: чем дольше держит цель, тем сильнее жжёт
      t.heat = fminv(2.0f, t.heat + 0.05f);
      hurt(zs[bi], dmg * (1.0f + t.heat), 8); Fx f = {tx, ty, zx, zy, 0, 0.11f, 8}; fxs.push_back(f);
    } else {                   // сетка: замедляет всех в радиусе
      for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - tx) * (x - tx) + (y - ty) * (y - ty) <= R * R) { zs[i].slow = 1.2f + 0.3f * t.lvl; hurt(zs[i], dmg * t.lvl, 4); } }
      Fx f = {tx, ty, tx, ty, 0, 0.3f, 4}; fxs.push_back(f);
    }
  }
  if (!pendingBrood.empty()) { for (size_t k = 0; k < pendingBrood.size(); k++) for (int j = 0; j < 6; j++) spawnAt(1, pendingBrood[k] - j * 0.25f, 0.9f); pendingBrood.clear(); say("МАТКА ВЫПУСТИЛА БЕГУНОВ!"); }
  for (size_t i = 0; i < fxs.size();) { fxs[i].t += dt; if (fxs[i].t > fxs[i].dur) { fxs[i] = fxs.back(); fxs.pop_back(); } else i++; }
  size_t w = 0; for (size_t i = 0; i < zs.size(); i++) if (zs[i].alive) zs[w++] = zs[i]; zs.resize(w);
  // конец волны
  if (waveOn && spawnIdx >= spawnLeft && zs.empty() && zstate == ZS_PLAY) {
    waveOn = false; int bonus = 20 + wave * 3; money += bonus;
    int farm = 0; for (size_t k = 0; k < tw.size(); k++) if (TD[tw[k].type].kind == K_FARM) { farm += farmPay(tw[k]); Fx f = {tw[k].x + 0.5f, tw[k].y + 0.5f, tw[k].x + 0.5f, tw[k].y + 0.5f, 0, 1.2f, 9}; fxs.push_back(f); }
    money += farm;
    char b[64]; if (farm) snprintf(b, sizeof b, "ВОЛНА %d ОТБИТА! +$%d, ФЕРМЫ +$%d", wave, bonus, farm); else snprintf(b, sizeof b, "ВОЛНА %d ОТБИТА! +$%d", wave, bonus); say(b);
    if (wave > bestWave[mapId]) bestWave[mapId] = wave;
    if (wave >= NWAVE) { zstate = ZS_WIN; if (mapId + 1 < NMAP && unlocked < mapId + 2) unlocked = mapId + 2; }
    saveProg();
  }
}

// ---------- рисование в 3D ----------
static const float TS = 2.0f;                 // размер клетки в мире
static int VX0 = 0, VY0 = 0, VX1 = 0, VY1 = 0, PANX = 0, OY = 0;   // область 3D-картинки и панель справа
static inline float WX(float x) { return (x - GW * 0.5f) * TS; }
static inline float WZ(float y) { return (GH * 0.5f - y) * TS; }
static V3 gEye, gTgt;
static void layout() {
  OY = fs * 14; int panW = imax(fs * 62, IW / 5);
  PANX = IW - panW - fs; VX0 = 0; VY0 = OY; VX1 = PANX - fs; VY1 = IH;
  // камера сверху-спереди; фокус подбираем так, чтобы поле целиком помещалось в область
  gEye = v3(0, 27.0f, -21.0f); gTgt = v3(0, 0, 1.5f);
  setCamera(gEye, gTgt);
  float mxL = 0, mxR = 0, myT = 0, myB = 0;
  float cx[4] = {WX(-0.3f), WX(GW + 1.3f), WX(GW + 1.3f), WX(-0.3f)}, cz[4] = {WZ(-0.2f), WZ(-0.2f), WZ(GH + 0.1f), WZ(GH + 0.1f)};   // поле + домик справа
  for (int i = 0; i < 4; i++) { V3 v = toView(v3(cx[i], 0, cz[i])); float x = v.x / v.z, y = v.y / v.z; mxL = fmaxv(mxL, -x); mxR = fmaxv(mxR, x); myT = fmaxv(myT, y); myB = fmaxv(myB, -y); }
  float vw = (float)(VX1 - VX0), vh = (float)(VY1 - VY0);
  focal = fminv(vw / (mxL + mxR), vh / (myT + myB + 0.12f)) * 0.98f;
  vcx = VX0 + vw * 0.5f + (mxL - mxR) * focal * 0.5f; vcy = VY0 + vh * 0.06f + focal * (myT + 0.06f);
}
static bool pickCell(int sx, int sy, int &gx, int &gy) {      // луч из камеры через точку экрана -> клетка на земле
  V3 dir = camF + camR * ((sx - vcx) / focal) + camU * ((vcy - sy) / focal);
  if (dir.y > -0.001f) return false;
  float t = -camEye.y / dir.y; V3 p = camEye + dir * t;
  float fx = p.x / TS + GW * 0.5f, fy = GH * 0.5f - p.z / TS;
  gx = (int)floorf(fx); gy = (int)floorf(fy);
  return gx >= 0 && gy >= 0 && gx < GW && gy < GH;
}
static bool pickPoint(int sx, int sy, float &fx, float &fy) {   // точка на земле в координатах клеток
  V3 dir = camF + camR * ((sx - vcx) / focal) + camU * ((vcy - sy) / focal);
  if (dir.y > -0.001f) return false;
  float t = -camEye.y / dir.y; V3 p = camEye + dir * t;
  fx = p.x / TS + GW * 0.5f; fy = GH * 0.5f - p.z / TS;
  return fx >= 0 && fy >= 0 && fx < GW && fy < GH;
}
static bool scr(V3 p, int &x, int &y) { V3 v = toView(p); if (v.z < 0.5f) return false; x = (int)(vcx + v.x * focal / v.z); y = (int)(vcy - v.y * focal / v.z); return true; }
static void zombieHeading(const Zom &z, float &h) {
  float x0, y0, x1, y1; posAt(z.d, x0, y0); posAt(z.d + 0.05f, x1, y1);
  h = atan2f(WX(x1) - WX(x0), WZ(y1) - WZ(y0));
}
static void drawZombie3D(const Zom &z) {
  float x, y; zxy(z, x, y); float h; zombieHeading(z, h);
  const ZDef &d = ZD[z.type]; float s = d.size / 0.28f * 1.3f;     // масштаб: бегун меньше, толстяк и босс больше
  V3 p = v3(WX(x), 0, WZ(y));
  float cs = cosf(h), sn = sinf(h); V3 ax = v3(cs, 0, -sn), az = v3(sn, 0, cs);
  float walk = sinf(gTime * 9.0f * d.spd + z.d * 4.0f);
  u32 body = z.burn > 0 ? mixc(d.col, RGB(255, 120, 30), 0.5f) : (z.slow > 0 ? mixc(d.col, RGB(120, 200, 255), 0.45f) : d.col);
  u32 cloth = z.type == 3 ? RGB(80, 80, 90) : (z.type == 4 ? RGB(90, 20, 30) : RGB(70, 60, 90));
  float fat = z.type == 2 ? 1.5f : 1.0f;
  discOct(p.x, p.z, 0.45f * s * fat, 0.02f, RGB(40, 60, 30));                                            // тень
  drawBox(p + ax * (0.13f * s) + az * (walk * 0.12f * s) + v3(0, 0.35f * s, 0), v3(0.09f * s, 0.35f * s, 0.09f * s), h, RGB(50, 50, 60), true);
  drawBox(p - ax * (0.13f * s) - az * (walk * 0.12f * s) + v3(0, 0.35f * s, 0), v3(0.09f * s, 0.35f * s, 0.09f * s), h, RGB(50, 50, 60), true);
  drawBox(p + v3(0, 0.98f * s, 0), v3(0.25f * s * fat, 0.3f * s, 0.15f * s * fat), h, cloth, true);           // туловище
  drawBox(p + v3(0, 1.42f * s, 0) + az * (0.05f * s), v3(0.15f * s, 0.15f * s, 0.15f * s), h, body, true);      // голова
  for (int k = -1; k <= 1; k += 2)                                                                                // руки вперёд, как у зомби
    drawBox(p + ax * (0.32f * s * fat * k) + v3(0, 1.15f * s, 0) + az * ((0.28f + 0.04f * walk * k) * s), v3(0.07f * s, 0.07f * s, 0.26f * s), h, body, true);
  if (z.type == 3) drawBox(p + v3(0, 1.62f * s, 0), v3(0.17f * s, 0.06f * s, 0.17f * s), h, RGB(110, 110, 120), true);  // каска
  if (z.type == 4) { for (int k = -1; k <= 1; k += 2) drawBox(p + ax * (0.36f * s * k) + v3(0, 1.3f * s, 0), v3(0.12f * s, 0.12f * s, 0.12f * s), h, RGB(80, 80, 80), true); }   // наплечники громилы
  if (z.type == 5) { for (int k = -1; k <= 1; k += 2) drawBox(p + ax * (0.32f * s * k) + v3(0, 1.15f * s, 0) + az * (0.6f * s), v3(0.02f * s, 0.02f * s, 0.14f * s), h, RGB(230, 230, 230), true);   // когти охотника
                     if (z.dash > 0) drawBox(p - az * (0.6f * s) + v3(0, 1.0f * s, 0), v3(0.05f, 0.05f, 0.4f * s), h, RGB(200, 160, 255), false); }
  if (z.type == 6) drawBox(p + v3(0, 0.95f * s, 0) + az * (0.22f * s), v3(0.32f * s, 0.26f * s, 0.24f * s), h, RGB(230, 150, 170), true);   // раздутое брюхо матки
  if (z.type == 7) { drawBox(p + ax * (0.45f * s) + v3(0, 0.9f * s, 0), v3(0.03f * s, 0.9f * s, 0.03f * s), h, RGB(90, 70, 50), true);          // посох и капюшон некроманта
                     drawBox(p + ax * (0.45f * s) + v3(0, 1.85f * s, 0), v3(0.08f * s, 0.08f * s, 0.08f * s), gTime * 3, RGB(120, 255, 200), false);
                     drawBox(p + v3(0, 1.5f * s, 0) - az * (0.04f * s), v3(0.19f * s, 0.2f * s, 0.18f * s), h, RGB(30, 50, 50), true); }
  if (z.type == 8) drawBox(p + az * (0.45f * s) + v3(0, 1.0f * s, 0), v3(0.42f * s, 0.55f * s, 0.05f * s), h, RGB(160, 170, 190), true);    // большой щит
  if (z.type == 9) { drawBox(p + v3(0, 1.66f * s, 0), v3(0.17f * s, 0.08f * s, 0.17f * s), h, RGB(255, 210, 40), true);                   // корона и плащ короля
                     for (int k = 0; k < 4; k++) drawBox(p + v3(0, 1.8f * s, 0) + ax * ((k - 1.5f) * 0.1f * s), v3(0.03f * s, 0.06f * s, 0.03f * s), h, RGB(255, 220, 60), true);
                     drawBox(p - az * (0.2f * s) + v3(0, 0.9f * s, 0), v3(0.32f * s, 0.55f * s, 0.03f * s), h, RGB(150, 20, 30), true); }
  if (z.shield) discOct(p.x, p.z, 0.5f * s, 0.06f, RGB(120, 170, 255));
}
static void drawTower3D(const Tow &t) {
  V3 c = v3(WX(t.x + 0.5f), 0, WZ(t.y + 0.5f));
  u32 col = TD[t.type].col;
  drawBox(c + v3(0, 0.3f, 0), v3(0.8f, 0.3f, 0.8f), 0, RGB(70, 70, 78), true);                     // фундамент
  float yaw = t.ang + 1.5708f;                                                                        // направление на цель -> поворот в мире
  float ca = cosf(t.ang), sa = sinf(t.ang);
  V3 fwd = v3(ca, 0, -sa);
  drawBox(c + v3(0, 0.85f, 0), v3(0.5f, 0.25f, 0.5f), yaw, col, true);                            // башенка
  int kd = TD[t.type].kind;
  if (kd == K_FARM) {                                     // ферма: амбар, крыша и монета сверху
    drawBox(c + v3(0, 0.9f, 0), v3(0.62f, 0.32f, 0.55f), 0, RGB(190, 70, 50), true);
    drawBox(c + v3(0, 1.32f, 0), v3(0.7f, 0.1f, 0.62f), 0, RGB(120, 60, 40), true);
    drawBox(c + v3(0, 1.8f + 0.1f * sinf(gTime * 3), 0), v3(0.22f, 0.22f, 0.05f), gTime * 2.0f, RGB(255, 210, 60), false);
    for (int i = 0; i < t.lvl; i++) drawBox(c + v3(-0.55f + i * 0.35f, 0.64f, -0.75f), v3(0.12f, 0.05f, 0.05f), 0, RGB(255, 210, 60), false);
    return;
  }
  if (kd == K_HQ) {                                       // штаб: здание с антенной и флагом
    drawBox(c + v3(0, 1.0f, 0), v3(0.55f, 0.45f, 0.55f), 0, col, true);
    drawBox(c + v3(0.3f, 1.9f, 0.3f), v3(0.03f, 0.5f, 0.03f), 0, RGB(60, 60, 60), true);
    drawBox(c + v3(0.48f, 2.25f, 0.3f), v3(0.16f, 0.1f, 0.02f), 0, RGB(220, 40, 40), false);
    for (int i = 0; i < t.lvl; i++) drawBox(c + v3(-0.55f + i * 0.35f, 0.64f, -0.75f), v3(0.12f, 0.05f, 0.05f), 0, RGB(255, 210, 60), false);
    return;
  }
  if (t.type == 3) { for (int k = -1; k <= 1; k += 2) drawBox(c + v3(0, 1.2f, 0) + v3(-sa, 0, -ca) * (0.25f * k) + fwd * 0.2f, v3(0.12f, 0.12f, 0.45f), yaw, RGB(60, 70, 60), true); }
  else if (kd == 5) { drawBox(c + v3(0, 1.15f, 0), v3(0.22f, 0.3f, 0.22f), yaw, col, true); drawBox(c + v3(0, 1.3f, 0) + fwd * 0.8f, v3(0.05f, 0.05f, 0.75f), yaw, RGB(30, 30, 30), true); }
  else if (kd == 6) { for (int i = 0; i < 3; i++) drawBox(c + v3(0, 1.1f + i * 0.28f, 0), v3(0.3f - i * 0.06f, 0.08f, 0.3f - i * 0.06f), 0.4f * i, RGB(200, 140, 60), true);
                      drawBox(c + v3(0, 2.0f, 0), v3(0.2f, 0.2f, 0.2f), gTime * 4, RGB(170, 220, 255), false); }
  else if (kd == 7) { drawBox(c + v3(0, 1.15f, 0) + fwd * 0.2f, v3(0.18f, 0.35f, 0.18f), yaw, RGB(60, 60, 50), true); }
  else if (kd == 8) { drawBox(c + v3(0, 1.1f, 0) + fwd * 0.45f, v3(0.12f, 0.12f, 0.35f), yaw, RGB(60, 60, 70), true); drawBox(c + v3(0, 1.1f, 0) + fwd * 0.82f, v3(0.08f, 0.08f, 0.04f), yaw, RGB(255, 240, 240), false); }
  else if (t.type == 4) drawBox(c + v3(0, 1.25f, 0), v3(0.35f, 0.15f, 0.35f), yaw + 0.785f, RGB(160, 230, 250), true);
  else drawBox(c + v3(0, 0.95f, 0) + fwd * 0.6f, v3(0.09f, 0.09f, t.type == 1 ? 0.35f : 0.5f) * (t.type == 2 ? 1.6f : 1.0f), yaw, RGB(40, 40, 46), true);
  for (int i = 0; i < t.lvl; i++) drawBox(c + v3(-0.55f + i * 0.35f, 0.64f, -0.75f), v3(0.12f, 0.05f, 0.05f), 0, RGB(255, 210, 60), false);
}
static void drawTree(int x, int y, int kind) {
  V3 c = v3(WX(x + 0.5f), 0, WZ(y + 0.5f));
  if (kind == 1) {
    drawBox(c + v3(0, 0.5f, 0), v3(0.15f, 0.5f, 0.15f), 0, RGB(100, 70, 40), true);
    drawBox(c + v3(0, 1.3f, 0), v3(0.7f, 0.45f, 0.7f), 0.4f, RGB(50, 110, 50), true);
    drawBox(c + v3(0, 1.9f, 0), v3(0.45f, 0.3f, 0.45f), 0.9f, RGB(70, 140, 60), true);
  } else drawBox(c + v3(0, 0.3f, 0), v3(0.55f, 0.3f, 0.45f), 0.5f, RGB(120, 120, 128), true);
}
static void drawWorld3D() {
  const MapDef &M = MAPS[mapId];
  setClipRect(VX0, VY0, VX1, VY1);
  setCamera(gEye, gTgt);
  fogCol = RGB(40, 46, 52); fogA = 1e9f; fogB = 2e9f; gLight = 1.0f; gNight = 0.0f;
  LDIR = vnorm(v3(0.45f, 0.85f, -0.35f));
  fillSky(); clearDepth();
  // вокруг поля - тёмная земля
  groundQuad(WX(-6), WZ(GH + 4), WX(GW + 6), WZ(-6), -0.02f, shadec(M.grass, 0.55f));
  for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
    u32 c = cellPath[y][x] ? M.path : shadec(M.grass, ((x + y) & 1) ? 1.0f : 0.92f);
    groundQuad(WX(x), WZ(y), WX(x + 1), WZ(y + 1), 0, c);
  }
  for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) if (cellBlock[y][x]) drawTree(x, y, cellBlock[y][x]);
  // убежище в конце дороги
  V3 e = v3(WX(pathX[pathN - 1]) - 1.2f, 0, WZ(pathY[pathN - 1]));
  drawBox(e + v3(0, 1.0f, 0), v3(1.1f, 1.0f, 1.3f), 0, RGB(130, 80, 55), true);
  drawBox(e + v3(0, 2.2f, 0), v3(1.25f, 0.25f, 1.45f), 0, RGB(150, 40, 40), true);
  drawBox(e + v3(1.12f, 0.6f, 0), v3(0.03f, 0.5f, 0.35f), 0, RGB(255, 220, 120), false);
  for (size_t k = 0; k < tw.size(); k++) drawTower3D(tw[k]);
  for (size_t i = 0; i < zs.size(); i++) drawZombie3D(zs[i]);
  // эффекты выстрелов
  for (size_t i = 0; i < fxs.size(); i++) {
    const Fx &f = fxs[i]; float k = f.t / f.dur;
    V3 a = v3(WX(f.x0), 1.0f, WZ(f.y0)), b = v3(WX(f.x1), 1.0f, WZ(f.y1));
    if (f.kind <= 1) { V3 p = a + (b - a) * k; drawBox(p, v3(0.08f, 0.08f, 0.08f), 0, f.kind ? RGB(255, 190, 110) : RGB(255, 240, 140), false); }
    else if (f.kind == 2) { for (int j = 0; j < 5; j++) { float t2 = (j + k) / 5.0f; drawBox(a + (b - a) * t2 + v3(0, 0.1f * sinf(j * 2.0f + gTime * 20), 0), v3(0.15f, 0.15f, 0.15f) * (0.6f + t2), gTime * 5 + j, j % 2 ? RGB(255, 140, 30) : RGB(255, 210, 60), false); } }
    else if (f.kind == 3) { if (k < 0.35f) drawBox(a + (b - a) * (k / 0.35f) + v3(0, 0.6f * sinf(k / 0.35f * 3.14f), 0), v3(0.12f, 0.12f, 0.12f), 0, RGB(230, 230, 230), false);
                            else { float r = 2.4f * (k - 0.3f) / 0.7f; discOct(b.x, b.z, r, 0.04f, RGB(255, 140, 40)); drawBox(b + v3(0, r * 0.4f, 0), v3(r * 0.3f, r * 0.3f, r * 0.3f), gTime * 3, RGB(255, 200, 80), false); } }
    else if (f.kind == 4) { discOct(a.x, a.z, 0.8f + 3.0f * k, 0.05f, RGB(140, 220, 255)); }
    else if (f.kind == 5) { V3 p = a + (b - a) * k; drawBox(p + v3(0, 0.2f, 0), v3(0.06f, 0.06f, 0.06f), 0, RGB(255, 255, 255), false); }
    else if (f.kind == 7) { if (k < 0.7f) { float t2 = k / 0.7f; drawBox(a + (b - a) * t2 + v3(0, 4.0f * sinf(t2 * 3.14f), 0), v3(0.14f, 0.14f, 0.14f), 0, RGB(60, 60, 60), true); }
                            else { float r = 3.0f * (k - 0.7f) / 0.3f; discOct(b.x, b.z, r, 0.05f, RGB(200, 120, 50)); } }
    else if (f.kind == 9) { drawBox(a + v3(0, 2.0f + 2.5f * k, 0), v3(0.2f, 0.2f, 0.05f), gTime * 6, RGB(255, 220, 60), false); }
    else if (f.kind == 10) { if (k < 0.4f) drawBox(v3(b.x, 12.0f * (1 - k / 0.4f), b.z), v3(0.3f, 0.3f, 0.6f), 0, RGB(70, 70, 70), true);
                             else { float r = 4.8f * (k - 0.4f) / 0.6f; discOct(b.x, b.z, r, 0.06f, RGB(255, 120, 30)); drawBox(b + v3(0, r * 0.5f, 0), v3(r * 0.35f, r * 0.35f, r * 0.35f), gTime * 4, RGB(255, 200, 70), false); } }
  }
  // молнии теслы и лучи лазера - поверх картинки
  for (size_t i = 0; i < fxs.size(); i++) {
    const Fx &f = fxs[i]; int x0, y0, x1, y1;
    if (f.kind != 6 && f.kind != 8) continue;
    if (!scr(v3(WX(f.x0), f.kind == 6 ? 2.0f : 1.1f, WZ(f.y0)), x0, y0) || !scr(v3(WX(f.x1), 1.0f, WZ(f.y1)), x1, y1)) continue;
    if (f.kind == 8) { drawLine(x0, y0, x1, y1, RGB(255, 60, 70), imax(2, fs * 2)); drawLine(x0, y0, x1, y1, RGB(255, 230, 230), imax(1, fs)); }
    else { int px = x0, py = y0; for (int s2 = 1; s2 <= 6; s2++) { int nx = x0 + (x1 - x0) * s2 / 6 + (s2 < 6 ? (rand() % (fs * 8 + 1)) - fs * 4 : 0), ny = y0 + (y1 - y0) * s2 / 6 + (s2 < 6 ? (rand() % (fs * 8 + 1)) - fs * 4 : 0); drawLine(px, py, nx, ny, RGB(170, 220, 255), imax(1, fs)); px = nx; py = ny; } }
  }
  setClipRect(0, 0, IW, IH);
  // полоски здоровья над головами (2D поверх)
  for (size_t i = 0; i < zs.size(); i++) {
    const Zom &z = zs[i]; float x, y; zxy(z, x, y); int sx, sy;
    if (!scr(v3(WX(x), 2.6f * ZD[z.type].size / 0.28f, WZ(y)), sx, sy)) continue;
    int bw = fs * 10; fillRect(sx - bw / 2, sy, bw, imax(2, fs), RGB(60, 0, 0)); fillRect(sx - bw / 2, sy, (int)(bw * clampf(z.hp / z.hpMax, 0, 1)), imax(2, fs), RGB(80, 230, 80));
  }
  // полоска здоровья босса во всю ширину
  for (size_t i = 0; i < zs.size(); i++) if (zs[i].alive && isBoss(zs[i].type)) {
    const Zom &z = zs[i]; int bw = (VX1 - VX0) * 3 / 5, bx = (VX0 + VX1) / 2 - bw / 2, by = IH - fs * 16;
    fillRectA(bx - fs * 2, by - fs * 10, bw + fs * 4, fs * 16, RGB(0, 0, 0), 150);
    char bb[64]; snprintf(bb, sizeof bb, "БОСС: %s", ZD[z.type].name); drawText(bx + bw / 2, by - fs * 8, bb, fs, RGB(255, 200, 200), 1);
    fillRect(bx, by, bw, fs * 3, RGB(70, 0, 0)); fillRect(bx, by, (int)(bw * clampf(z.hp / z.hpMax, 0, 1)), fs * 3, RGB(230, 50, 60));
    break;
  }
  // радиус выбранной башни: точки по кругу на земле
  if (selTow >= 0 && selTow < (int)tw.size() && TD[tw[selTow].type].kind != K_FARM) {
    const Tow &t = tw[selTow]; float R = tRange(t) * TS;
    for (int a = 0; a < 72; a++) { float an = a / 72.0f * 6.283f; int sx, sy; if (scr(v3(WX(t.x + 0.5f) + cosf(an) * R, 0.05f, WZ(t.y + 0.5f) + sinf(an) * R), sx, sy)) fillRect(sx - 1, sy - 1, 3, 3, RGB(255, 255, 255)); }
  }
}
static void drawHUD() {
  char b[96];
  fillRect(0, 0, IW, OY - fs, RGB(18, 20, 24));
  fillRect(PANX - fs, OY - fs, IW - PANX + fs, IH - OY + fs, RGB(24, 26, 32));
  int hx = fs * 3;
  snprintf(b, sizeof b, "$%d", money); drawTextSh(hx, fs * 3, b, fs + 1, RGB(255, 220, 90), 0); hx += textW(b, fs + 1) + fs * 12;
  snprintf(b, sizeof b, "ЖИЗНИ %d", lives); drawTextSh(hx, fs * 3, b, fs + 1, lives > 5 ? RGB(130, 230, 130) : RGB(255, 90, 80), 0); hx += textW(b, fs + 1) + fs * 12;
  snprintf(b, sizeof b, "ВОЛНА %d/%d", wave, NWAVE); drawTextSh(hx, fs * 3, b, fs + 1, RGB(220, 220, 240), 0);
  snprintf(b, sizeof b, "%s", MAPS[mapId].name); drawTextSh(PANX + (IW - PANX) / 2, fs * 3, b, fs, RGB(170, 180, 200), 1);
  int pw = IW - PANX - fs * 2, bh = imax(fs * 16, (IH - OY) / 9), y = OY;
  if (selTow >= 0 && selTow < (int)tw.size()) {
    Tow &t = tw[selTow];
    snprintf(b, sizeof b, "%s ур.%d", TD[t.type].name, t.lvl); drawText(PANX + pw / 2, y, b, fs, RGB(255, 255, 255), 1); y += fs * 10;
    snprintf(b, sizeof b, "УРОН %d  РАДИУС %.1f", (int)tDmg(t), tRange(t)); drawText(PANX + pw / 2, y, b, imax(1, fs - 1), RGB(200, 210, 230), 1); y += fs * 9;
    if (t.lvl < 3) { snprintf(b, sizeof b, "Улучшить\n$%d", upCost(t)); if (button(PANX, y, pw, bh, b, money >= upCost(t), RGB(60, 140, 80))) { money -= upCost(t); t.lvl++; } }
    else button(PANX, y, pw, bh, "Максимум", false, RGB(60, 140, 80));
    y += bh + fs * 2;
    int refund = (int)(TD[t.type].cost * 0.7f * (1 + (t.lvl - 1) * 0.8f)); snprintf(b, sizeof b, "Продать\n$%d", refund);
    if (button(PANX, y, pw, bh, b, true, RGB(170, 80, 60))) { money += refund; tw.erase(tw.begin() + selTow); selTow = -1; }
    y += bh + fs * 2;
    if (button(PANX, y, pw, bh, "Готово", true, RGB(80, 86, 110))) selTow = -1;
  } else {
    int cw = (pw - fs) / 2, ch = imax(fs * 13, (IH - y - bh * 3 - fs * 6) / 6 - fs);
    for (int i = 0; i < NTOW; i++) {
      int cx = PANX + (i % 2) * (cw + fs), cy = y + (i / 2) * (ch + fs);
      snprintf(b, sizeof b, "%s\n$%d", TD[i].name, TD[i].cost);
      u32 c = i == selType ? TD[i].col : shadec(TD[i].col, 0.55f);
      if (button(cx, cy, cw, ch, b, true, c)) { selType = i; aiming = false;
        static const char *TIP[NTOW] = {"БЫСТРО СТРЕЛЯЕТ", "БЬЁТ ТРОИХ СРАЗУ", "ЖЖЁТ ВСЕХ РЯДОМ", "ВЗРЫВ ПО ПЛОЩАДИ", "ЗАМЕДЛЯЕТ", "ДАЛЕКО И СКВОЗЬ БРОНЮ", "МОЛНИЯ НА 4 ЗОМБИ",
          "НАВЕСОМ, НЕ ВПЛОТНУЮ", "ЖЖЁТ ВСЁ СИЛЬНЕЕ", "ДЕНЬГИ ПОСЛЕ ВОЛНЫ", "УСИЛИВАЕТ БАШНИ РЯДОМ"};
        say(TIP[i]); }
      if (i == selType) fillRect(cx, cy + ch - fs, cw, fs, RGB(255, 230, 90));
    }
    y += ((NTOW + 1) / 2) * (ch + fs);
  }
  int yb = IH - bh - fs;
  if (!waveOn && wave < NWAVE) { if (button(PANX, yb - bh - fs, pw, bh, "ВОЛНА!", true, RGB(200, 60, 50))) startWave(); }
  else if (selTow < 0) {
    if (strikeCd > 0) snprintf(b, sizeof b, "Авиаудар %d с", (int)ceilf(strikeCd)); else snprintf(b, sizeof b, aiming ? "Нажми на поле" : "Авиаудар!");
    if (button(PANX, yb - bh - fs, pw, bh, b, strikeCd <= 0, aiming ? RGB(255, 150, 40) : RGB(170, 60, 40))) { aiming = !aiming; if (aiming) say("НАЖМИ, КУДА УДАРИТЬ"); }
  }
  int hw = (pw - fs) / 2;
  if (button(PANX, yb, hw, bh, speedX == 1 ? "x1" : "x2", true, RGB(70, 90, 140))) speedX = speedX == 1 ? 2 : 1;
  if (button(PANX + hw + fs, yb, hw, bh, paused ? "ДАЛЬШЕ" : "ПАУЗА", true, RGB(70, 90, 140))) paused = !paused;
  if (msgT > 0) { int w = textW(msg, fs + 1) + fs * 8, cx = (VX0 + VX1) / 2; fillRectA(cx - w / 2, OY + fs * 4, w, fs * 14, RGB(0, 0, 0), 170); drawText(cx, OY + fs * 8, msg, fs + 1, RGB(255, 255, 255), 1); }
}
static void frameTitle() {
  fillRect(0, 0, IW, IH, RGB(24, 28, 22));
  for (int i = 0; i < 14; i++) { Zom z; z.d = fmodf(gTime * 0.6f + i * 1.3f, 18.0f); z.type = i % 4; z.hp = z.hpMax = 1; z.slow = z.burn = 0; z.alive = true; float x = z.d - 1, y = 7.6f; int px = (int)(x * IW / 16), py = (int)(y * IH / 9); int r = imax(4, (int)(ZD[z.type].size * IH / 9)); fillCircle(px, py, r, ZD[z.type].col, 255); fillCircle(px - r / 3, py - r / 4, imax(1, r / 5), RGB(255, 60, 40), 255); fillCircle(px + r / 3, py - r / 4, imax(1, r / 5), RGB(255, 60, 40), 255); }
  drawTextSh(IW / 2, IH / 10, "ЗОМБИ-ОБОРОНА", fs * 3, RGB(140, 230, 120), 1);
  drawText(IW / 2, IH / 10 + fs * 26, "СТАВЬ БАШНИ И ДЕРЖИ ОБОРОНУ 30 ВОЛН", fs, RGB(220, 220, 220), 1);
  int bw = IW / 4, bh = IH / 6, gap = fs * 4, x0 = IW / 2 - (bw * 3 + gap * 2) / 2, y0 = IH * 2 / 5;
  char b[96];
  for (int m = 0; m < NMAP; m++) {
    bool open = m < unlocked;
    if (open) snprintf(b, sizeof b, "%s\nРЕКОРД %d/30", MAPS[m].name, bestWave[m]); else snprintf(b, sizeof b, "%s\nЗАКРЫТО", MAPS[m].name);
    if (button(x0 + m * (bw + gap), y0, bw, bh, b, open, m == 0 ? RGB(70, 130, 60) : (m == 1 ? RGB(80, 90, 120) : RGB(130, 100, 70)))) startGame(m);
  }
  drawText(IW / 2, y0 + bh + fs * 8, "ПРОЙДИ 30 ВОЛН, ЧТОБЫ ОТКРЫТЬ СЛЕДУЮЩУЮ КАРТУ", fs, RGB(170, 180, 190), 1);
}
static void frameGame(float dt) {
  layout();
  if (zstate == ZS_PLAY && P.clicked && P.cx < VX1 && P.cy >= VY0) {
    int gx, gy;
    if (aiming) {
      float fx2, fy2;
      if (pickPoint(P.cx, P.cy, fx2, fy2)) {
        float dmg = 260.0f * waveHp(imax(1, wave));
        for (size_t i = 0; i < zs.size(); i++) { if (!zs[i].alive) continue; float x, y; zxy(zs[i], x, y); if ((x - fx2) * (x - fx2) + (y - fy2) * (y - fy2) <= 2.4f * 2.4f) hurt(zs[i], dmg, 3); }
        Fx f = {fx2, fy2 - 6.0f, fx2, fy2, 0, 1.0f, 10}; fxs.push_back(f);
        strikeCd = 45.0f; aiming = false; say("АВИАУДАР!");
      }
      P.clicked = false;
    }
    else if (pickCell(P.cx, P.cy, gx, gy)) {
      int found = -1; for (size_t k = 0; k < tw.size(); k++) if (tw[k].x == gx && tw[k].y == gy) found = (int)k;
      if (found >= 0) selTow = found;
      else if (selTow >= 0) selTow = -1;
      else if (cellPath[gy][gx] || cellBlock[gy][gx]) say("ЗДЕСЬ СТРОИТЬ НЕЛЬЗЯ");
      else if (money < TD[selType].cost) say("НЕ ХВАТАЕТ ДЕНЕГ");
      else { Tow t; t.x = gx; t.y = gy; t.type = selType; t.lvl = 1; t.cd = 0; t.ang = 0; t.target = -1; t.heat = 0; tw.push_back(t); money -= TD[selType].cost; }
    }
    P.clicked = false;
  }
  int steps = speedX; for (int s = 0; s < steps; s++) stepGame(dt);
  if (msgT > 0) msgT -= dt;
  drawWorld3D();
  drawHUD();
  if (zstate == ZS_WIN || zstate == ZS_LOSE) {
    fillRectA(0, 0, IW, IH, RGB(0, 0, 0), 170);
    drawTextSh(IW / 2, IH / 4, zstate == ZS_WIN ? "ПОБЕДА! УБЕЖИЩЕ ВЫСТОЯЛО" : "ЗОМБИ ПРОРВАЛИСЬ", fs * 2, zstate == ZS_WIN ? RGB(140, 240, 140) : RGB(255, 100, 90), 1);
    char b[64]; snprintf(b, sizeof b, "ВОЛНА %d ИЗ %d", wave, NWAVE); drawText(IW / 2, IH / 4 + fs * 22, b, fs + 1, RGB(230, 230, 230), 1);
    if (button(IW / 2 - IW / 6, IH / 2, IW / 3, IH / 7, "Ещё раз", true, RGB(70, 130, 60))) startGame(mapId);
    if (button(IW / 2 - IW / 6, IH / 2 + IH / 7 + fs * 4, IW / 3, IH / 7, "Карты", true, RGB(80, 86, 110))) zstate = ZS_TITLE;
  }
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  if (!gfxInit()) return 1;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  rotMode = SH > SW;
  LW = rotMode ? SH : SW; LH = rotMode ? SW : SH;
  computeSize();
  if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL));
  loadProg();
  memset(&P, 0, sizeof P);
  bool running = true;
  Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 5) saveProg();
      else if (e.type == 4) {   // кнопка «назад»
        if (zstate == ZS_TITLE) running = false;
        else if (zstate == ZS_PLAY && selTow >= 0) selTow = -1;
        else if (zstate == ZS_PLAY) { if (!paused) paused = true; else { saveProg(); zstate = ZS_TITLE; } }
        else zstate = ZS_TITLE;
      } else {
        int mx, my; mapPt(e.x, e.y, mx, my);
        if (e.type == 1) { P.down = true; P.pressed = true; P.x = P.sx = mx; P.y = P.sy = my; }
        else if (e.type == 3) { P.x = mx; P.y = my; }
        else if (e.type == 2) { P.x = mx; P.y = my; P.cx = mx; P.cy = my; P.down = false; P.clicked = true; }
      }
    }
    if (zstate == ZS_TITLE) frameTitle(); else frameGame(dt);
    gfxPresent();
    Uint32 spent = SDL_GetTicks() - now;
    if (spent < 16) SDL_Delay(16 - spent);
  }
  saveProg();
  gfxQuit();
  return 0;
}
