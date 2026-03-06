// Copyright(c) 2023 Takao Akaki
// Arange 2026 Tomoshi Wagata
// (Customized for CoreS3 + M5GO Bottom3: LED, Audio, Mute & Battery Warning)

#include <M5Unified.h>
#include <Avatar.h>
#include "fft.hpp"
#include <FastLED.h>
#include <cinttypes>
#include <cmath>

// --- LTR-553ALS-WA (環境光センサー) とダークモード設定 ---
static uint32_t last_als_check_msec = 0;
static bool is_dark_mode = false;
static const int DARK_THRESHOLD = 3; // 暗さのしきい値 (環境に合わせて数値を調整してください)
static constexpr uint8_t LTR553_ADDR = 0x23; // センサーのI2Cアドレス

bool initALS() {
  // LTR-553ALS-WAのALS_CONTRレジスタ(0x80)に0x01を書き込み、アクティブモード(計測開始)にする
  return M5.In_I2C.writeRegister8(LTR553_ADDR, 0x80, 0x01, 400000);
}

int getAmbientLight() {
  // センサーのCH0 (可視光＋赤外線) レジスタから明るさの数値を読み取る
  // 0x8Aが下位バイト、0x8Bが上位バイト
  uint8_t dataL = M5.In_I2C.readRegister8(LTR553_ADDR, 0x8A, 400000);
  uint8_t dataH = M5.In_I2C.readRegister8(LTR553_ADDR, 0x8B, 400000);
  return (dataH << 8) | dataL; // 16ビットのデータに合成して返す
}
// ----------------------------------------------------


// --- M5GO Bottom3 LED 設定ここから ---
#define LED_PIN     5
#define NUM_LEDS    10
#define S_VOL    70
CRGB leds[NUM_LEDS];
// --- ここまで追加 ---

#ifdef ARDUINO
#include <esp_system.h>
#endif

#if defined(ARDUINO_M5STACK_CORES3)
  #include <gob_unifiedButton.hpp>
  goblib::UnifiedButton unifiedButton;
#endif

#define USE_MIC

#ifdef USE_MIC
  #define READ_LEN    (2 * 256)
  #define LIPSYNC_LEVEL_MAX 10.0f

  int16_t *adcBuffer = NULL;
  static fft_t fft;
  static constexpr size_t WAVE_SIZE = 256 * 2;
  static constexpr const size_t record_samplerate = 16000;
  static int16_t *rec_data;
  
  uint8_t lipsync_shift_level = 11;
  float lipsync_max = LIPSYNC_LEVEL_MAX;
#endif

using namespace m5avatar;

Avatar avatar;
ColorPalette *cps[6];
uint8_t palette_index = 0;

static constexpr uint32_t ANGRY_BG_COLOR = TFT_RED;
static uint32_t cps_bg_default[6] = { TFT_NAVY, TFT_ORANGE, (uint16_t)0x303303, TFT_BLACK, TFT_WHITE, (uint16_t)0x00ff00 };
static bool angry_bg_on = false;

static constexpr uint32_t SLEEP_BG_COLOR = TFT_BLACK;
static bool sleep_bg_on = false;

uint32_t last_rotation_msec = 0;
uint32_t last_lipsync_max_msec = 0;

#if defined(ARDUINO_M5STACK_CORES3)
static constexpr uint32_t IDLE_TO_SLEEP_MS = 25 * 1000;
static constexpr float    SOUND_ACTIVE_RATIO_ON = 0.12f;
static constexpr uint32_t SOUND_ACTIVE_HOLD_MS = 500;
static constexpr float    MOTION_DELTA_G_ON = 0.18f;
static constexpr uint32_t MOTION_ACTIVE_HOLD_MS = 700;
#else
static constexpr uint32_t IDLE_TO_SLEEP_MS = 60 * 1000;
static constexpr float    SOUND_ACTIVE_RATIO_ON = 0.06f;
static constexpr uint32_t SOUND_ACTIVE_HOLD_MS = 500;
static constexpr float    MOTION_DELTA_G_ON = 0.10f;
static constexpr uint32_t MOTION_ACTIVE_HOLD_MS = 700;
#endif

