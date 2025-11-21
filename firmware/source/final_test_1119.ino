#include "LEDController.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <DFRobot_I2C_Multiplexer.h>

// ==== 펌프 동작 핀 : D8, 초음파 트리거 핀 : D7, 초음파 에코 핀 : A0 ==== 
const int TRIG_PIN = 7;
const int ECHO_PIN = A0;
const int PUMP_PIN = 8;

// ==== 네오픽셀 ====
// 상시 ON LED → D5
#define LED_ALWAYS_PIN   5
#define LED_ALWAYS_COUNT 20

// 트리거 LED → D6
#define LED_TRIG_PIN     6
#define LED_TRIG_COUNT   1

// 상시 LED와 트리거 LED의 밝기
#define ALWAYS_BRIGHTNESS 20
#define TRIG_BRIGHTNESS 255

// 상시 ON(1~8번) = 흰색
#define LED_ON_R 255
#define LED_ON_G 255
#define LED_ON_B 255

// 트리거 영역(9~16번) = 파란색
#define LED_TRIG_R 0
#define LED_TRIG_G 0
#define LED_TRIG_B 255

// ==== 초음파 상수 ====
const float US_PER_CM = 58.0;
const float TRIGGER_DISTANCE = 12.0;
const unsigned long PUMP_ON_TIME = 1000UL;

// ==== I2C 멀티플렉서 ====
#define I2C_MUX_ADDR   0x70
#define MUX_PORT_MESSI 0
#define MUX_PORT_RONALDO 1   // ★ 새 OLED 연결

// ==== 상시 LED 색 순환용 ====
// 흰-빨-주-노-초-파-보
struct RGB {
  uint8_t r, g, b;
};

const RGB ALWAYS_COLORS[] = {
  {255, 255, 255}, // 흰
  {255,   0,   0}, // 빨
  {255, 100,   0}, // 주 (적당한 주황)
  {255, 255,   0}, // 노
  {  0, 255,   0}, // 초
  {  0,   0, 255}, // 파
  {180,   0, 255}  // 보 (보라)
};

const uint8_t ALWAYS_COLOR_COUNT = sizeof(ALWAYS_COLORS) / sizeof(ALWAYS_COLORS[0]);
uint8_t alwaysColorIndex = 0;

unsigned long lastColorChangeMillis = 0;
const unsigned long COLOR_CHANGE_INTERVAL = 1000; // 0.5초마다 색 변경

enum SystemState {
  STATE_IDLE,        // 평상시: 색 계속 바뀜 + 트리거 감시
  STATE_PUMP_ON,     // 펌프 동작 중
  STATE_COOLDOWN     // 펌프 끝난 후 재트리거 방지 시간
};

SystemState systemState = STATE_IDLE;

unsigned long pumpStartMillis = 0;
unsigned long cooldownStartMillis = 0;
const unsigned long COOLDOWN_TIME = 2000UL; // 재트리거 방지 시간 (2초)

DFRobot_I2C_Multiplexer mux(&Wire, I2C_MUX_ADDR);

// ==== OLED 두 개 ====
// Messi (채널 0)
U8G2_SSD1306_128X64_NONAME_1_HW_I2C dispMessi(U8G2_R0, U8X8_PIN_NONE);

// Ronaldo (채널 1) ★ 새 OLED
U8G2_SSD1306_128X64_NONAME_1_HW_I2C dispRonaldo(U8G2_R0, U8X8_PIN_NONE);

// ==== NeoPixel 객체 2개 ====
LEDController ledsAlways(LED_ALWAYS_COUNT, LED_ALWAYS_PIN);  // D5 → 상시 흰색
LEDController ledsTrigger(LED_TRIG_COUNT, LED_TRIG_PIN);     // D6 → 트리거 파랑

void setAlwaysStripColor(uint8_t colorIndex) {
  RGB c = ALWAYS_COLORS[colorIndex];
  for (int i = 0; i < LED_ALWAYS_COUNT; i++) {
    ledsAlways.setPixel(i, c.r, c.g, c.b);
  }
  ledsAlways.show();
}

void drawCentered(U8G2 &disp, int y, const char* text) {
  int16_t textWidth = disp.getStrWidth(text);
  int16_t x = (128 - textWidth) / 2;
  disp.drawStr(x, y, text);
}

