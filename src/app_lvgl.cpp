// =====================================================================
//  app_lvgl.cpp  -  LVGL v8 user interface for the ESP32 Pixel Pet
//
//  This is the LVGL rewrite of the original immediate-mode TFT_eSPI UI in
//  main.cpp. It is compiled when USE_LVGL == 1 (see config.h) and then owns
//  setup()/loop(); when USE_LVGL == 0 main.cpp provides them instead.
//
//  How the two worlds fit together
//  -------------------------------
//  * TFT_eSPI stays the low level driver (panel init, rotation, pixel push)
//    and LVGL paints through a 160x40 px draw buffer (see lv_port.cpp);
//  * the animated face keeps being rendered by EyeEngine into its own RGB565
//    sprite. Every screen that wants eyes owns an lv_canvas widget whose buffer
//    *is* eyes.pixels(), so after eyes.draw() the canvas is invalidated and
//    LVGL blits the freshly painted pixels (no extra copy, no extra RAM);
//  * the status bars, the menu and the info pages are real LVGL widgets
//    (lv_bar / lv_btn / lv_label) and inherit the default theme;
//  * the mini game (game.cpp) keeps drawing straight onto the panel: it is a
//    full screen real time loop that never shares the screen with a widget.
//
//  Key handling
//  ------------
//  * menu screen: the key edges are pushed into LVGL's keypad queue
//    (lv_port_poll_keys()). The focus group walks the menu buttons and the
//    item confirmed with [A] is executed on the *next* frame through
//    `pendingAction`, so no screen can ever change from inside an LVGL event;
//  * all other screens poll the keys directly, exactly like the legacy UI.
//    That keeps the real time screens (welcome / face / sleep / emote / music
//    / game) free of queued keys and of input latency.
//
//  Frame order (see loop()): inputs -> pet tick -> lv_timer_handler() ->
//  screen handler. The keys the menu polls at the end of frame N are therefore
//  processed by LVGL at the start of frame N+1, i.e. one ~30 ms frame later.
// =====================================================================
#include "config.h"

#if USE_LVGL

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>      // strcmp: the UI_SWEEP self-check compares text

#include "buzzer.h"
#include "buttons.h"
#include "sensors.h"
#include "eyes.h"
#include "pet.h"
#include "game.h"
#include "settings.h"     // cfg: user tunables (talk / expression / growth)
#include "netconfig.h"    // soft-AP + settings web page
#include "ui.h"          // UI::statColor()
#include "cnfont.h"      // CN_GLYPH_W / CN_GLYPH_H: the CJK font cell size
#include "lv_font_cn.h"
#include "lv_port.h"
#include "uistyle.h"     // design tokens: colours / radii / rhythm / type scale
#include "careaction.h"  // feed / wash / medicine: animation -> stat -> face

#if USE_SD
#include <SPI.h>
#include <SD.h>
#endif

// ---------------------------------------------------------------- palette
// TFT_eSPI colour (RGB565, red in the high bits) -> lv_color_t.
static inline lv_color_t C(uint16_t c) {
  return lv_color_make((uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC),
                       (uint8_t)((c << 3) & 0xF8));
}

// ---------------------------------------------------------------- layout
#define HDR_H    UI_HDR_H             // header band (title + "B:返回")
#define INFO_Y   UI_INFO_Y            // first text row of an info page
#define MENU_ROW UI_ROW_H             // menu row pitch
#define MENU_Y   UI_INFO_Y            // first menu row

// A screen change fades in (LV_SCR_LOAD_ANIM_FADE_IN) instead of hard cutting;
// 0 switches instantly again. The game screen is *always* switched instantly,
// because it paints the panel itself right after the switch.
#define SCR_FADE_MS 140

// ---------------------------------------------------------------- design
// Every colour, radius, spacing step, font and icon of the interface lives in
// uistyle.h; the aliases below are the ones the screens use most, so the code
// reads like a sketch of the layout. R_CARD and CHIP_H are the two shapes the
// whole UI is built from: a 5 px radius card and an 8 px pill "chip" that
// frames every hint line like a hardware key. shade() (also in uistyle.h)
// derives the lit state of a colour from it, so no second palette is needed.
#define R_CARD UI_R_CARD              // card / focused row radius
#define CHIP_H UI_CHIP_H              // hint chip height

// ---------------------------------------------------------------- globals
TFT_eSPI  tft;
EyeEngine eyes(&tft);
Pet       pet;
CatchGame game;

// The three care actions (feed / wash / medicine) run as a shared three beat
// state machine: animation first, stat second, expression third (careaction.h).
// Keeping it out of this file means the legacy TFT_eSPI UI runs the very same
// sequence, only its redraw differs.
static CareAction care(pet, eyes);

enum AppScreen {
  SCR_WELCOME, SCR_FACE, SCR_MENU, SCR_STAT, SCR_SLEEP,
  SCR_EMOTE, SCR_MUSIC, SCR_ABOUT, SCR_CANT, SCR_RESULT, SCR_GAME, SCR_NET,
  SCR_COUNT
};
static AppScreen screen = SCR_WELCOME;

// one LVGL screen object per AppScreen; faceView[] holds the canvas that shows
// the animated face (nullptr on the screens that do not show it).
static lv_obj_t* scr[SCR_COUNT]      = { nullptr };
static lv_obj_t* faceView[SCR_COUNT] = { nullptr };

// menu --------------------------------------------------------------------
static const char* const MENU[] = {
  "喂食", "玩耍", "睡觉", "洗澡", "吃药",
  "状态", "音乐", "表情", "网络", "关于", "重来"
};
// One Font Awesome glyph per row, in the same order - so a row reads as "icon +
// word" like a settings list. LV_SYMBOL_* lives inside LVGL's own Montserrat
// fonts (LV_FONT_MONTSERRAT_14), so each icon costs no flash, no image asset and
// no second font: see the "icons" line of uistyle.h.
static const char* const MENU_ICON[] = {
  LV_SYMBOL_BELL,       // 喂食  the dinner bell
  LV_SYMBOL_PLAY,       // 玩耍  the mini game
  LV_SYMBOL_EYE_CLOSE,  // 睡觉  eyes shut
  LV_SYMBOL_TINT,       // 洗澡  a drop of water
  LV_SYMBOL_PLUS,       // 吃药  the medical sign
  LV_SYMBOL_BARS,       // 状态  the five bars
  LV_SYMBOL_AUDIO,      // 音乐  headphones
  LV_SYMBOL_IMAGE,      // 表情  the expression gallery
  LV_SYMBOL_WIFI,       // 网络  the config AP
  LV_SYMBOL_FILE,       // 关于  the version sheet
  LV_SYMBOL_REFRESH     // 重来  start over
};
#define MENU_N ((int)(sizeof(MENU) / sizeof(MENU[0])))
static_assert(sizeof(MENU_ICON) / sizeof(MENU_ICON[0]) == (size_t)MENU_N,
              "MENU and MENU_ICON must stay in step");
static int       menuSel = 0;
static lv_obj_t* menuBtn[MENU_N]  = { nullptr };
static lv_obj_t* menuLbl[MENU_N]  = { nullptr };   // the icon + word of the row
static lv_obj_t* menuMark[MENU_N] = { nullptr };   // the focus marker bar
static int       pendingAction = -1;   // menu entry to run after LVGL's events
static bool      focusMoved = false;   // set by the FOCUSED event handler

// expression gallery ------------------------------------------------------
static const EyeMood EMOTE_ORDER[] = {
  MOOD_NEUTRAL, MOOD_HAPPY, MOOD_LOVE, MOOD_SURPRISED, MOOD_WINK,
  MOOD_CURIOUS, MOOD_SAD, MOOD_ANGRY, MOOD_SLEEPY, MOOD_DIZZY,
  MOOD_SICK, MOOD_DEAD
};
#define EMOTE_N ((int)(sizeof(EMOTE_ORDER) / sizeof(EMOTE_ORDER[0])))
static int       emoteIdx = 0;
static lv_obj_t* emoteLabel = nullptr;
static lv_obj_t* emoteDot[EMOTE_N] = { nullptr };  // gallery position indicator
static lv_obj_t* emoteCount = nullptr;             // "3/12" - the page's number

// music -------------------------------------------------------------------
static const uint8_t SONGS[] = { SND_SONG1, SND_SONG2, SND_SONG3 };
#define SONG_N ((int)(sizeof(SONGS) / sizeof(SONGS[0])))
static int       songSel = 0;
static lv_obj_t* musicName  = nullptr;
static lv_obj_t* musicState = nullptr;
static lv_obj_t* eqBar[8]   = { nullptr };  // the music screen's level meter

// face screen panel -------------------------------------------------------
static lv_obj_t* faceStage    = nullptr;   // stage name  (top left)
static lv_obj_t* faceStatus   = nullptr;   // status word (top right)
static lv_obj_t* statBar[5]   = { nullptr };
static lv_obj_t* sleepEnergy  = nullptr;
static lv_obj_t* welcomeHint  = nullptr;

