/**
 * @file main.cpp
 * @brief バケット昇降制御（Arduino UNO R3）
 *
 * ■ ピン配置
 *   pin_BUTTON : ボタン       D4  — 任意のデジタルピンでOK（特殊機能なし）
 *                                   内蔵プルアップ使用・押下時 LOW
 *                                   ボタンの片側をD4、もう片側をGNDに繋ぐだけ
 *   pin_SW_A   : リミットSwA  D5  — 上昇完了検知（NC: 押されていない間通電）
 *   pin_SW_B   : リミットSwB  D6  — 下降完了検知（NC: 押されていない間通電）
 *   pin_PWM    : PWM出力      D10 — モータドライバ速度入力
 *   pin_DIR    : DIR出力      D9  — モータドライバ方向入力（bool制御）
 *   pin_LED    : LED          D13 — 作動中 点灯 / 待機中 消灯
 *
 * ■ ボタンの配線（シンプル・外付け抵抗不要）
 *   D4 ──[ボタン]── GND
 *   INPUT_PULLUP を使うので外付け抵抗は一切不要。
 *   押していない: D4 = HIGH（内蔵プルアップ）
 *   押している  : D4 = LOW （GNDに落ちる）
 *   ※ 割り込み（interrupt）は使っていない。普通の digitalRead のみ。
 *
 * ■ NCリミットスイッチの配線
 *   D5(or D6) ──[NCスイッチ]── GND
 *   押されていない（通電=閉路）: ピン = LOW
 *   リミット到達（開路）      : ピン = HIGH（内蔵プルアップ）
 *
 * ■ 動作フロー
 *   IDLE →[ボタン押下]→ RISING →[SwA反応]→ WAITING(2秒) →[経過]→ LOWERING →[SwB反応]→ IDLE
 */

#include <Arduino.h>

// ============================================================
//  デバッグモード
//  有効にする: 下の行をそのままにする
//  無効にする: 下の行を // でコメントアウトする
// ============================================================
//#define DEBUG

#ifdef DEBUG
  // デバッグ出力の間隔 [ms]（短すぎるとシリアルが詰まる）
  const uint32_t DEBUG_INTERVAL_MS = 200;
#endif

// ============================================================
//  ピン定数（配線変更はここだけ修正する）
//  ※ D2・D3 は割り込みピンなので混乱を避けるため使わない
// ============================================================
const uint8_t pin_BUTTON = 4;   // ボタン（INPUT_PULLUP、押下=LOW）← D4は普通のデジタルピン
const uint8_t pin_SW_A   = 5;   // リミットSwA 上昇完了（NC、INPUT_PULLUP）
const uint8_t pin_SW_B   = 6;   // リミットSwB 下降完了（NC、INPUT_PULLUP）
const uint8_t pin_PWM    = 10;  // モータ速度 PWM出力
const uint8_t pin_DIR    = 9;   // モータ方向 DIR出力
const uint8_t pin_LED    = 13;  // インジケータ LED

// ============================================================
//  動作パラメータ（調整用）
// ============================================================
const uint8_t  MOTOR_SPEED  = 50;   // モータ速度（0〜255）
const uint32_t WAIT_MS      = 2000;  // 上昇停止後の待機時間 [ms]
const uint32_t DEBOUNCE_MS  = 50;    // ボタンのチャタリング除去時間 [ms]
const bool     DIR_UP       = false; // 上昇方向のDIR値（逆なら false）

// ============================================================
//  状態定義
//  ※ RISING/LOWERING/IDLE は Arduino 組み込みマクロと重複するため ST_ を付与
// ============================================================
enum State {
    ST_IDLE,     // 待機中（ボタン入力受付、LED消灯）
    ST_RISING,   // 上昇中（SwA 待ち、LED点灯）
    ST_WAITING,  // 上昇完了後の一時停止（2秒待機、LED点灯）
    ST_LOWERING  // 下降中（SwB 待ち、LED点灯）
};

State    state      = ST_IDLE;
uint32_t waitStart  = 0;      // WAITING 開始時刻

// ============================================================
//  ヘルパー関数
// ============================================================

/**
 * @brief NCリミットスイッチがリミット位置に達したか判定する。
 *   押されていない（閉路）: LOW  → false（まだ到達していない）
 *   リミット到達（開路）  : HIGH → true
 */
bool isAtLimit(uint8_t pin) {
    return (digitalRead(pin) == HIGH);
}

/**
 * @brief ボタンの押下を検出する（デバウンス付きワンショット検出）。
 *
 * INPUT_PULLUP: 押下時 LOW (GND接続)
 * DEBOUNCE_MS 以上連続して安定した後に、押された瞬間（OFF→ON）だけ 1度 true を返す。
 *
 * @return true ボタンが確実に押された瞬間のみ
 */
bool buttonPressed() {
    static bool buttonState = false;       // 確定したボタン状態 (true=押下中)
    static bool lastRawState = false;      // 直前の生読み取り値
    static uint32_t lastDebounceTime = 0;  // 変化検知時刻

    // INPUT_PULLUP なので LOW が押下 (true)
    const bool rawState = (digitalRead(pin_BUTTON) == LOW);

    // チャタリング等で生の値が変化したらタイマー更新
    if (rawState != lastRawState) {
        lastDebounceTime = millis();
        lastRawState = rawState;
    }

    // 変化から DEBOUNCE_MS 以上安定しているか確認
    if ((millis() - lastDebounceTime) >= DEBOUNCE_MS) {
        // 確定した状態と異なっていれば更新
        if (rawState != buttonState) {
            buttonState = rawState;
            // 離された状態から「押された状態」へ切り替わった瞬間だけ true を返す
            if (buttonState) {
                return true;
            }
        }
    }

    return false;
}