void setup() {
  Serial.begin(9600);

  // 센서/펌프 핀 설정
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);

  // ---- NeoPixel 초기화 ----
  ledsAlways.begin(ALWAYS_BRIGHTNESS);
  ledsTrigger.begin(TRIG_BRIGHTNESS);

  // 상시 LED: 색 순환용 초기값 (처음은 흰색)
  alwaysColorIndex = 0;
  setAlwaysStripColor(alwaysColorIndex);
  lastColorChangeMillis = millis();


  // 트리거 LED(9~16)는 처음 OFF
  ledsTrigger.clearAll();
  ledsTrigger.show();

  // ---- I2C / MUX 초기화 ----
  Wire.begin();
  mux.begin();

  // ==== OLED #1: Messi (포트0) ====
  mux.selectPort(MUX_PORT_MESSI);
dispMessi.begin();
dispMessi.setFont(u8g2_font_fub20_tr);

dispMessi.firstPage();
do {
  drawCentered(dispMessi, 40, "Apple");
 } while (dispMessi.nextPage());

// -----------------
mux.selectPort(MUX_PORT_RONALDO);
dispRonaldo.begin();
dispRonaldo.setFont(u8g2_font_fub20_tr);

dispRonaldo.firstPage();
do {
  drawCentered(dispRonaldo, 40, "Banana");
 } while (dispRonaldo.nextPage());

  Serial.println("HC-SR04 + Pump + NeoPixel x2 strip + Dual OLED via I2C MUX start");
}

void loop() {
  unsigned long now = millis();

  // 1) 상태에 따라 상시 LED 색 순환 (IDLE에서만)
  if (systemState == STATE_IDLE) {
    if (now - lastColorChangeMillis >= COLOR_CHANGE_INTERVAL) {
      alwaysColorIndex = (alwaysColorIndex + 1) % ALWAYS_COLOR_COUNT;
      setAlwaysStripColor(alwaysColorIndex);
      lastColorChangeMillis = now;
    }
  }
  // STATE_PUMP_ON / STATE_COOLDOWN에서는 색을 갱신하지 않으므로
  // "지금 색에서 멈춘 상태"가 유지됨.

  // 2) 거리 측정 (예전과 동일, 다만 마지막 delay는 제거할 예정)
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  float distance_cm = (duration > 0) ? (duration / US_PER_CM) : 999.0;

  Serial.print("Distance: ");
  Serial.print(distance_cm);
  Serial.println(" cm");

  // 3) 상태머신으로 펌프/트리거 LED 제어
  switch (systemState) {
    case STATE_IDLE:
      // 평상시: 거리가 임계값 이하로 들어오면 트리거
      if (distance_cm <= TRIGGER_DISTANCE) {
        Serial.println("Trigger detected");

        // 트리거 LED 파란색 ON
        for (int i = 0; i < LED_TRIG_COUNT; i++) {
          ledsTrigger.setPixel(i, LED_TRIG_R, LED_TRIG_G, LED_TRIG_B);
        }
        ledsTrigger.show();

        // 펌프 ON
        digitalWrite(PUMP_PIN, HIGH);
        pumpStartMillis = now;
        systemState = STATE_PUMP_ON;
      } else {
        digitalWrite(PUMP_PIN, LOW); // 안전상 한번 더 확인
      }
      break;

    case STATE_PUMP_ON:
      // 펌프 ON 상태가 PUMP_ON_TIME만큼 유지되면 OFF로 전환
      if (now - pumpStartMillis >= PUMP_ON_TIME) {
        digitalWrite(PUMP_PIN, LOW);

        // 트리거 LED OFF
        ledsTrigger.clearAll();
        ledsTrigger.show();

        // 재트리거 방지용 쿨다운 상태 진입
        cooldownStartMillis = now;
        systemState = STATE_COOLDOWN;
      }
      break;

    case STATE_COOLDOWN:
      // 쿨다운 시간 동안은 거리와 상관없이 아무 동작 안 함
      if (now - cooldownStartMillis >= COOLDOWN_TIME) {
        systemState = STATE_IDLE;
      }
      break;
  }

  // 4) loop 끝에서 큰 delay는 넣지 말고, 필요하면 아주 작은 delay만
  delay(5); // 또는 아예 생략해도 무방
}