// info pages: a title plus a pool of 12 rows that are filled in on entry. A
// *data* page uses a row as "dim label (left) + bright value (right)"; a
// *message* page uses it as one centred line, one of which is emphasised (the
// "hero": the score, the verdict) and rendered a size larger.
struct InfoPage {
  lv_obj_t* scr;            // (static storage -> zero initialised)
  lv_obj_t* title;
  lv_obj_t* line[12];       // left column / the whole centred line
  lv_obj_t* val[12];        // right column of a data page (null on messages)
  int       n;
  bool      full;           // true: full screen centred layout (message pages)
  int       hero;           // message pages: index of the emphasised line
};
static InfoPage pgStat, pgCant, pgResult;   // the About page is static (buildAbout)

// timing ------------------------------------------------------------------
static uint32_t lastFrame = 0, statusMs = 0, sensorMs = 0, animMs = 0, hudMs = 0;
static uint32_t saveMs = 0, talkMs = 0, welcomeMs = 0, flairMs = 0;
static int      welcomeStage = 0;
static EyeMood  lastExpr = MOOD_COUNT;

// network config screen ---------------------------------------------------
static lv_obj_t* netLine[5] = { nullptr };

// game --------------------------------------------------------------------
enum { GPA_INTRO, GPA_PLAY, GPA_RESULT };
static int  gamePhase = GPA_INTRO;
static bool gameRewarded = false;

// prototypes --------------------------------------------------------------
static void setScreen(AppScreen s);
static void enterFace();
static void openMenu();
static void menuAction(int sel);
static void startGame();
static void showStatus();
static void showMusic();
static void showEmote();
static void showAbout();
static void showNet();
static void showCantRestart();
static void showGameResult();
static void updateStatBars_();
static void petSpeak();

// =====================================================================
//  small widget helpers
// =====================================================================
// forward declarations (mkHeader uses them, they are defined just below)
static lv_obj_t* mkLabel(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, uint16_t col, lv_text_align_t align);
static void      setLabel(lv_obj_t* l, const char* txt, uint16_t col);

