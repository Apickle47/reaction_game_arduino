#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_timer.h"
#include <Preferences.h>


// ---------- constants ----------
#define NUM_LEDS 3
#define BUTTON_PIN 4
#define LEADERBOARD_SIZE 4
#define EMPTY_SLOT 9999
#define COUNTDOWN_STEP_US 1000000

#define SCREEN_W 128
#define SCREEN_H 64
#define OLED_ADDR 0x3C

const int led_pins[NUM_LEDS] = { 23, 19, 18 };

enum GameState { STATE_IDLE,
                 STATE_COUNTDOWN,
                 STATE_HOLD,
                 STATE_GO,
                 STATE_RESULT,
                 STATE_FALSE };

// ---------- global variables ----------
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
Preferences prefs;

GameState state = STATE_IDLE;
int scores[LEADERBOARD_SIZE];
int64_t start = 0;
int64_t random_time = 0;
int prev_button = HIGH;

// ---------- screen helpers ----------
void show_text(const char* big, const char* small) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(big);
  display.setTextSize(1);
  display.println(small);
  display.display();
}

void draw_scores(int ms, int highlight) {  // Draws the scores on the physical screen
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.printf("TOP %d     YOU: %d ms", LEADERBOARD_SIZE, ms);

  display.setCursor(0, 16);
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (scores[i] == EMPTY_SLOT) {
      display.printf(" %d.  ---", i + 1);
    } else {
      display.printf(" %d.  %4d ms", i + 1, scores[i]);
      if (i == highlight) display.print("  < You");
    }
    display.println();
  }

  display.setCursor(0, 56);
  display.print("Click to play again");
  display.display();
}

// ---------- game helpers ----------
void leds_show(int count) {  // Used to turn on the LEDs
  for (int i = 0; i < NUM_LEDS; i++) {
    digitalWrite(led_pins[i], i < count ? HIGH : LOW);
  }
}

int add_score(int t) {  // Adds high scores to the leaderboard
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (t < scores[i]) {
      for (int j = LEADERBOARD_SIZE - 1; j > i; j--) {
        scores[j] = scores[j - 1];
      }
      scores[i] = t;
      prefs.putBytes("scores", scores, sizeof(scores));
      return i;
    }
  }
  return -1;
}

void print_scores(int highlight) {  // Prints the scores on the output screen
  Serial.printf("\n====== TOP %d TIMES ======\n", LEADERBOARD_SIZE);
  for (int i = 0; i < LEADERBOARD_SIZE; i++) {
    if (scores[i] == EMPTY_SLOT) {
      Serial.printf(" %d.  (empty)\n", i + 1);
    } else {
      Serial.printf(" %d.  %4d ms", i + 1, scores[i]);
      if (i == highlight) Serial.print("   < You");
      Serial.println();
    }
  }
  Serial.println("=========================\n");
}

void start_round(int64_t now) {  // Output for round start
  state = STATE_COUNTDOWN;
  start = now;
  Serial.println("COUNTDOWN");
  show_text("GET READY", "Wait for lights");
}

void false_start() {  // Output for false starts
  leds_show(0);
  Serial.println("False start! Disqualified!");
  Serial.println("Press the button to try again");
  show_text("FALSE", "Press to retry");
  state = STATE_FALSE;
}

// ---------- setup ----------
void setup() {
  for (int i = 0; i < LEADERBOARD_SIZE; i++) scores[i] = EMPTY_SLOT;
  prefs.begin("scores", false);
  prefs.getBytes("scores", scores, sizeof(scores));
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found");
    while (true) delay(10);
  }
  display.setTextColor(SSD1306_WHITE);

  for (int i = 0; i < NUM_LEDS; i++) pinMode(led_pins[i], OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);


  Serial.println("Press and release the button to start");
  show_text("REACTION", "Press to start");
}

// ---------- main loop ----------
void loop() {
  int button_state = digitalRead(BUTTON_PIN);
  bool pressed_event = (prev_button == HIGH && button_state == LOW);
  bool released_event = (prev_button == LOW && button_state == HIGH);
  int64_t now = esp_timer_get_time();

  switch (state) {
    case STATE_IDLE:
      leds_show(0);
      if (released_event) start_round(now);
      break;

    case STATE_COUNTDOWN:
      {
        if (pressed_event) {
          false_start();
          break;
        }
        int lit = (int)((now - start) / COUNTDOWN_STEP_US) + 1;
        if (lit < NUM_LEDS) {
          leds_show(lit);
        } else {
          leds_show(NUM_LEDS);
          start = now;
          random_time = (int64_t)random(1000, 4000) * 1000;
          state = STATE_HOLD;
        }
        break;
      }

    case STATE_HOLD:
      if (pressed_event) {
        false_start();
      } else if (now - start >= random_time) {
        leds_show(0);
        start = now;
        state = STATE_GO;
      }
      break;

    case STATE_GO:
      if (pressed_event) {
        int ms = (int)((now - start) / 1000);
        Serial.printf("Reaction time: %d ms\n", ms);
        int slot = add_score(ms);
        print_scores(slot);
        draw_scores(ms, slot);
        Serial.println("Press the button to play again");
        state = STATE_RESULT;
      }
      break;

    case STATE_RESULT:
    case STATE_FALSE:
      if (pressed_event) start_round(now);
      break;
  }

  prev_button = button_state;
  delay(1);
}