/** @brief モータを指定方向・速度で駆動する。 */
void motorDrive(bool up, uint8_t speed) {
    digitalWrite(pin_DIR, up ? HIGH : LOW);
    analogWrite(pin_PWM, speed);
}

/** @brief モータを停止する（PWM=0）。 */
void motorStop() {
    analogWrite(pin_PWM, 0);
}

#ifdef DEBUG
/**
 * @brief 全入力ピンの状態をシリアルモニタに出力する（DEBUGモード専用）。
 *
 * DEBUG_INTERVAL_MS ごとに1行出力する。毎ループ呼んでもシリアルを詰まらせない。
 *
 * 出力例:
 *   [DBG] State=IDLE | BTN=OFF | SwA=OFF | SwB=OFF
 *   [DBG] State=RISING | BTN=ON  | SwA=OFF | SwB=OFF
 *
 * ピンの論理:
 *   ボタン : INPUT_PULLUP → 押下=LOW → 表示では ON/OFF に変換
 *   SwA/SwB: INPUT_PULLUP + NC → リミット到達=HIGH → 表示では ON/OFF に変換
 */
void debugPrint() {
    static uint32_t lastPrint = 0;
    if ((millis() - lastPrint) < DEBUG_INTERVAL_MS) return;
    lastPrint = millis();

    // 各ピンの状態を読み取る
    const bool btn  = (digitalRead(pin_BUTTON) == LOW);   // 押下=LOW→反転してON
    const bool swA  = (digitalRead(pin_SW_A)   == HIGH);  // リミット到達=HIGH
    const bool swB  = (digitalRead(pin_SW_B)   == HIGH);  // リミット到達=HIGH

    // 現在の状態名
    const char* stateStr;
    switch (state) {
        case ST_IDLE:     stateStr = "IDLE    "; break;
        case ST_RISING:   stateStr = "RISING  "; break;
        case ST_WAITING:  stateStr = "WAITING "; break;
        case ST_LOWERING: stateStr = "LOWERING"; break;
        default:          stateStr = "UNKNOWN "; break;
    }

    Serial.print(F("[DBG] State="));
    Serial.print(stateStr);
    Serial.print(F(" | BTN="));
    Serial.print(btn  ? F("ON ") : F("OFF"));
    Serial.print(F(" | SwA="));
    Serial.print(swA  ? F("ON ") : F("OFF"));
    Serial.print(F(" | SwB="));
    Serial.println(swB ? F("ON ") : F("OFF"));
}
#endif

// ============================================================
//  setup
// ============================================================
void setup() {
    // 全入力を INPUT_PULLUP に統一（外付け抵抗不要）
    // ボタン   : 押下=LOW
    // NCスイッチ: 通常=LOW、リミット到達=HIGH
    pinMode(pin_BUTTON, INPUT_PULLUP);
    pinMode(pin_SW_A,   INPUT_PULLUP);
    pinMode(pin_SW_B,   INPUT_PULLUP);

    pinMode(pin_PWM, OUTPUT);
    pinMode(pin_DIR, OUTPUT);
    pinMode(pin_LED, OUTPUT);

    motorStop();
    digitalWrite(pin_LED, HIGH);

    Serial.begin(9600);
    Serial.println(F("Ready. State: IDLE"));
}

// ============================================================
//  loop（非ブロッキング・ステートマシン）
// ============================================================
void loop() {
    switch (state) {

        // --------------------------------------------------------
        case ST_IDLE:
        // ボタンが確実に押されたら上昇開始。
        // SwA がすでに反応している（上端にいる）場合は無視。
        // --------------------------------------------------------
            if (!buttonPressed()) break;

            if (isAtLimit(pin_SW_A)) {
                Serial.println(F("Already at top. Ignored."));
            } else {
                state = ST_RISING;
                digitalWrite(pin_LED, LOW);
                motorDrive(DIR_UP, MOTOR_SPEED);
                Serial.println(F("-> RISING"));
            }
            break;

        // --------------------------------------------------------
        case ST_RISING:
        // SwA が反応したら停止して待機タイマー開始。
        // --------------------------------------------------------
            if (!isAtLimit(pin_SW_A)) break;

            motorStop();
            waitStart = millis();
            state = ST_WAITING;
            Serial.println(F("-> WAITING"));
            break;

        // --------------------------------------------------------
        case ST_WAITING:
        // WAIT_MS 経過したら下降開始（delay() 不使用・非ブロッキング）。
        // --------------------------------------------------------
            if ((millis() - waitStart) < WAIT_MS) break;

            state = ST_LOWERING;
            motorDrive(!DIR_UP, MOTOR_SPEED);
            Serial.println(F("-> LOWERING"));
            break;

        // --------------------------------------------------------
        case ST_LOWERING:
        // SwB が反応したら停止して IDLE に戻る。
        // --------------------------------------------------------
            if (!isAtLimit(pin_SW_B)) break;

            motorStop();
            digitalWrite(pin_LED, HIGH);
            state = ST_IDLE;
            Serial.println(F("-> IDLE"));
            break;
    }

#ifdef DEBUG
    debugPrint();  // DEBUGが有効なときだけ200msごとにピン状態を出力
#endif
}
