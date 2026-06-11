#include "btn_input.h"

#include "DEV_Config.h"

#include <Arduino.h>

#define BTN_DEBOUNCE_MS   25U
#define BTN_DOUBLE_MS     400U
#define BTN_LONG_MS       700U

struct ButtonTracker {
  bool stableDown;
  bool lastRawDown;
  uint32_t lastChangeMs;
  uint32_t pressStartMs;
  bool longFired;
  uint8_t clickCount;
  uint32_t lastReleaseMs;
  BtnAction pendingAction;
};

static ButtonTracker s_btnA;
static ButtonTracker s_btnB;
static BtnAction s_serialPending = BTN_ACTION_NONE;

static void print_serial_help(void) {
  Serial.println("[BtnSim] Serial keys (115200, line ending Any):");
  Serial.println("  n = A click (next)   p = A double (prev)   b = A long (back)");
  Serial.println("  c = B click (confirm)   v = B double (voice)   w = B long (wallpaper)   h = help");
}

static bool readRawDown(BtnId id) {
  const int pin = (id == BTN_ID_A) ? BTN_A_PIN : BTN_B_PIN;
  return digitalRead(pin) == LOW;
}

static BtnAction actionFor(BtnId id, uint8_t clickCount, bool isLong) {
  if (isLong) {
    if (id == BTN_ID_A) {
      return BTN_ACTION_BACK;
    }
    if (id == BTN_ID_B) {
      return BTN_ACTION_WALLPAPER_TOGGLE;
    }
    return BTN_ACTION_NONE;
  }

  if (clickCount >= 2) {
    return (id == BTN_ID_A) ? BTN_ACTION_PREV : BTN_ACTION_VOICE_TOGGLE;
  }
  if (clickCount == 1) {
    return (id == BTN_ID_A) ? BTN_ACTION_NEXT : BTN_ACTION_CONFIRM;
  }
  return BTN_ACTION_NONE;
}

static void queueAction(ButtonTracker *btn, BtnAction action) {
  if (action != BTN_ACTION_NONE) {
    btn->pendingAction = action;
  }
}

static void finalizeClicks(ButtonTracker *btn, BtnId id) {
  if (btn->clickCount == 0) {
    return;
  }
  queueAction(btn, actionFor(id, btn->clickCount, false));
  btn->clickCount = 0;
}

static void updateButton(ButtonTracker *btn, BtnId id, uint32_t now) {
  const bool rawDown = readRawDown(id);
  if (rawDown != btn->lastRawDown) {
    btn->lastChangeMs = now;
    btn->lastRawDown = rawDown;
  }

  if ((now - btn->lastChangeMs) < BTN_DEBOUNCE_MS) {
    return;
  }

  if (rawDown != btn->stableDown) {
    btn->stableDown = rawDown;
    if (btn->stableDown) {
      btn->pressStartMs = now;
      btn->longFired = false;
    } else {
      if (btn->longFired) {
        btn->clickCount = 0;
      } else {
        btn->clickCount++;
        btn->lastReleaseMs = now;
      }
    }
  }

  if (btn->stableDown && !btn->longFired && (now - btn->pressStartMs) >= BTN_LONG_MS) {
    btn->longFired = true;
    btn->clickCount = 0;
    queueAction(btn, actionFor(id, 0, true));
  }

  if (!btn->stableDown && btn->clickCount > 0 && (now - btn->lastReleaseMs) >= BTN_DOUBLE_MS) {
    finalizeClicks(btn, id);
  }
}

#if BTN_SERIAL_SIM
static void queue_serial_action(BtnAction action) {
  s_serialPending = action;
  Serial.printf("[BtnSim] queued action=%d\n", (int)action);
}

static BtnAction action_from_serial_char(char ch) {
  switch (ch) {
    case 'n':
    case 'N':
      return BTN_ACTION_NEXT;
    case 'p':
    case 'P':
      return BTN_ACTION_PREV;
    case 'b':
    case 'B':
      return BTN_ACTION_BACK;
    case 'c':
    case 'C':
      return BTN_ACTION_CONFIRM;
    case 'v':
    case 'V':
      return BTN_ACTION_VOICE_TOGGLE;
    case 'w':
    case 'W':
      return BTN_ACTION_WALLPAPER_TOGGLE;
    default:
      return BTN_ACTION_NONE;
  }
}
#endif

void btn_input_init(void) {
  s_btnA = {};
  s_btnB = {};
  s_serialPending = BTN_ACTION_NONE;
  s_btnA.lastRawDown = readRawDown(BTN_ID_A);
  s_btnA.stableDown = s_btnA.lastRawDown;
  s_btnB.lastRawDown = readRawDown(BTN_ID_B);
  s_btnB.stableDown = s_btnB.lastRawDown;

#if BTN_SERIAL_SIM
  print_serial_help();
#endif
}

void btn_input_update(void) {
  const uint32_t now = millis();
  updateButton(&s_btnA, BTN_ID_A, now);
  updateButton(&s_btnB, BTN_ID_B, now);
}

void btn_input_serial_poll(void) {
#if BTN_SERIAL_SIM
  while (Serial.available() > 0) {
    const char ch = (char)Serial.read();
    if (ch == 'h' || ch == 'H' || ch == '?') {
      print_serial_help();
      continue;
    }
    const BtnAction action = action_from_serial_char(ch);
    if (action != BTN_ACTION_NONE) {
      queue_serial_action(action);
    }
  }
#else
  (void)0;
#endif
}

bool btn_input_consume(BtnAction *outAction) {
  if (s_serialPending != BTN_ACTION_NONE) {
    *outAction = s_serialPending;
    s_serialPending = BTN_ACTION_NONE;
    return true;
  }
  if (s_btnA.pendingAction != BTN_ACTION_NONE) {
    *outAction = s_btnA.pendingAction;
    s_btnA.pendingAction = BTN_ACTION_NONE;
    return true;
  }
  if (s_btnB.pendingAction != BTN_ACTION_NONE) {
    *outAction = s_btnB.pendingAction;
    s_btnB.pendingAction = BTN_ACTION_NONE;
    return true;
  }
  return false;
}
