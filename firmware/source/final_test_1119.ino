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


// ==== 초음파 입력시 LED 업데이트를 위한 전역 상태 변수 ====
enum AlwaysMode {
  ALWAYS_MODE_IDLE,   // 평상시: 느리게 변화
  ALWAYS_MODE_PUMP    // 펌프 ON 동안: 빠르게 여러 바퀴
};

AlwaysMode alwaysMode = ALWAYS_MODE_IDLE;

// 공통으로 쓰는 타이머 (논리적인 Always_Update_Time)
unsigned long lastAlwaysUpdateMillis = 0;

// IDLE일 때 색 변경 간격
const unsigned long ALWAYS_IDLE_INTERVAL = 1000;  // 예: 1초마다

// PUMP ON 3초 동안 3바퀴 돌리고 싶다면:
const uint8_t ALWAYS_TURNS_WHILE_PUMP = 3;  // 3바퀴
const uint16_t ALWAYS_STEPS_WHILE_PUMP = ALWAYS_COLOR_COUNT * ALWAYS_TURNS_WHILE_PUMP;
// 펌프 ON 동안 한 step당 간격
const unsigned long ALWAYS_PUMP_INTERVAL = PUMP_ON_TIME / ALWAYS_STEPS_WHILE_PUMP;

// 현재 펌프 동안 몇 step 진행했는지 (과하게 돌지 않게)
uint16_t alwaysPumpStepCount = 0;


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


// ==== 초음파 입력시, Update 함수 ====
void updateAlwaysStrip(unsigned long now) {
  unsigned long interval;

  if (alwaysMode == ALWAYS_MODE_IDLE) {
    interval = ALWAYS_IDLE_INTERVAL;  // 느리게
  } else { // ALWAYS_MODE_PUMP
    interval = ALWAYS_PUMP_INTERVAL;  // 빠르게
  }

  if (now - lastAlwaysUpdateMillis >= interval) {
    // 다음 색으로 한 칸 이동
    alwaysColorIndex = (alwaysColorIndex + 1) % ALWAYS_COLOR_COUNT;
    setAlwaysStripColor(alwaysColorIndex);

    lastAlwaysUpdateMillis = now;

    // 펌프 중일 때는 step 카운트도 올림 (원하면 제한 가능)
    if (alwaysMode == ALWAYS_MODE_PUMP) {
      alwaysPumpStepCount++;
      // 굳이 제한하고 싶으면:
      // if (alwaysPumpStepCount >= ALWAYS_STEPS_WHILE_PUMP) {
      //   항상Mode를 IDLE로 돌리거나, 그냥 더 안돌게 해도 됨.
      // }
    }
  }
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

  // 1) Always LED는 모든 상태에서 항상 이 함수로만 관리
  updateAlwaysStrip(now);

  // 2) 거리 측정
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

  // 3) 상태머신
  switch (systemState) {
    case STATE_IDLE:
      if (distance_cm <= TRIGGER_DISTANCE) {
        Serial.println("Trigger detected → PUMP_ON");

        // ★ 펌프 ON 들어갈 때: Always 모드를 PUMP로 전환
        alwaysMode = ALWAYS_MODE_PUMP;
        alwaysPumpStepCount = 0;            // 카운터 리셋
        lastAlwaysUpdateMillis = now;       // 펌프용 기준 시간 재설정

        // TRIG LED ON
        for (int i = 0; i < LED_TRIG_COUNT; i++) {
          ledsTrigger.setPixel(i, LED_TRIG_R, LED_TRIG_G, LED_TRIG_B);
        }
        ledsTrigger.show();

        // 펌프 ON
        digitalWrite(PUMP_PIN, HIGH);
        pumpStartMillis = now;

        systemState = STATE_PUMP_ON;
      } else {
        digitalWrite(PUMP_PIN, LOW); // 안전용
      }
      break;

    case STATE_PUMP_ON:
      if (now - pumpStartMillis >= PUMP_ON_TIME) {
        // 펌프 OFF
        digitalWrite(PUMP_PIN, LOW);

        // TRIG LED OFF
        ledsTrigger.clearAll();
        ledsTrigger.show();

        // ★ 펌프 끝날 때: Always 모드를 다시 IDLE로
        alwaysMode = ALWAYS_MODE_IDLE;
        lastAlwaysUpdateMillis = now;   // IDLE용 기준 시간 다시 잡기

        cooldownStartMillis = now;
        systemState = STATE_COOLDOWN;
      }
      break;

    case STATE_COOLDOWN:
      if (now - cooldownStartMillis >= COOLDOWN_TIME) {
        systemState = STATE_IDLE;
        // IDLE 들어갈 때 추가로 뭔가 리셋하고 싶으면 여기서
      }
      break;
  }

  // 필요하면 소량 delay
  delay(5);
}