static constexpr uint32_t SHOUT_MIN_MS     = 20 * 1000;
static constexpr uint32_t SHOUT_MAX_MS     = 90 * 1000;

static constexpr float    LOUD_RATIO_THRESHOLD = 0.85f;
static constexpr uint8_t  LOUD_EVENT_TARGET_COUNT = 10;
static constexpr uint32_t LOUD_DEBOUNCE_MS = 350;
static constexpr uint32_t ANGRY_DURATION_MS = 5000;
static uint8_t  loud_event_count = 0;
static bool     loud_prev = false;
static uint32_t loud_last_event_msec = 0;
static bool     is_angry = false;
static uint32_t angry_end_msec = 0;
static Expression expression_before_angry = Expression::Neutral;

static uint32_t last_activity_msec = 0;
static bool     is_sleeping = false;

static uint32_t next_shout_msec = 0;
static uint32_t shout_end_msec = 0;
static uint32_t speech_clear_msec = 0;
static uint32_t force_mouth_until_msec = 0;

static Expression current_expression = Expression::Neutral;
static Expression expression_before_sleep = Expression::Neutral;
static Expression expression_before_shout = Expression::Neutral;

static bool sound_active = false;
static bool motion_active = false;

// --- 追加機能：消音とバッテリー ---
static bool is_mute = true;
static bool is_low_battery = false;
static uint32_t last_bat_check_msec = 0;
// --------------------------------

static void updateLEDColor() {
  CRGB ledColor;
  if (sleep_bg_on) {
    ledColor = CRGB::Black;
  } else if (angry_bg_on) {
    ledColor = CRGB::Red;
  } else {
    switch(palette_index) {
      case 0: ledColor = CRGB::Navy; break;
      case 1: ledColor = CRGB::Orange; break;
      case 2: ledColor = CRGB(0, 50, 0); break;
      case 3: ledColor = CRGB::Black; break;
      case 4: ledColor = CRGB::White; break;
      case 5: ledColor = CRGB::Green; break;
      default: ledColor = CRGB::Black; break;
    }
  }
  fill_solid(leds, NUM_LEDS, ledColor);
  FastLED.show();
}

static void setExpressionTracked(Expression e) {
  current_expression = e;
  avatar.setExpression(e);
}

static void applyAngryBackground(bool on) {
  if (angry_bg_on == on) return;
  angry_bg_on = on;
  for (int i = 0; i < 6; ++i) {
    if (!cps[i]) continue;
    uint32_t bg;
    if (sleep_bg_on) {
      bg = SLEEP_BG_COLOR;
    } else {
      bg = on ? ANGRY_BG_COLOR : cps_bg_default[i];
    }
    cps[i]->set(COLOR_BACKGROUND, bg);
  }
  if (palette_index < 6 && cps[palette_index]) {
    avatar.setColorPalette(*cps[palette_index]);
  }
  updateLEDColor();
}

static void applySleepBackground(bool on) {
  if (sleep_bg_on == on) return;
  sleep_bg_on = on;
  for (int i = 0; i < 6; ++i) {
    if (!cps[i]) continue;
    uint32_t bg;
    if (sleep_bg_on) {
      bg = SLEEP_BG_COLOR;
    } else {
      bg = angry_bg_on ? ANGRY_BG_COLOR : cps_bg_default[i];
    }
    cps[i]->set(COLOR_BACKGROUND, bg);
  }
  if (palette_index < 6 && cps[palette_index]) {
    avatar.setColorPalette(*cps[palette_index]);
  }
  updateLEDColor();
}

static void scheduleNextShout(uint32_t now) {
  next_shout_msec = now + (uint32_t)random((long)SHOUT_MIN_MS, (long)SHOUT_MAX_MS + 1);
}