// A screen with the opaque pet background. SCROLLABLE and CLICKABLE are
// cleared: a static container must not react to the keypad or to scrolling.
static lv_obj_t* mkScreen(uint16_t bg) {
  lv_obj_t* s = lv_obj_create(NULL);
  lv_obj_set_size(s, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(s, C(bg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_set_style_radius(s, 0, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return s;
}

// A 1 px hairline. Drawn as a plain rect: a separator must not bring border,
// padding or radius defaults of its own.
static void mkHair(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                   uint16_t col) {
  lv_obj_t* l = lv_obj_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, w, 1);
  lv_obj_set_style_bg_color(l, C(col), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(l, 0, 0);
  lv_obj_set_style_radius(l, 0, 0);
  lv_obj_set_style_pad_all(l, 0, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

// The little rounded colour bar used as a title / focus marker.
static lv_obj_t* mkAccentBar(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t h, uint16_t col) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, 3, h);
  lv_obj_set_style_radius(b, 1, 0);
  lv_obj_set_style_bg_color(b, C(col), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return b;
}

// A rounded "card": vertical gradient + hairline edge. Children are positioned
// absolutely (the card has no padding), so it stays a pure decoration.
static lv_obj_t* mkCard(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, lv_coord_t h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_radius(c, R_CARD, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, C(UI_EDGE), 0);
  lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(c, C(UI_CARD1), 0);
  lv_obj_set_style_bg_grad_color(c, C(UI_CARD2), 0);
  lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_set_style_shadow_width(c, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return c;
}

// A pill shaped "chip" that frames a hint line - reads like a hardware key
// instead of a bare sentence.
static lv_obj_t* mkChip(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                        lv_coord_t w, const char* txt, uint16_t col) {
  lv_obj_t* c = mkCard(parent, x, y, w, CHIP_H);
  lv_obj_set_style_radius(c, UI_R_PILL, 0);       // = half of the chip height
  lv_obj_set_style_bg_color(c, C(UI_CARD2), 0);
  lv_obj_set_style_bg_grad_color(c, C(UI_CARD1), 0);
  lv_obj_t* l = mkLabel(c, 0, 1, w - 2, col, LV_TEXT_ALIGN_CENTER);
  setLabel(l, txt, col);
  return c;
}

// 18 px header: a soft gradient band with a hairline foot, the title behind an
// accent bar (the same bar the focused menu row uses, i.e. one "you are on this
// item" mark for the whole interface) and an optional "B:返回" hint on the
// right. Every data page of the pet therefore opens with the same title bar.
static void mkHeader(lv_obj_t* s, const char* title, const char* right) {
  lv_obj_t* band = lv_obj_create(s);
  lv_obj_set_pos(band, 0, 0);
  lv_obj_set_size(band, SCR_W, HDR_H);
  lv_obj_set_style_radius(band, 0, 0);
  lv_obj_set_style_border_width(band, 0, 0);
  lv_obj_set_style_pad_all(band, 0, 0);
  lv_obj_set_style_bg_color(band, C(UI_HDR1), 0);
  lv_obj_set_style_bg_grad_color(band, C(UI_HDR2), 0);
  lv_obj_set_style_bg_grad_dir(band, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
  lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  mkHair(s, 0, HDR_H - 1, SCR_W, UI_EDGE);
  mkAccentBar(s, UI_PAD, 3, CN_GLYPH_H - 4, COL_ACCENT);

  lv_obj_t* t = mkLabel(s, 11, 1, SCR_W - 15, COL_TEXT, LV_TEXT_ALIGN_LEFT);
  setLabel(t, title, COL_TEXT);
  if (right) {
    lv_obj_t* r = mkLabel(s, 4, 1, SCR_W - 8, COL_DIM, LV_TEXT_ALIGN_RIGHT);
    setLabel(r, right, COL_DIM);
  }
}

// A transparent, non scrollable label with an explicit box so it can be
// clipped / right aligned without ever resizing its neighbours.
static lv_obj_t* mkLabel(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, uint16_t col, lv_text_align_t align) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_font(l, &lv_font_cn, 0);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_set_style_text_color(l, C(col), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(l, 0, 0);
  lv_label_set_text(l, "");
  return l;
}

// Cheap guard: only touch the widget when the text really changed, so a label
// refreshed every frame does not invalidate its area every frame.
static void setLabel(lv_obj_t* l, const char* txt, uint16_t col) {
  if (!l || !txt) return;
  lv_obj_set_style_text_color(l, C(col), 0);
  const char* cur = lv_label_get_text(l);
  if (!cur || strcmp(cur, txt) != 0) lv_label_set_text(l, txt);
}

// --- the shared design layer ---------------------------------------------
// A label with an explicit font. The type scale is deliberately tiny (see
// uistyle.h): words in the 16 px CJK font, numbers in Montserrat, and exactly
// one "hero" number per page - that is what gives a 160x128 panel a hierarchy.
static lv_obj_t* mkLabelF(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t w, uint16_t col, lv_text_align_t align,
                          const lv_font_t* font) {
  lv_obj_t* l = mkLabel(parent, x, y, w, col, align);
  lv_obj_set_style_text_font(l, font, 0);
  return l;
}

// Write a label whose font follows its content: pure ASCII (every value on a
// data page: "45%", "12m") lands in the sharper Montserrat, text with CJK in
// the CJK font - so mixed strings keep working.
static void setValue(lv_obj_t* l, const char* txt, uint16_t col) {
  if (!l || !txt) return;
  const lv_font_t* f = uiFont(txt);
  if (lv_obj_get_style_text_font(l, 0) != f) lv_obj_set_style_text_font(l, f, 0);
  setLabel(l, txt, col);
}

// The 120 ms ease-out fade for everything the focus flips (menu pill, icon,
// text, chevron): a row lights up instead of snapping. One shared style, so it
// costs a single style object however many widgets add it.
static lv_style_t* uiStyleFade() {
  static lv_style_t st;
  static lv_style_transition_dsc_t tr;
  static const lv_style_prop_t PROPS[] = {
    LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR, LV_STYLE_BORDER_OPA,
    LV_STYLE_BORDER_COLOR, LV_STYLE_OPA, LV_STYLE_TEXT_COLOR, LV_STYLE_PROP_INV
  };
  static bool init = false;
  if (!init) {
    init = true;
    lv_style_init(&st);
    lv_style_transition_dsc_init(&tr, PROPS, lv_anim_path_ease_out, 120, 0, NULL);
    lv_style_set_transition(&st, &tr);
  }
  return &st;
}

// A looping opacity animation - the "z z z" of the sleep screen is three of
// these, staggered by their delay, which is what makes the pet breathe.
static void animOpaCb(void* var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)v, 0);
}

static void animOpaLoop(lv_obj_t* o, lv_opa_t from, lv_opa_t to, uint32_t ms,
                        uint32_t delay) {
  if (!o) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, animOpaCb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_playback_time(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// The equaliser bars of the music screen: the callback sets the height *and*
// keeps the bottom edge pinned (EQ_BASE_Y, see buildMusic), so no alignment is
// involved and the bars grow upwards from the baseline.
#define EQ_BASE_Y (STATUS_Y + 18)
static void animEqCb(void* var, int32_t v) {
  lv_obj_t* o = (lv_obj_t*)var;
  lv_obj_set_height(o, (lv_coord_t)v);
  lv_obj_set_y(o, EQ_BASE_Y - (lv_coord_t)v);
}

static void animEqLoop(lv_obj_t* o, int lo, int hi, uint32_t ms, uint32_t delay) {
  if (!o) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, animEqCb);
  lv_anim_set_values(&a, lo, hi);
  lv_anim_set_time(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_playback_time(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

// The music level meter only moves while something is playing, so the screen
// never lies. Called from showMusic() / frameMusic() with buzzerBusy(); it does
// nothing when the state did not change, which is what keeps the running
// animations from restarting every poll.
#define EQ_LO 3
#define EQ_HI 15
static bool eqPlaying = false;

static void eqSetPlaying(bool playing) {
  if (playing == eqPlaying) return;
  eqPlaying = playing;
  for (int i = 0; i < (int)(sizeof(eqBar) / sizeof(eqBar[0])); i++) {
    lv_obj_t* b = eqBar[i];
    if (!b) continue;
    lv_anim_del(b, animEqCb);                    // drop the old loop ...
    if (playing) {
      animEqLoop(b, EQ_LO, EQ_HI - (i % 3) * 4, 380 + (i % 4) * 90, i * 90);
    } else {
      animEqCb(b, EQ_LO);                        // ... and lie flat again
    }
  }
}

// A 8 px tall bar. lv_bar animates its own indicator, so the caller can just
// keep assigning values and LVGL eases between them (LV_ANIM_OFF keeps it in
// step with the model and avoids a backlog of tweens).
static lv_obj_t* mkBar(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                       lv_coord_t w, uint16_t col) {
  lv_obj_t* b = lv_bar_create(parent);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, 9);
  lv_bar_set_range(b, 0, 100);
  lv_bar_set_value(b, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);         // pill shaped groove
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_bg_color(b, C(UI_TRACK), 0);            // the empty track
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  // filled part: same pill shape, lit from the top
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, C(col), LV_PART_INDICATOR); // the filled part
  lv_obj_set_style_bg_grad_color(b, C(shade(col, 5)), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  return b;
}

static void setBar(lv_obj_t* b, int v, uint16_t col) {
  if (!b) return;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  lv_obj_set_style_bg_color(b, C(col), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(b, C(shade(col, 5)), LV_PART_INDICATOR);
  if (lv_bar_get_value(b) != v) lv_bar_set_value(b, v, LV_ANIM_OFF);
}

// The scene is shown by a canvas whose buffer *is* the EyeEngine sprite, so
// EyeEngine keeps owning the pixel memory. Because the canvas is an image of
// a variable source, LVGL's built in decoder hands out that pointer directly
// (no copy), which is why refreshing the canvas is all that is needed after a
// render. The canvas is the full screen: the pet itself sits in the upper part
// and everything below it is scene background, which is what the status HUD is
// drawn on top of (the widgets are simply created after the canvas).
static lv_obj_t* mkFaceView(lv_obj_t* parent) {
  lv_obj_t* cv = lv_canvas_create(parent);
  lv_obj_clear_flag(cv, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_canvas_set_buffer(cv, eyes.pixels(), FACE_W, PET_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_size(cv, FACE_W, PET_H);
  lv_obj_set_pos(cv, 0, 0);         // the scene occupies the whole screen
  return cv;
}

static void faceInvalidate(lv_obj_t* cv) {
  if (!cv) return;
  // The cache only stores the decoder descriptor, never the pixels, so this
  // just drops the (cheap) "already opened" shortcut.
  lv_img_cache_invalidate_src(lv_canvas_get_img(cv));
  lv_obj_invalidate(cv);
}

// =====================================================================
//  screen construction  (runs once, from setup())
// =====================================================================
static void buildWelcome() {
  lv_obj_t* s = scr[SCR_WELCOME] = mkScreen(COL_BG);
  faceView[SCR_WELCOME] = mkFaceView(s);

  lv_obj_t* t = mkLabel(s, 0, STATUS_Y + 8, SCR_W, COL_ACCENT, LV_TEXT_ALIGN_CENTER);
  setLabel(t, FW_NAME, COL_ACCENT);
  mkHair(s, SCR_W / 2 - 22, STATUS_Y + 24, 44, COL_ACCENT);   // short underline
  lv_obj_t* v = mkLabel(s, 0, STATUS_Y + 26, SCR_W, COL_DIM, LV_TEXT_ALIGN_CENTER);
  setLabel(v, "v" FW_VERSION, COL_DIM);

  // the hint sits in a chip, so it reads like a button and not like a sentence;
  // a slow breath makes it the one thing the eye is drawn to on this screen
  const int cw = SCR_W - 68;
  welcomeHint = lv_obj_get_child(mkChip(s, (SCR_W - cw) / 2, STATUS_Y + 44, cw,
                                        "A开始", COL_TEXT), 0);
  animOpaLoop(welcomeHint, LV_OPA_40, LV_OPA_COVER, 900, 0);
}

static void buildFace() {
  lv_obj_t* s = scr[SCR_FACE] = mkScreen(COL_BG);
  faceView[SCR_FACE] = mkFaceView(s);

  // status panel over the lower half of the scene: the widgets are transparent,
  // so the pet's background shows through them (no black band any more)
  lv_obj_t* p = lv_obj_create(s);
  lv_obj_set_pos(p, 0, STATUS_Y);
  lv_obj_set_size(p, SCR_W, STATUS_H);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_all(p, 0, 0);
  lv_obj_set_style_radius(p, 0, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  faceStage  = mkLabel(p, 4, 1, SCR_W - 8, COL_TEXT, LV_TEXT_ALIGN_LEFT);
  faceStatus = mkLabel(p, 4, 1, SCR_W - 8, COL_DIM, LV_TEXT_ALIGN_RIGHT);

  static const char* const NAMES[5] = { "饥饿", "快乐", "体力", "健康", "清洁" };
  const int barTop = CN_GLYPH_H + 2;
  const int rowH   = (STATUS_H - barTop) / 3;
  const int colW   = SCR_W / 2;
  for (int i = 0; i < 5; i++) {
    const int c = i & 1, r = i >> 1;
    const int lx = c * colW + 4;
    const int ly = barTop + r * rowH;
    lv_obj_t* l = mkLabel(p, lx, ly, 2 * CN_GLYPH_W, COL_DIM, LV_TEXT_ALIGN_LEFT);
    setLabel(l, NAMES[i], COL_DIM);
    statBar[i] = mkBar(p, lx + 2 * CN_GLYPH_W + 2, ly + 4,
                       colW - 2 * CN_GLYPH_W - 10, COL_GOOD);
  }
}

static void buildSleep() {
  lv_obj_t* s = scr[SCR_SLEEP] = mkScreen(COL_BG);
  faceView[SCR_SLEEP] = mkFaceView(s);

  // The "z z z" breathes instead of sitting still - the one visible sign that
  // the pet is asleep, since the face itself is already closed eyed.
  lv_obj_t* z = mkLabel(s, 0, STATUS_Y + 8, SCR_W, COL_ACCENT, LV_TEXT_ALIGN_CENTER);
  setLabel(z, "z z z ...", COL_ACCENT);
  animOpaLoop(z, LV_OPA_30, LV_OPA_COVER, 1100, 0);

  sleepEnergy = mkLabel(s, 0, STATUS_Y + 28, SCR_W, COL_DIM, LV_TEXT_ALIGN_CENTER);
  setLabel(sleepEnergy, "体力 100%", COL_DIM);
  const int cw = SCR_W - 40;
  mkChip(s, (SCR_W - cw) / 2, STATUS_Y + 46, cw, "A起床 B返回", COL_DIM);
}

// Position indicator of the expression gallery - one dot per mood with the
// current one filled. Twelve 4 px objects are cheaper than any layout that
// would do the same, and they tell the user how far along the gallery they are,
// which the "左右切换" hint alone does not. The colour flip fades (uiStyleFade).
static void emoteDots() {
  for (int i = 0; i < EMOTE_N; i++) {
    lv_obj_t* d = emoteDot[i];
    if (!d) continue;
    const bool on = (i == emoteIdx);
    lv_obj_set_style_bg_color(d, C(on ? COL_ACCENT : UI_DOT), 0);
    lv_obj_set_style_bg_opa(d, on ? LV_OPA_COVER : LV_OPA_60, 0);
  }
  // ... and the same position again as the page's one number ("3/12"), right
  // aligned on the mood row: a glance tells you how many moods are left.
  char b[12];
  snprintf(b, sizeof b, "%d/%d", emoteIdx + 1, EMOTE_N);
  setLabel(emoteCount, b, COL_DIM);
}

static void buildEmote() {
  lv_obj_t* s = scr[SCR_EMOTE] = mkScreen(COL_BG);
  faceView[SCR_EMOTE] = mkFaceView(s);

  emoteLabel = mkLabel(s, 0, STATUS_Y + 14, SCR_W, COL_ACCENT, LV_TEXT_ALIGN_CENTER);
  setLabel(emoteLabel, moodName(MOOD_NEUTRAL), COL_ACCENT);
  // the counter shares the mood row but owns the right edge (the CJK name is
  // centred and only ~2 cells wide, so the two never meet). montserrat_14 has
  // exactly the 16 px line height of the CJK cell, so both sit on the same y.
  emoteCount = mkLabelF(s, UI_PAD, STATUS_Y + 14, SCR_W - 2 * UI_PAD, COL_DIM,
                        LV_TEXT_ALIGN_RIGHT, UI_TXT_SM);

  const int n = EMOTE_N, pitch = 7;
  const int x0 = (SCR_W - (n * 4 + (n - 1) * (pitch - 4))) / 2;
  for (int i = 0; i < n; i++) {
    lv_obj_t* d = lv_obj_create(s);
    lv_obj_set_pos(d, x0 + i * pitch, STATUS_Y + 33);
    lv_obj_set_size(d, 4, 4);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_pad_all(d, 0, 0);
    lv_obj_set_style_bg_color(d, C(UI_DOT), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_60, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(d, uiStyleFade(), 0);
    emoteDot[i] = d;
  }
  emoteDots();

  const int cw = SCR_W - 20;
  mkChip(s, (SCR_W - cw) / 2, STATUS_Y + 42, cw, "左右切换 B返回", COL_DIM);
}

static void buildMusic() {
  lv_obj_t* s = scr[SCR_MUSIC] = mkScreen(COL_BG);
  faceView[SCR_MUSIC] = mkFaceView(s);

  lv_obj_t* t = mkLabel(s, 0, STATUS_Y + 2, SCR_W, COL_ACCENT, LV_TEXT_ALIGN_CENTER);
  setLabel(t, "音乐", COL_ACCENT);
  musicName = mkLabel(s, 0, STATUS_Y + 18, SCR_W, COL_TEXT, LV_TEXT_ALIGN_CENTER);
  setLabel(musicName, buzzerName(SONGS[0]), COL_TEXT);
  musicState = mkLabel(s, 0, STATUS_Y + 34, SCR_W, COL_DIM, LV_TEXT_ALIGN_CENTER);
  setLabel(musicState, "已停止", COL_DIM);
  lv_obj_t* h = mkLabel(s, 0, STATUS_Y + 48, SCR_W, COL_DIM, LV_TEXT_ALIGN_CENTER);
  setLabel(h, "上下选 A播放 B停", COL_DIM);

  // A level meter flanking the title - the one moving element of the screen, so
  // "playing" is visible before the state word is even read. The bars grow
  // upwards from the title row's baseline (see animEqCb) and stay flat while
  // nothing plays (eqSetPlaying).
  for (int i = 0; i < 8; i++) {
    const bool right = (i >= 4);
    const int  k     = i & 3;
    const lv_coord_t x = right ? SCR_W - 11 - k * 6 : 8 + k * 6;
    eqBar[i] = mkAccentBar(s, x, EQ_BASE_Y - EQ_LO, EQ_LO,
                           right ? shade(COL_ACCENT, 6) : COL_ACCENT);
  }
}

// A data page: header band, one card, and a 12 row two column table inside it
// (a 128 px tall screen holds 2 x 6 rows). A row is
//
//     * 段          幼年        <- bullet, dim CJK label, bright value
//     ---------------------      <- hairline separator
//
// i.e. "段 幼年" is a table entry instead of a sentence, and the labels are
// deliberately one CJK cell each so they never crowd the value. Keeping the
// widgets alive (instead of building a page on entry) means switching pages
// never allocates.
static void buildInfoPage(AppScreen which, InfoPage& p, const char* title) {
  lv_obj_t* s = p.scr = scr[which] = mkScreen(COL_BG);
  mkHeader(s, title, "B:返回");
  p.full = false;
  p.hero = -1;

  const int n = 12;
  int cols = 1, rows = n;
  while (cols < 2 && rows > (SCR_H - INFO_Y - 2) / CN_GLYPH_H) {
    cols++;
    rows = (n + cols - 1) / cols;
  }
  int lineH = (SCR_H - INFO_Y - 2) / rows;
  if (lineH > CN_GLYPH_H + 8) lineH = CN_GLYPH_H + 8;
  const int colW = SCR_W / cols;

  mkCard(s, UI_PAD, INFO_Y - 2, SCR_W - 2 * UI_PAD, SCR_H - INFO_Y);

  p.n = n;
  for (int i = 0; i < n; i++) {
    const int c = i / rows, r = i % rows;
    const lv_coord_t lx = c * colW + UI_PAD;
    const lv_coord_t ly = INFO_Y + r * lineH;
    mkAccentBar(s, lx + 4, ly + 6, 3, shade(UI_ACC, 3));       // row bullet
    p.line[i] = mkLabel(s, lx + 11, ly, 18, COL_DIM, LV_TEXT_ALIGN_LEFT);
    p.val[i]  = mkLabelF(s, lx + 22, ly + 2, colW - 26, COL_TEXT,
                         LV_TEXT_ALIGN_RIGHT, UI_TXT_SM);
    if (r < rows - 1)                                         // table hairline
      mkHair(s, lx + 4, ly + lineH - 1, colW - 8, UI_SEP);
  }
}

// A message page ("重来" / "游戏结束"): a dialog card that owns the whole screen
// (no header band) with centred lines, one of them the "hero" - the score or
// the verdict - which gets 1.6 rows and (if it is a number) the 20 px font.
static void buildMessagePage(AppScreen which, InfoPage& p, int n, int hero) {
  lv_obj_t* s = p.scr = scr[which] = mkScreen(COL_BG);
  p.full = true;
  p.n    = n;
  p.hero = hero;

  // hero = 1.6 normal rows  =>  n-1 + 1.6 units  =>  unit = avail / (5n+3) * 5
  const int avail = SCR_H - 2 * UI_PAD;
  const int unit  = (avail * 5) / (n * 5 + 3);
  const int heroH = avail - unit * (n - 1);

  mkCard(s, UI_PAD, UI_PAD, SCR_W - 2 * UI_PAD, avail);

  int y = UI_PAD;
  for (int i = 0; i < n; i++) {
    const int h = (i == hero) ? heroH : unit;
    // a normal line is a 16 px cell, the hero is centred inside its taller row
    const lv_coord_t yo = (i == hero) ? y + (h - 20) / 2 : y + 1;
    p.line[i] = mkLabel(s, UI_PAD, yo, SCR_W - 2 * UI_PAD, COL_TEXT,
                        LV_TEXT_ALIGN_CENTER);
    y += h;
  }
}

// The About page is plain text, so it is written once at build time. Unlike the
// data pages it has no header band: the lines are spread over the *whole*
// 160x128 screen and centred, so the page reads as one full screen block.
static const char* const ABOUT_LINES[] = {
  "ESP32宠物",
  "版本 " FW_VERSION,
  "创作者 LGH",
  "主控 ESP32",
  "按键 6键",
  "光照36 温度39",
  "A确定 B返回"
};
#define ABOUT_N ((int)(sizeof(ABOUT_LINES) / sizeof(ABOUT_LINES[0])))

static void buildAbout() {
  lv_obj_t* s = scr[SCR_ABOUT] = mkScreen(COL_BG);
  const int avail = SCR_H - 2 * UI_PAD;
  const int lineH = avail / ABOUT_N;            // 120 / 7 = 17 px per line
  mkCard(s, UI_PAD, UI_PAD, SCR_W - 2 * UI_PAD, avail);
  for (int i = 0; i < ABOUT_N; i++) {
    // line 0 = title, line 2 = the author, last line = the key hint
    const uint16_t col = (i == 0 || i == 2) ? COL_ACCENT
                       : (i == ABOUT_N - 1) ? COL_DIM : COL_TEXT;
    lv_obj_t* l = mkLabel(s, UI_PAD, UI_PAD + i * lineH, SCR_W - 2 * UI_PAD, col,
                          LV_TEXT_ALIGN_CENTER);
    setLabel(l, ABOUT_LINES[i], col);
  }
  mkHair(s, SCR_W / 2 - 22, UI_PAD + lineH - 2, 44, COL_ACCENT);  // title underline
}

static void buildGame() {
  // The mini game paints the panel itself; LVGL only needs a blank screen to
  // sit behind it (and to give the keypad back to the menu afterwards).
  scr[SCR_GAME] = mkScreen(COL_BG);
}

// The network config screen: the ESP32 raises its own access point here and the
// phone points its browser at the address shown. Layout follows the info pages
// (a header plus a card of rows) so it reads as the same family of screens.
static void buildNet() {
  lv_obj_t* s = scr[SCR_NET] = mkScreen(COL_BG);
  mkHeader(s, "网络设置", "B:返回");
  mkCard(s, UI_PAD, INFO_Y - 2, SCR_W - 2 * UI_PAD, SCR_H - INFO_Y - UI_PAD);
  for (int i = 0; i < 5; i++)
    netLine[i] = mkLabel(s, UI_PAD + 4, INFO_Y + 2 + i * (CN_GLYPH_H + UI_GAP),
                         SCR_W - 2 * UI_PAD - 8, COL_DIM, LV_TEXT_ALIGN_LEFT);
}

// ---------------------------------------------------------------- menu
// Focus is drawn as one gesture, never half of it: the pill lights up, the row
// text (icon + word) goes bright and the marker bar appears. All of them carry
// the same 120 ms fade (uiStyleFade), so a row lights up instead of snapping.
static void setMenuRow(int i, bool focused) {
  lv_obj_t* b = menuBtn[i];
  if (!b) return;
  lv_obj_set_style_bg_opa(b, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(b, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  if (menuLbl[i])  lv_obj_set_style_text_color(menuLbl[i],  C(focused ? COL_TEXT : COL_DIM), 0);
  if (menuMark[i]) lv_obj_set_style_bg_opa(menuMark[i], focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void menuEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  const int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (code == LV_EVENT_CLICKED) {
    pendingAction = idx;      // run by frameMenu(), never from inside LVGL
  } else if (code == LV_EVENT_FOCUSED) {
    menuSel = idx;
    focusMoved = true;
    setMenuRow(idx, true);
  } else if (code == LV_EVENT_DEFOCUSED) {
    setMenuRow(idx, false);
  }
}

static void buildMenu() {
  lv_obj_t* s = scr[SCR_MENU] = mkScreen(COL_BG);
  mkHeader(s, "菜单", "B:返回");

  // Vertical list; the focused row is scrolled into view automatically because
  // lv_btn keeps LV_OBJ_FLAG_SCROLL_ON_FOCUS.
  lv_obj_t* list = lv_obj_create(s);
  lv_obj_set_pos(list, 0, MENU_Y);
  lv_obj_set_size(list, SCR_W, SCR_H - MENU_Y);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(list, C(COL_PANEL2), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);

  for (int i = 0; i < MENU_N; i++) {
    // lv_btn (not lv_obj) on purpose: its class clears LV_OBJ_FLAG_SCROLLABLE,
    // which is what makes the keypad send CLICKED to it instead of entering
    // "edit" mode. The theme's own button look is switched off just below.
    lv_obj_t* b = menuBtn[i] = lv_btn_create(list);
    lv_obj_set_pos(b, 2, i * MENU_ROW);
    lv_obj_set_size(b, SCR_W - 4, MENU_ROW - 1);
    lv_obj_set_style_radius(b, R_CARD, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, C(UI_EDGE), 0);
    lv_obj_set_style_border_opa(b, LV_OPA_TRANSP, 0);       // edge only when focused
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, C(UI_CARD1), 0);           // the focused pill
    lv_obj_set_style_bg_grad_color(b, C(UI_CARD2), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);

    // "icon + word" in ONE label: the glyph is Font Awesome (Montserrat), the two
    // CJK characters are the project font, and LVGL's fallback chain glues them
    // together - see uiFontSymbols() in uistyle.h. A second label per row was
    // tried first and cost ~7 KB of the LVGL pool, which the boot log showed as
    // 74% -> 85% full; the fallback font costs 60 bytes of RAM instead.
    char row[32];
    snprintf(row, sizeof row, "%s %s", MENU_ICON[i], MENU[i]);

    menuLbl[i] = mkLabel(b, 0, 0, SCR_W - 20, COL_DIM, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_font(menuLbl[i], uiFontSymbols(), 0);
    setLabel(menuLbl[i], row, COL_DIM);
    lv_obj_align(menuLbl[i], LV_ALIGN_LEFT_MID, 14, 0);
    // focus marker: invisible until setMenuRow() shows it
    menuMark[i] = mkAccentBar(b, 6, 3, MENU_ROW - 7, COL_ACCENT);
    lv_obj_set_style_bg_opa(menuMark[i], LV_OPA_TRANSP, 0);

    // Everything the focus flips fades instead of snapping: the pill, its text
    // and the marker all carry the same 120 ms transition.
    lv_obj_add_style(b, uiStyleFade(), 0);
    lv_obj_add_style(menuLbl[i], uiStyleFade(), 0);
    lv_obj_add_style(menuMark[i], uiStyleFade(), 0);

    lv_obj_add_event_cb(b, menuEvent, LV_EVENT_CLICKED,   (void*)(intptr_t)i);
    lv_obj_add_event_cb(b, menuEvent, LV_EVENT_FOCUSED,   (void*)(intptr_t)i);
    lv_obj_add_event_cb(b, menuEvent, LV_EVENT_DEFOCUSED, (void*)(intptr_t)i);
  }
}

// =====================================================================
//  actions / screen transitions
// =====================================================================
// Short situational chit-chat shown as a little bubble over the face. Kept
// brief (<= ~7 CJK glyphs) so it always fits inside the bubble.
static void petSpeak() {
  PetState& s = pet.s();
  const char* msg;
  static char idle[80];
  if (s.sick)              msg = (random(2) ? "我不舒服" : "咳咳...");
  else if (s.hunger < 25)  msg = (random(2) ? "我饿了"   : "想吃东西");
  else if (s.energy < 25)  msg = (random(2) ? "好困啊"   : "想睡觉");
  else if (s.clean  < 30)  msg = (random(2) ? "我想洗澡" : "身上脏了");
  else if (s.happy  < 30)  msg = (random(2) ? "陪我玩嘛" : "有点无聊");
  else if (s.happy >= 85)  msg = (random(3) == 0 ? "最喜欢你" : "好开心");
  else {
    settingsPickPhrase(idle, sizeof idle);       // the user enabled catchphrases
    msg = idle;
  }
  eyes.say(msg, 2600);
}

// Message page "hero" line: a pure ASCII number gets the 20 px font (there is
// exactly one "big" moment per message page), a CJK sentence keeps the 16 px
// CJK cell because that is the only size the CJK font has.
static void setHero(lv_obj_t* l, const char* txt) {
  if (!l || !txt) return;
  bool ascii = true;
  for (const unsigned char* q = (const unsigned char*)txt; *q; q++)
    if (*q >= 0x80) { ascii = false; break; }
  lv_obj_set_style_text_font(l, ascii ? UI_TXT_HERO : UI_TXT_CJK, 0);
  setLabel(l, txt, COL_ACCENT);
}

// Fill an info page (its title was fixed at build time) and switch to it. The
// rows are written *before* the screen is loaded, so the first paint is already
// complete and nothing flashes. `values` is null for message pages: those take
// their colour hierarchy from the position of a line (hero / hint), data pages
// from being a label or a value.
static void showInfoPage(AppScreen which, InfoPage& p, const char* const* lines,
                         const char* const* values, int n) {
  for (int i = 0; i < p.n; i++) {
    const bool use = (i < n) && lines[i] && lines[i][0];
    const char* txt = use ? lines[i] : "";
    if (p.val[i]) {                                       // data page row
      setValue(p.line[i], txt, COL_DIM);
      setValue(p.val[i], (values && use && values[i]) ? values[i] : "", COL_TEXT);
    } else if (i == p.hero) {                             // the big moment
      setHero(p.line[i], txt);
    } else {                        // title / ordinary line / key hint
      setValue(p.line[i], txt, (i == 0)          ? COL_ACCENT
                             : (i == p.n - 1)    ? COL_DIM : COL_TEXT);
    }
  }
  setScreen(which);
}

static void showStatus() {
  PetState& s = pet.s();
  // The data page is a table: one CJK cell per row as the (dim) label and the
  // value right aligned next to it, so "段 幼年" reads as an entry instead of a
  // sentence and all numbers line up in a column.
  const char* lab[12];
  char v[12][20];
  lab[0]  = "段";  snprintf(v[0],  20, "%s",  stageName(s.stage));
  lab[1]  = "龄";  petAgeText(v[1],  20, s.ageMin);   // 42分 / 23时 / 6天
  lab[2]  = "重";  snprintf(v[2],  20, "%ug", s.weight);
  lab[3]  = "饿";  snprintf(v[3],  20, "%u%%", s.hunger);
  lab[4]  = "乐";  snprintf(v[4],  20, "%u%%", s.happy);
  lab[5]  = "力";  snprintf(v[5],  20, "%u%%", s.energy);
  lab[6]  = "康";  snprintf(v[6],  20, "%u%%", s.health);
  lab[7]  = "净";  snprintf(v[7],  20, "%u%%", s.clean);
  lab[8]  = "便";  snprintf(v[8],  20, "%u",   s.poopsTotal);
  lab[9]  = "误";  snprintf(v[9],  20, "%u",   s.mistakes);
  char lt[24], lh[24];
  tempText(lt, sizeof lt);
  lightText(lh, sizeof lh);
  lab[10] = "温";  snprintf(v[10], 20, "%s", lt);
  lab[11] = "光";  snprintf(v[11], 20, "%s", lh);

  const char* val[12];
  for (int i = 0; i < 12; i++) val[i] = v[i];
  showInfoPage(SCR_STAT, pgStat, lab, val, 12);
}

static void showAbout() {
  // static page - the text was written by buildAbout()
  setScreen(SCR_ABOUT);
}

// Bring the soft-AP up and show how to reach it. If the radio cannot start the
// rows explain that instead of leaking a half-up AP.
static void showNet() {
  char a[44];
  if (!netConfigBegin()) {
    setLabel(netLine[0], "网络启动失败", COL_BAD);
    setLabel(netLine[1], "请再试一次",   COL_DIM);
    for (int i = 2; i < 5; i++) setLabel(netLine[i], "", COL_DIM);
  } else {
    setLabel(netLine[0], "WiFi热点",       COL_ACCENT);
    setLabel(netLine[1], netConfigSsid(),  COL_TEXT);      // e.g. ESP32-Pet-1A2B
    snprintf(a, sizeof a, "密码 %s", netConfigPass());
    setLabel(netLine[2], a, COL_TEXT);
    snprintf(a, sizeof a, "网址 %s", netConfigIp());
    setLabel(netLine[3], a, COL_TEXT);
    setLabel(netLine[4], "浏览器打开网址", COL_DIM);
  }
  setScreen(SCR_NET);
}

// Shown when the player picks the restart entry while the pet is still alive:
// the verdict (hero, line 0) fills the page, the consequence is spelled out
// underneath and the last line is the way back out.
static void showCantRestart() {
  static const char* lines[] = {
    "不能重来",
    "宠物还活着",
    "",
    "等它去世后",
    "才能重新开始",
    "A/B返回"
  };
  showInfoPage(SCR_CANT, pgCant, lines, nullptr, 6);
}

// The end-of-round page: the score is the hero line (20 px if it is a number)
// and the two rewards explain what the round was worth.
static void showGameResult() {
  const int sc = game.score();
  char b0[24], b1[24], b2[24];
  snprintf(b0, 24, "%d", sc);
  snprintf(b1, 24, "快乐 +%d", min(30, sc * 2));
  snprintf(b2, 24, "饥饿 +%d", min(20, sc));
  const char* lines[] = { "游戏结束", "分数", b0, b1, b2, "A/B返回" };
  showInfoPage(SCR_RESULT, pgResult, lines, nullptr, 6);
}

static void showEmote() {
  emoteIdx = 0;
  eyes.setBaseMood(EMOTE_ORDER[emoteIdx]);
  setLabel(emoteLabel, moodName(EMOTE_ORDER[emoteIdx]), COL_ACCENT);
  emoteDots();
  setScreen(SCR_EMOTE);
}

static void showMusic() {
  songSel = 0;
  setLabel(musicName, buzzerName(SONGS[songSel]), COL_TEXT);
  setLabel(musicState, buzzerBusy() ? "播放中" : "已停止",
           buzzerBusy() ? COL_GOOD : COL_DIM);
  eqSetPlaying(buzzerBusy());
  setScreen(SCR_MUSIC);
}

static void startGame() {
  game.begin(&tft);
  gamePhase    = GPA_INTRO;
  gameRewarded = false;
  setScreen(SCR_GAME);
  lv_refr_now(NULL);        // paint the (blank) game screen first ...
  gameRunIntro(&tft);       // ... then put the GET READY splash on top of it
}

static void updateStatBars_() {
  PetState& s = pet.s();
  setLabel(faceStage, stageName(s.stage), COL_TEXT);
  const char* st =
      s.dead          ? "死亡" :
      s.asleep        ? "睡觉" :
      s.sick          ? "生病" :
      (s.hunger < 20) ? "饥饿" :
      (s.clean  < 25) ? "脏了" : "很棒";
  setLabel(faceStatus, st, s.dead ? COL_BAD : s.sick ? COL_SICK : COL_DIM);

  const int v[5] = { s.hunger, s.happy, s.energy, s.health, s.clean };
  for (int i = 0; i < 5; i++) setBar(statBar[i], v[i], UI::statColor(v[i]));
}

// The corner read-outs (light top left, temperature top right) are painted into
// the *scene sprite* by EyeEngine, so they show up on every screen that displays
// the face and need no LVGL widget of their own. They are refreshed once a
// second: EyeEngine copies the strings and repaints them with each frame.
static void updateSensorHud() {
  char l[20], r[20];
  lightHudText(l, sizeof l);
  tempHudText (r, sizeof r);
  eyes.setHud(l, r);
}

// The single entry point for changing screens: it clears the key queue, drops
// any pending menu action and forgets the menu focus so a stale key can never
// leak into the screen we are entering. The switch fades in (SCR_FADE_MS) -
// except for the two screens that are painted by someone else right after the
// switch (the game draws onto the panel, the welcome screen fades from the
// untouched default screen at boot), which must not be delayed.
static void setScreen(AppScreen s) {
  lv_port_clear_keys();
  pendingAction = -1;
  focusMoved    = false;
  screen        = s;
  if (s != SCR_MENU) lv_group_remove_all_objs(lv_port_group());

  if (SCR_FADE_MS == 0 || s == SCR_GAME || s == SCR_WELCOME) {
    lv_disp_load_scr(scr[s]);
  } else {
    lv_scr_load_anim(scr[s], LV_SCR_LOAD_ANIM_FADE_IN, SCR_FADE_MS, 0, false);
  }
  statusMs = animMs = millis();   // panel/canvas are refreshed next frame
}

static void enterFace() {
  lastExpr = MOOD_COUNT;          // force an expression refresh
  setScreen(SCR_FACE);
  updateStatBars_();
  flairMs = millis() + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
}

static void openMenu() {
  const int want = menuSel;        // save before LVGL's FOCUSED event can touch it
  setScreen(SCR_MENU);             // also clears the key queue
  lv_group_t* g = lv_port_group();
  lv_group_remove_all_objs(g);
  // Note: lv_group_add_obj() auto focuses the very first object it receives
  // (its FOCUSED event resets menuSel), so the remembered row is restored
  // afterwards instead of before.
  for (int i = 0; i < MENU_N; i++) lv_group_add_obj(g, menuBtn[i]);
  menuSel = want;
  lv_group_focus_obj(menuBtn[want]);
  lv_obj_scroll_to_view(menuBtn[want], LV_ANIM_OFF);
  focusMoved = false;              // the focus above is not a "moved" event
}

static void menuAction(int sel) {
  switch (sel) {
    // The care actions are three beats long (see careaction.h): the pet plays the
    // animation first and the stat is applied when it ends, so the face screen is
    // entered right away - that is where the animation is visible.
    case 0: care.begin(CARE_FEED);     enterFace(); break;
    case 1: startGame();                                                                                         break;
    case 2: if (!pet.s().dead) { pet.toggleSleep(); sfxSleep();
              if (pet.s().asleep) setScreen(SCR_SLEEP); else enterFace(); }                                      break;
    case 3: care.begin(CARE_WASH);     enterFace(); break;
    case 4: care.begin(CARE_MEDICINE); enterFace(); break;
    case 5: showStatus();                                                                                        break;
    case 6: showMusic();                                                                                         break;
    case 7: showEmote();                                                                                         break;
    case 8: showNet();                                                                                           break;
    case 9: showAbout();                                                                                         break;
    case 10: // restart is only allowed once the pet has died
      if (pet.s().dead) { pet.reset(); lastExpr = MOOD_COUNT; sfxStartup(); enterFace(); }
      else              { sfxCancel(); showCantRestart(); }
      break;
  }
  pet.save();   // persist the effect of every menu action right away
}

static void welcome() {
  welcomeStage = 0;
  welcomeMs    = millis();
  setScreen(SCR_WELCOME);
  eyes.setBaseMood(MOOD_SLEEPY);
  eyes.draw();
  faceInvalidate(faceView[SCR_WELCOME]);
}

// =====================================================================
//  per frame screen handlers  (~30 fps)
// =====================================================================
static void frameWelcome(uint32_t dt) {
  eyes.update(dt);
  eyes.draw();
  faceInvalidate(faceView[SCR_WELCOME]);

  const uint32_t e = millis() - welcomeMs;
  if (welcomeStage == 0 && e > 500)  { welcomeStage = 1; eyes.setBaseMood(MOOD_NEUTRAL); eyes.blinkNow(); sfxConfirm(); }
  if (welcomeStage == 1 && e > 1400) { welcomeStage = 2; eyes.lookRandom(); }
  if (welcomeStage == 2 && e > 2100) { welcomeStage = 3; eyes.setBaseMood(MOOD_HAPPY); eyes.flashMood(MOOD_LOVE, 900); sfxHappy(); }

  if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I) || e > 6000) { sfxClick(); enterFace(); }
}

static void frameFace(uint32_t dt) {
  PetState& s = pet.s();
  if (s.asleep && !s.dead) { setScreen(SCR_SLEEP); return; }

  const EyeMood m = pet.expressionMood();
  if (m != lastExpr) { eyes.setBaseMood(m); lastExpr = m; }

  eyes.update(dt);
  eyes.draw();
  faceInvalidate(faceView[SCR_FACE]);

  const uint32_t now = millis();
  if (now - statusMs > 700) { statusMs = now; updateStatBars_(); }

  // Idle small talk, spaced by the user setting (talkMaxMs == 0 switches it off).
  // Muted while a care action is playing: its bubble would cover the animation
  // (the bubble is drawn after the action overlay) and would talk over beat 3.
  if (!s.dead && cfg.talkMaxMs && !care.busy() && (int32_t)(now - talkMs) >= 0) {
    talkMs = now + random((long)cfg.talkMinMs, (long)cfg.talkMaxMs);
    petSpeak();
  }

  // Ambient expression flair: even while the stats (and therefore the base mood)
  // barely move, flash a lively expression now and then so the pet feels alive.
  if (!s.dead && !s.asleep && !care.busy() && (int32_t)(now - flairMs) >= 0) {
    flairMs = now + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
    const EyeMood base = pet.expressionMood();
    if (base == MOOD_NEUTRAL || base == MOOD_HAPPY ||
        base == MOOD_CURIOUS || base == MOOD_LOVE) {
      static const EyeMood FLAIR[] = { MOOD_HAPPY, MOOD_LOVE, MOOD_SURPRISED,
                                       MOOD_WINK, MOOD_CURIOUS };
      eyes.flashMood(FLAIR[random(sizeof(FLAIR) / sizeof(FLAIR[0]))],
                     700 + random(0, 900));
      if (random(3) == 0) eyes.lookRandom();
    }
  }

  if (btnPressed(BTN_A_I))     { sfxConfirm(); openMenu(); return; }
  if (btnPressed(BTN_B_I))     { eyes.flashMood(MOOD_LOVE, 800); eyes.say("嘿嘿", 1500); pet.petIt(); sfxHappy(); return; }
  if (btnPressed(BTN_UP_I))    { eyes.look(0, -100);    sfxMove(); }
  if (btnPressed(BTN_DOWN_I))  { eyes.look(0,  100);    sfxMove(); }
  if (btnPressed(BTN_LEFT_I))  { eyes.look(-100, 0);    sfxMove(); }
  if (btnPressed(BTN_RIGHT_I)) { eyes.look( 100, 0);    sfxMove(); }
}

static void frameMenu() {
  // Feed this frame's key edges into LVGL; lv_timer_handler() (top of the next
  // frame) turns them into focus moves / CLICKED events.
  lv_port_poll_keys();

  if (focusMoved) { focusMoved = false; sfxMove(); }

  if (btnPressed(BTN_B_I)) { sfxCancel(); enterFace(); return; }

  if (pendingAction >= 0) {
    const int a = pendingAction;
    pendingAction = -1;
    sfxConfirm();
    menuAction(a);              // may change the screen
  }
}

static void frameInfo() {
  if (btnPressed(BTN_B_I) || btnPressed(BTN_A_I)) { sfxCancel(); openMenu(); }
}

static void frameNet(uint32_t) {
  netConfigLoop();                     // serve the config page (blocking HTTP)
  if (btnPressed(BTN_B_I) || btnPressed(BTN_A_I)) {
    netConfigEnd();                    // drop the AP again -> radio off
    sfxCancel();
    openMenu();
  }
}

static void frameSleep(uint32_t dt) {
  PetState& s = pet.s();
  if (!s.asleep || s.dead) { enterFace(); return; }

  eyes.setBaseMood(MOOD_SLEEPY);
  eyes.update(dt);
  eyes.draw();
  faceInvalidate(faceView[SCR_SLEEP]);

  const uint32_t now = millis();
  if (now - animMs > 500) {
    animMs = now;
    char b[24];
    snprintf(b, sizeof b, "体力 %u%%", s.energy);
    setLabel(sleepEnergy, b, COL_DIM);
  }

  if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I)) { pet.wake(); sfxConfirm(); enterFace(); }
}

static void frameEmote(uint32_t dt) {
  eyes.update(dt);
  eyes.draw();
  faceInvalidate(faceView[SCR_EMOTE]);

  const int prev = emoteIdx;
  // The chip at the bottom promises "左右切换", so the horizontal keys are the
  // ones that walk the gallery (this is what was broken: only UP/DOWN were read).
  // UP/DOWN still work as aliases, and A keeps stepping forward too.
  if (btnPressed(BTN_RIGHT_I) || btnPressed(BTN_DOWN_I) || btnPressed(BTN_A_I))
    emoteIdx = (emoteIdx + 1) % EMOTE_N;
  if (btnPressed(BTN_LEFT_I)  || btnPressed(BTN_UP_I))
    emoteIdx = (emoteIdx + EMOTE_N - 1) % EMOTE_N;
  if (emoteIdx != prev) {
    eyes.setBaseMood(EMOTE_ORDER[emoteIdx]);
    setLabel(emoteLabel, moodName(EMOTE_ORDER[emoteIdx]), COL_ACCENT);
    emoteDots();
    sfxMove();
  }

  if (btnPressed(BTN_B_I)) { sfxCancel(); openMenu(); }
}

static void frameMusic(uint32_t dt) {
  eyes.update(dt);
  eyes.draw();
  faceInvalidate(faceView[SCR_MUSIC]);

  const int prev = songSel;
  if (btnPressed(BTN_DOWN_I)) songSel = (songSel + 1) % SONG_N;
  if (btnPressed(BTN_UP_I))   songSel = (songSel + SONG_N - 1) % SONG_N;
  if (songSel != prev) sfxMove();
  if (btnPressed(BTN_A_I))    buzzerPlay(SONGS[songSel], true);

  const uint32_t now = millis();
  if (songSel != prev || now - animMs > 300) {   // keep "playing" up to date
    animMs = now;
    setLabel(musicName, buzzerName(SONGS[songSel]), COL_TEXT);
    setLabel(musicState, buzzerBusy() ? "播放中" : "已停止",
             buzzerBusy() ? COL_GOOD : COL_DIM);
    eqSetPlaying(buzzerBusy());                  // level meter follows the buzzer
  }

  if (btnPressed(BTN_B_I)) { buzzerStop(); eqSetPlaying(false); sfxCancel(); openMenu(); }
}

static void frameGame(uint32_t dt) {
  // The game paints straight onto the panel, so nothing here may touch LVGL's
  // widgets (see startGame(): the blank game screen is flushed before the
  // splash and after that LVGL has nothing left to refresh).
  if (gamePhase == GPA_INTRO) {
    if (btnPressed(BTN_A_I))      { game.start(); gamePhase = GPA_PLAY; sfxConfirm(); }
    else if (btnPressed(BTN_B_I)) { sfxCancel(); openMenu(); }
    return;
  }

  if (gamePhase == GPA_PLAY) {
    game.update(dt);
    game.render();
    if (game.exitRequested()) { sfxCancel(); openMenu(); return; }
    if (game.over()) {
      gamePhase = GPA_RESULT;
      if (!gameRewarded) {
        gameRewarded = true;
        const int sc = game.score();
        pet.addHappy(min(30, sc * 2));
        pet.addHunger(min(20, sc));
        eyes.flashMood(MOOD_LOVE, 1500);
      }
      showGameResult();
    }
    return;
  }

  if (btnPressed(BTN_A_I) || btnPressed(BTN_B_I)) { sfxClick(); openMenu(); }
}

// =====================================================================
//  setup / loop
// =====================================================================
static void buildScreens() {
  buildWelcome();
  buildFace();
  buildMenu();
  buildSleep();
  buildEmote();
  buildMusic();
  buildInfoPage(SCR_STAT,    pgStat,   "状态");
  buildAbout();                                    // static, full screen, no header
  buildMessagePage(SCR_CANT,   pgCant,   6, 0);    // "A/B返回" pages: no header,
  buildMessagePage(SCR_RESULT, pgResult, 6, 2);    // line 2 of the result = score
  buildGame();
  buildNet();
}

void setup() {
  Serial.begin(115200);
  delay(60);
  randomSeed(analogRead(LDR_PIN) ^ micros());

  settingsBegin();        // user tunables (talk / expression / growth) from NVS
  buzzerInit();
  buttonsInit();
  sensorsInit();
  updateSensorHud();      // prime the scene corners before the first frame

  tft.init();
  tft.setRotation(SCR_ROTATION);
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, COL_BG);

  if (!eyes.begin(&tft)) {
    tft.fillScreen(COL_BAD);
    tft.drawString("No RAM for face!", SCR_W / 2, SCR_H / 2, 1);
    while (true) delay(1000);
  }

  lv_port_init(&tft);            // LVGL heap + display/indev drivers

  pet.begin();
  // The status table shows the age in one unit only (see petAgeText), so the
  // exact minute count goes here - that is what makes a legacy, 30x inflated
  // save recognisable at a glance.
  Serial.printf("[pet] state: %s, age %u min\n",
                pet.load() ? "inherited from NVS" : "fresh (no save found)",
                (unsigned)pet.s().ageMin);
  {
    // The growth speed is a stored user setting, so print the pace it implies:
    // "age too fast" is then a one line answer instead of a code dig.
    char gp[96];
    petGrowthPlanText(gp, sizeof gp);
    Serial.printf("[pet] age: %s\n", gp);
  }

#if USE_SD
  SPI.begin(18, 19, 23, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 8000000)) Serial.println("[sd] card not found");
#endif

  buildScreens();
  updateStatBars_();

#if UI_SWEEP
  // Verification hook (see UI_SWEEP in config.h): paint every page once while
  // the boot log is still being watched, so a page that cannot be drawn is
  // found here rather than by a user walking into it.
  for (int i = 0; i < SCR_COUNT; i++) {
    lv_disp_load_scr(scr[i]);
    for (int f = 0; f < 6; f++) {
      lv_timer_handler();
      delay(15);
    }
    Serial.printf("[ui] sweep %d/%d ok\n", i + 1, (int)SCR_COUNT);
  }

  // Every menu row carries an LV_SYMBOL_* glyph. A symbol that is not inside
  // montserrat_14 would draw as an empty box (or as nothing at all) and nobody
  // would notice in a log - so ask the font itself, once, while the log is open.
  {
    int ok = 0, wmin = 999, wmax = 0, cpmin = 0xFFFF, cpmax = 0;
    for (int i = 0; i < MENU_N; i++) {
      const unsigned char* s = (const unsigned char*)MENU_ICON[i];
      uint32_t cp = s[0];                                   // decode the first
      if      (cp >= 0xF0) cp = ((cp & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
      else if (cp >= 0xE0) cp = ((cp & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
      else if (cp >= 0xC0) cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F);
      lv_font_glyph_dsc_t d;
      lv_memset_00(&d, sizeof(d));
      if (lv_font_get_glyph_dsc(UI_TXT_SM, &d, cp, 0)) {
        ok++;
        if ((int)d.box_w < wmin) wmin = (int)d.box_w;
        if ((int)d.box_w > wmax) wmax = (int)d.box_w;
        if (cp < cpmin) cpmin = cp;
        if (cp > cpmax) cpmax = cp;
      }
    }
    Serial.printf("[ui] menu icons %d/%d ok (U+%04X..U+%04X, box_w %d..%d px)\n",
                  ok, (int)MENU_N, (unsigned)cpmin, (unsigned)cpmax, wmin, wmax);
  }

  // The 龄 row of the status page is formatted, not printed raw: it switches to
  // 分 / 时 / 天 instead of quietly growing to "4600m" - the exact bug this check
  // was added for. The vectors sit on both unit boundaries (59/60, 1439/1440) and
  // end on the largest age a uint16_t ageMin can hold (65535 min = 45天).
  {
    static const uint16_t IN[]   = { 0,     59,     60,    1439,   1440,   9051,   65535 };
    static const char*    WANT[] = { "0分", "59分", "1时", "23时", "1天", "6天", "45天" };
    int ok = 0;
    for (int i = 0; i < 7; i++) {
      char t[16];
      petAgeText(t, sizeof t, IN[i]);
      if (strcmp(t, WANT[i]) == 0) ok++;
      else Serial.printf("[ui] age text MISMATCH %u -> %s (want %s)\n", IN[i], t, WANT[i]);
    }
    Serial.printf("[ui] age text %d/7 ok\n", ok);
  }

  // ... and then play one care action of each kind, so the whole "animate ->
  // settle -> perform" path (careaction.h) plus its drawing (eyes.cpp) runs once
  // with the log open. The pet is put back exactly as it was: a self test must
  // never feed or punish it for real.
  setScreen(SCR_FACE);
  const PetState restore = pet.s();
  static const CareKind DEMO[] = { CARE_FEED, CARE_WASH, CARE_MEDICINE };
  for (int i = 0; i < 3; i++) {
    care.begin(DEMO[i]);
    const uint32_t t0 = millis();
    while (millis() - t0 < CARE_ANIM_MS + 500) {
      lv_timer_handler();
      // The two lines frameFace() runs every frame, so the demo covers the "beat
      // 3's flash must survive the UI's lastExpr bookkeeping" case as well.
      const EyeMood bm = pet.expressionMood();
      if (bm != lastExpr) { eyes.setBaseMood(bm); lastExpr = bm; }
      eyes.update(16);                 // same ~16 ms step as the real loop
      eyes.draw();
      faceInvalidate(faceView[SCR_FACE]);
      care.update();
      if (care.consumeSettled()) { updateStatBars_(); lastExpr = pet.expressionMood(); }
      delay(16);
    }
    // 500 ms after the settle the flash of beat 3 must still be the visible mood;
    // if "mood" already equals "base" here, something wiped it.
    Serial.printf("[ui] care demo %d/3 ok (mood %s, base %s)\n", i + 1,
                  moodName(eyes.mood()), moodName(pet.expressionMood()));
  }
  pet.s() = restore;
  pet.save();
  lastExpr = MOOD_COUNT;
#endif

  statusMs = sensorMs = animMs = saveMs = hudMs = millis();
  sfxStartup();
  welcome();
  lastFrame = millis();
  talkMs  = millis() + random((long)cfg.talkMinMs, (long)cfg.talkMaxMs);
  flairMs = millis() + random((long)cfg.exprMinMs, (long)cfg.exprMaxMs);
  Serial.printf("[ui] LVGL %d.%d.%d ready, %d screens, pet scene %dx%d\n",
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                (int)SCR_COUNT, (int)FACE_W, (int)PET_H);
  Serial.printf("[mem] free heap %u B after the scene sprite\n",
                (unsigned)ESP.getFreeHeap());
  // The LVGL pool is the one number that decides whether the interface can grow
  // (LV_MEM_SIZE in lv_conf.h). Printing it here means a new widget that does
  // not fit shows up as a healthy-looking UI that misbehaves later - the log
  // shows it immediately instead.
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("[ui] LVGL pool %u B: %u%% used, %u B free (biggest block %u B, frag %u%%)\n",
                (unsigned)mon.total_size, (unsigned)mon.used_pct,
                (unsigned)mon.free_size, (unsigned)mon.free_biggest_size,
                (unsigned)mon.frag_pct);
  const int td = tempDeciC();
  Serial.printf("[sensor] light GPIO%d = %d%% (raw %d)   temp GPIO%d = %d.%dC (raw %d)\n",
                LDR_PIN, lightPct(), lightRaw(),
                NTC_PIN, td / 10, abs(td % 10), tempRaw());
}

void loop() {
  const uint32_t now = millis();
  buttonsUpdate();
  buzzerUpdate();

  if (now - sensorMs >= 100) {
    sensorMs = now;
    sensorsUpdate();
    if (now - hudMs >= 1000) {
      hudMs = now;
      updateSensorHud();                       // scene corners (light / temp), ~1 Hz
#if BTN_DEBUG
      // Report *meaningful* sensor changes as well, so the ADC wiring on
      // GPIO36 (light) / GPIO39 (temperature) can be checked in the monitor.
      static int dbgLight = -1, dbgTemp = -999;
      const int lp = lightPct(), td = tempDeciC();
      if (abs(lp - dbgLight) >= 2 || abs(td - dbgTemp) >= 20) {
        dbgLight = lp; dbgTemp = td;
        Serial.printf("[sensor] LDR GPIO%d = %d%% (raw %d)   NTC GPIO%d = %d.%dC (raw %d)\n",
                      LDR_PIN, lp, lightRaw(),
                      NTC_PIN, td / 10, abs(td % 10), tempRaw());
      }
#endif
    }
  }

#if BTN_DEBUG
  // report every key-state CHANGE so the wiring can be verified in the monitor
  static uint8_t dbgKeys = 0;
  uint8_t keys = 0;
  for (uint8_t i = 0; i < 6; i++) if (btnDown(i)) keys |= (1 << i);
  if (keys != dbgKeys) {
    dbgKeys = keys;
    Serial.printf("[keys] mask=0x%02X  %s%s%s%s%s%s\n", keys,
                  btnDown(BTN_UP_I)    ? "UP " : "",
                  btnDown(BTN_DOWN_I)  ? "DN " : "",
                  btnDown(BTN_LEFT_I)  ? "LT " : "",
                  btnDown(BTN_RIGHT_I) ? "RT " : "",
                  btnDown(BTN_A_I)     ? "A "  : "",
                  btnDown(BTN_B_I)     ? "B "  : "");
  }
#endif

  // ~30 fps render/handle cadence (keeps button edges low-latency)
  uint32_t dt = now - lastFrame;
  if (dt < 30) return;
  lastFrame = now;
  if (dt > 200) dt = 200;

  pet.update(dt);
  if (pet.consumePoop())   sfxPoop();
  if (pet.consumeEvolve()) { eyes.flashMood(MOOD_LOVE, 1500); sfxEvolve(); pet.save(); }

  // "Cover me": a hand over the light + temperature sensors makes the light fall
  // and the NTC warm up (body heat), so the pet basks in it. The one-shot gesture
  // is consumed every frame (so it can never queue up) but only acted on when the
  // pet is awake on the face screen.
  const bool covered = sensorsCoverEvent();
  if (covered && screen == SCR_FACE && !pet.s().dead && !pet.s().asleep) {
    eyes.flashMood(MOOD_LOVE, 1500);
    eyes.say("好舒服啊", 2200);
    pet.petIt();
    sfxHappy();
#if BTN_DEBUG
    Serial.println("[sensor] hand-cover gesture -> the pet says 好舒服啊");
#endif
  }

  // Care action timeline: the animation runs on its own and the stat lands when it
  // is over (see careaction.h). It is driven here rather than in the menu handler
  // because the screen may well have changed in between - the action still has to
  // finish, and the bars have to move in the very frame the value did.
  care.update();
  if (care.consumeSettled()) {
    updateStatBars_();                    // bars / status word: only now do they change
    lastExpr = pet.expressionMood();      // the resting face already moved with the
  }                                       // stat, so do NOT set MOOD_COUNT here: that
                                          // would re-apply the base mood next frame and
                                          // wipe beat 3's flash before it is seen

#if PET_AUTOSAVE_MS
  if (now - saveMs >= PET_AUTOSAVE_MS) { saveMs = now; pet.save(); }   // periodic checkpoint
#endif

  // Animate widgets, run the keypad state machines and flush whatever the
  // previous frame invalidated (the face canvas included).
  lv_timer_handler();

  switch (screen) {
    case SCR_WELCOME: frameWelcome(dt); break;
    case SCR_FACE:    frameFace(dt);    break;
    case SCR_MENU:    frameMenu();      break;
    case SCR_STAT:
    case SCR_ABOUT:
    case SCR_CANT:
    case SCR_RESULT:  frameInfo();      break;
    case SCR_SLEEP:   frameSleep(dt);   break;
    case SCR_EMOTE:   frameEmote(dt);   break;
    case SCR_MUSIC:   frameMusic(dt);   break;
    case SCR_GAME:    frameGame(dt);    break;
    case SCR_NET:     frameNet(dt);     break;
    default: break;
  }

  buttonsEndFrame();
}

#endif  // USE_LVGL