static float motionDeltaG() {
  if (!M5.Imu.isEnabled()) return 0.0f;
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);
  static bool inited = false;
  static float pax = 0, pay = 0, paz = 0;
  if (!inited) {
    pax = ax; pay = ay; paz = az;
    inited = true;
    return 0.0f;
  }
  float dx = ax - pax;
  float dy = ay - pay;
  float dz = az - paz;
  pax = ax; pay = ay; paz = az;
  float d = std::sqrt(dx*dx + dy*dy + dz*dz);
  return d;
}

static bool updateMotionActive(uint32_t now) {
  static uint32_t last_motion_msec = 0;
  float d = motionDeltaG();
  if (d > MOTION_DELTA_G_ON) {
    last_motion_msec = now;
  }
  return (uint32_t)(now - last_motion_msec) < MOTION_ACTIVE_HOLD_MS;
}

static bool updateSoundActive(float ratio, uint32_t now) {
  static uint32_t last_sound_msec = 0;
  if (ratio >= SOUND_ACTIVE_RATIO_ON) {
    last_sound_msec = now;
  }
  return (uint32_t)(now - last_sound_msec) < SOUND_ACTIVE_HOLD_MS;
}

static void enterSleep(uint32_t now) {
  applyAngryBackground(false);
  applySleepBackground(true);
  is_sleeping = true;
  expression_before_sleep = current_expression;
  setExpressionTracked(Expression::Sleepy);
  avatar.setSpeechText("Zzz...");
  avatar.setMouthOpenRatio(0.0f);
  next_shout_msec = now + SHOUT_MAX_MS;
  shout_end_msec = 0;
  speech_clear_msec = 0;
  force_mouth_until_msec = 0;
}

static void wakeUp(uint32_t now) {
  is_sleeping = false;
  applySleepBackground(false);
  
  // バッテリー低下時は起きてもアピールする
  if (is_low_battery) {
    avatar.setSpeechText("I need some energy...");
    setExpressionTracked(Expression::Sad);
  } else {
    avatar.setSpeechText("");
    setExpressionTracked(expression_before_sleep);
  }
  
  last_activity_msec = now;
  scheduleNextShout(now);
}

static void doShout(uint32_t now) {
  if (is_sleeping) return;
  if (is_angry) return;
  if (shout_end_msec != 0 && now < shout_end_msec) return;

  expression_before_shout = current_expression;
  setExpressionTracked(Expression::Happy);
  avatar.setSpeechText("Oh!");
  force_mouth_until_msec = now + 300;
  shout_end_msec = now + 2000;
  speech_clear_msec = now + 2000;

#ifdef USE_MIC
  if (M5.Mic.isEnabled()) {
    while (M5.Mic.isRecording()) { M5.delay(1); }
    M5.Mic.end();
  }
#endif

  // ミュートでなければ音を鳴らす
  if (!is_mute) {
    M5.Speaker.begin();
    M5.Speaker.setVolume(S_VOL);
    M5.Speaker.tone(1400, 90);
    M5.delay(100);
    M5.Speaker.tone(900, 140);
    while (M5.Speaker.isPlaying()) { M5.update(); M5.delay(1); }
    M5.Speaker.end();
  }

#ifdef USE_MIC
  M5.Mic.begin();
#endif

  last_activity_msec = now;
}

static void updateTimedActions(uint32_t now) {
  // --- 1. Shout(Oh!)の終了判定 ---
  if (shout_end_msec != 0 && now >= shout_end_msec) {
    shout_end_msec = 0;
    
    // ★バグ修正ポイント：どんな状態でも確実に正しい顔に戻るようにする
    if (!is_sleeping && !is_angry) {
      if (is_low_battery) {
        setExpressionTracked(Expression::Sad); // バッテリー低下時は悲しい顔
      } else {
        setExpressionTracked(Expression::Neutral); // 通常時は普通の顔
      }
    } else {
      // 怒り・スリープ中にShoutが終了した場合のバグ対策
      // 終了後にHappyへ戻らないよう、記憶している顔をNeutralにリセットしておく
      expression_before_angry = Expression::Neutral;
      expression_before_sleep = Expression::Neutral;
    }
  }
  
  // --- 2. セリフの消去判定 ---
  if (speech_clear_msec != 0 && now >= speech_clear_msec) {
    speech_clear_msec = 0;
    if (is_low_battery && !is_sleeping) {
      avatar.setSpeechText("I need some energy...");
    } else if (!is_sleeping) {
      avatar.setSpeechText("");
    }
  }

  // --- 3. 怒りモードの終了判定 ---
  if (is_angry && angry_end_msec != 0 && now >= angry_end_msec) {
    is_angry = false;
    applyAngryBackground(false);
    angry_end_msec = 0;
    
    if (!is_sleeping) {
      if (is_low_battery) {
        setExpressionTracked(Expression::Sad);
      } else {
        setExpressionTracked(expression_before_angry); // バグ対策済みの顔に戻る
      }
    }
  }
}

static void enterAngry(uint32_t now) {
  if (is_angry) return;
  is_angry = true;
  expression_before_angry = current_expression;
  setExpressionTracked(Expression::Angry);
  applyAngryBackground(true);
  if (ANGRY_DURATION_MS == 0) {
    angry_end_msec = 0;
  } else {
    angry_end_msec = now + ANGRY_DURATION_MS;
  }
  loud_event_count = 0;

#ifdef USE_MIC
  if (M5.Mic.isEnabled()) {
    while (M5.Mic.isRecording()) { M5.delay(1); }
    M5.Mic.end();
  }
#endif

  // ミュートでなければ警告音を鳴らす
  if (!is_mute) {
    M5.Speaker.begin();
    M5.Speaker.setVolume(S_VOL);
    for(int i = 0; i < 3; i++) {
      M5.Speaker.tone(2000, 100);
      M5.delay(150);
    }
    while (M5.Speaker.isPlaying()) { M5.update(); M5.delay(1); }
    M5.Speaker.end();
  }

#ifdef USE_MIC
  M5.Mic.begin();
#endif
}

static void updateAngryByLoudSound(float ratio, uint32_t now) {
  if (is_sleeping) return;
#ifndef SDL_h_
  if (!M5.Mic.isEnabled()) return;
#endif
  bool loud_now = (ratio >= LOUD_RATIO_THRESHOLD);
  if (loud_now && !loud_prev) {
    if ((uint32_t)(now - loud_last_event_msec) > LOUD_DEBOUNCE_MS) {
      loud_last_event_msec = now;
      if (loud_event_count < 255) loud_event_count++;
      if (loud_event_count >= LOUD_EVENT_TARGET_COUNT) {
        enterAngry(now);
      }
    }
  }
  loud_prev = loud_now;
}

void lipsync() {
  size_t bytesread;
  uint64_t level = 0;
#ifndef SDL_h_
  if (!M5.Mic.isEnabled()) {
    avatar.setMouthOpenRatio(0.0f);
    return;
  }

  if ( M5.Mic.record(rec_data, WAVE_SIZE, record_samplerate)) {
    fft.exec(rec_data);
    for (size_t bx=5;bx<=60;++bx) {
      int32_t f = fft.get(bx);
      level += abs(f);
    }
  }
  uint32_t temp_level = level >> lipsync_shift_level;
  float ratio = (float)(temp_level / lipsync_max);
  if (ratio <= 0.01f) {
    ratio = 0.0f;
    if ((lgfx::v1::millis() - last_lipsync_max_msec) > 500) {
      last_lipsync_max_msec = lgfx::v1::millis();
      lipsync_max = LIPSYNC_LEVEL_MAX;
    }
  } else {
    if (ratio > 1.3f) {
      if (ratio > 1.5f) {
        lipsync_max += 10.0f;
      }
      ratio = 1.3f;
    }
    last_lipsync_max_msec = lgfx::v1::millis();
  }

  if ((lgfx::v1::millis() - last_rotation_msec) > 350) {
    int direction = random(-2, 2);
    avatar.setRotation(direction * 10 * ratio);
    last_rotation_msec = lgfx::v1::millis();
  }
#else
  float ratio = 0.0f;
#endif
  uint32_t now = lgfx::v1::millis();
  sound_active = updateSoundActive(ratio, now);

  if (now < force_mouth_until_msec && ratio < 0.9f) {
    ratio = 0.9f;
  }

  updateAngryByLoudSound(ratio, now);
  avatar.setMouthOpenRatio(ratio);
}

void setup()
{
  auto cfg = M5.config();
#if defined(ARDUINO_M5STACK_CORES3)
  cfg.internal_mic = true;
  cfg.internal_spk = true; // スピーカーON
#endif
  M5.begin(cfg);
  
  M5.Power.setExtOutput(true);

#ifdef ARDUINO
  randomSeed(esp_random());
#endif

#if defined( ARDUINO_M5STACK_CORES3 )
  unifiedButton.begin(&M5.Display, goblib::UnifiedButton::appearance_t::transparent_all);
#endif
  M5.Log.setLogLevel(m5::log_target_display, ESP_LOG_NONE);
  M5.Log.setLogLevel(m5::log_target_serial, ESP_LOG_INFO);
  M5.Log.setEnableColor(m5::log_target_serial, false);
  
  float scale = 0.0f;
  int8_t position_top = 0;
  int8_t position_left = 0;
  uint8_t display_rotation = 1;
  uint8_t first_cps = 0;
  auto mic_cfg = M5.Mic.config();
  
  switch (M5.getBoard()) {
    case m5::board_t::board_M5StackCoreS3:
    case m5::board_t::board_M5StackCoreS3SE:
      first_cps = 0;
      scale = 1.0f;
      position_top = 0;
      position_left = 0;
      display_rotation = 1;
      mic_cfg.noise_filter_level = (mic_cfg.noise_filter_level + 8) & 255;
      M5.Mic.config(mic_cfg);
      break;
    default:
      scale = 1.0f;
      display_rotation = 1;
      break;
  }

#ifndef SDL_h_
  rec_data = (typeof(rec_data))heap_caps_malloc(WAVE_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
  memset(rec_data, 0 , WAVE_SIZE * sizeof(int16_t));
  M5.Speaker.end();
  M5.Mic.begin();
#endif

  M5.Display.setRotation(display_rotation);
  avatar.setScale(scale);
  avatar.setPosition(position_top, position_left);
  avatar.init();

  cps[0] = new ColorPalette();
  cps[0]->set(COLOR_PRIMARY, TFT_WHITE);
  cps[0]->set(COLOR_BACKGROUND, TFT_NAVY);
  cps[1] = new ColorPalette();
  cps[1]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[1]->set(COLOR_BACKGROUND, TFT_ORANGE);
  cps[2] = new ColorPalette();
  cps[2]->set(COLOR_PRIMARY, (uint16_t)0x00ff00);
  cps[2]->set(COLOR_BACKGROUND, (uint16_t)0x303303);
  cps[3] = new ColorPalette();
  cps[3]->set(COLOR_PRIMARY, TFT_WHITE);
  cps[3]->set(COLOR_BACKGROUND, TFT_BLACK);
  cps[4] = new ColorPalette();
  cps[4]->set(COLOR_PRIMARY, TFT_BLACK);
  cps[4]->set(COLOR_BACKGROUND, TFT_WHITE);
  cps[5] = new ColorPalette();
  cps[5]->set(COLOR_PRIMARY, (uint16_t)0x303303);
  cps[5]->set(COLOR_BACKGROUND, (uint16_t)0x00ff00);
  
  palette_index = first_cps;
  avatar.setColorPalette(*cps[first_cps]);
  setExpressionTracked(Expression::Neutral);
  
  last_activity_msec = lgfx::v1::millis();
  scheduleNextShout(last_activity_msec);
  last_rotation_msec = lgfx::v1::millis();

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(30); 
  updateLEDColor();

  // --- 環境光センサーの初期化 ---
  if (M5.getBoard() == m5::board_t::board_M5StackCoreS3 || 
      M5.getBoard() == m5::board_t::board_M5StackCoreS3SE) {
    if (initALS()) {
      M5_LOGI("LTR-553ALS init success!");
    } else {
      M5_LOGE("LTR-553ALS init failed!");
    }
  }

  M5_LOGI("setup end");
}

void loop()
{
  M5.update();
  uint32_t now = lgfx::v1::millis();

#if defined( ARDUINO_M5STACK_CORES3 )
  unifiedButton.update();
#endif
  
  bool user_activity = false;
  
  // BtnA (画面左下)
  if (M5.BtnA.wasPressed()) {
    user_activity = true;
    palette_index++;
    if (palette_index > 5) palette_index = 0;
    avatar.setColorPalette(*cps[palette_index]);
    updateLEDColor();
  }
  
  if (M5.BtnA.wasDoubleClicked()) {
    user_activity = true;
    M5.Display.setRotation(3);
  }
  
  // BtnB (画面中央下) : 消音モード切り替え
  if (M5.BtnB.wasPressed()) {
    user_activity = true;
    is_mute = !is_mute; // ON/OFFを反転させる
    if (is_mute) {
      avatar.setSpeechText("Mute: ON");
    } else {
      avatar.setSpeechText("Mute: OFF");
    }
    speech_clear_msec = now + 2000; // 2秒後に文字を消す
  }

  // 電源ボタン
  if (M5.BtnPWR.wasClicked()) {
    user_activity = true;
#ifdef ARDUINO
    esp_restart();
#endif
  } 

  // --- バッテリー監視 (10秒に1回チェック) ---
  if (now - last_bat_check_msec > 10000) {
    last_bat_check_msec = now;
    int bat_level = M5.Power.getBatteryLevel();
    
    // バッテリーが10%未満の場合
    if (bat_level >= 0 && bat_level < 10) {
      if (!is_low_battery) {
        is_low_battery = true;
        avatar.setSpeechText("I need some energy...");
        setExpressionTracked(Expression::Sad); // 悲しい顔にする
      }
    } else {
      // バッテリーが回復した場合
      if (is_low_battery) {
        is_low_battery = false;
        if (speech_clear_msec == 0) avatar.setSpeechText("");
        if (!is_angry && !is_sleeping) setExpressionTracked(Expression::Neutral);
      }
    }
  }
  // ------------------------------------------

  motion_active = updateMotionActive(now);
  lipsync();

  bool active = user_activity || motion_active || sound_active;
  if (active) last_activity_msec = now;

  if (!is_sleeping) {
    if ((uint32_t)(now - last_activity_msec) > IDLE_TO_SLEEP_MS) {
      enterSleep(now);
    }
  } else {
    if (active) wakeUp(now);
  }

  if (!is_sleeping && !is_angry && now >= next_shout_msec) {
    doShout(now);
    scheduleNextShout(now);
  }

  // --- 環境光センサーを使ったダークモード判定 (3秒に1回チェック) ---
  if (now - last_als_check_msec > 3000) {
    last_als_check_msec = now;
    int brightness = getAmbientLight();
    
    // ★デバッグ用：現在取得している明るさの数値をアバターに喋らせる
    // char debug_text[32];
    // sprintf(debug_text, "Light: %d", brightness);
    // avatar.setSpeechText(debug_text);
    // speech_clear_msec = now + 2000; // 2秒後に吹き出しを消す
    
    // I2C通信エラー時(65535など)は処理をスキップする
    if (brightness < 65000) { 
      if (brightness < DARK_THRESHOLD) {
        if (!is_dark_mode) {
          is_dark_mode = true;
          M5.Display.setBrightness(30); // 画面を暗くする
          if (!is_sleeping) enterSleep(now);
        }
      } else {
        if (is_dark_mode) {
          is_dark_mode = false;
          M5.Display.setBrightness(128); // 元の明るさに戻す
          if (is_sleeping) wakeUp(now);
        }
      }
    }
  }
  // ----------------------------------------------------
  
  
  updateTimedActions(now);
  lgfx::v1::delay(1);
}
