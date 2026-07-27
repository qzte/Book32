#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"
#include "AppMgr.h"
#include "ButtonPressLogic.h"

InputMgr::InputMgr() : btn(PIN_BUTTON, true, true), btnBack(PIN_BUTTON_BACK, true, true),
                       btnSleep(PIN_BUTTON_SLEEP, true, true) { // Active Low, Pullup
    callback = nullptr;
}

InputMgr& InputMgr::getInstance() {
    static InputMgr instance;
    return instance;
}

void InputMgr::init() {
    // Configure timing FIRST - ULTRA SNAPPY SETTINGS.
    // KEY3 is the one button actually driven by OneButton::tick(), so these
    // settings take effect here. KEY1 and KEY2 are polled with digitalRead()
    // and get their timing from ButtonPressLogic.h instead.
    btn.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btn.setClickMs(100);        // Very short click window - no waiting for double-click
    btn.setPressMs(BUTTON_LONG_PRESS_MS);

    // Attach static handlers that trampoline to member functions
    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);
    // Double-click disabled for faster response

    // KEY1 - dedicated Back button - Use long press for going to main menu.
    // No handlers attached and tick() is never called on it: the polling task
    // reads the pin directly, so these setters are inert. They are kept only so
    // the object is left in a consistent state if tick() is ever restored.
    // Real debounce for KEY1 lives in classifyButtonRelease().
    btnBack.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btnBack.setPressMs(BUTTON_LONG_PRESS_MS);

    // KEY2 - short click triggers a manual full display refresh; long press
    // enters standby. Standby stays on the long press so a brush against the
    // button never drops the device into deep sleep mid-page. Polled manually
    // for the same reason as KEY1.
    btnSleep.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btnSleep.setPressMs(BUTTON_LONG_PRESS_MS);
    pinMode(PIN_BUTTON_SLEEP, INPUT_PULLUP);

    if (!_taskHandle) {
        BaseType_t result = xTaskCreatePinnedToCore(
            inputTask,
            "InputPoll",
            3072,
            this,
            2,
            &_taskHandle,
            1
        );
        _taskRunning = (result == pdPASS);
        if (!_taskRunning) {
            Serial.println("Input task failed to start; falling back to loop polling");
            _taskHandle = nullptr;
        }
    }
}

void InputMgr::update() {
    if (!_taskRunning) {
        btn.tick();
        // Don't tick btnBack - using manual polling in inputTask
    }

    InputAction action = INPUT_NONE;
    while (dequeueAction(action)) {
        // A manual full refresh is a display concern, not an app command, so
        // it's consumed here instead of being dispatched. Every screen gets it
        // for free that way, including modals that would drop an unknown
        // action on the floor. forceRedraw() only sets the app's dirty flags;
        // the repaint itself happens in the next AppMgr::draw(), which keeps
        // the ~2s e-ink refresh off this code path.
        if (action == INPUT_REFRESH) {
            Serial.println("INPUT: KEY2 Click -> FULL REFRESH");
            App* current = AppMgr::getInstance().getCurrentApp();
            if (current) current->forceRedraw();
            continue;
        }
        Serial.printf("InputMgr::update() - dispatching action %d to callback\n", action);
        if(callback) callback(action);
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);
    while (true) {
        self->btn.tick();
        // btnBack.tick() removed - using manual polling instead

        // v1.9.1 diagnostics (PINDIAG). A user reported KEY1 and KEY3 entering
        // standby, but no code path in this file connects those buttons to
        // sleep: standby is only reached from the KEY2 long press below, and
        // idle sleep only from BatteryMgr's timeout. Before changing any
        // behaviour we need to see what the pins actually read, so log a raw
        // snapshot of all three on every edge. 1 = released (pull-up),
        // 0 = pressed (active low).
        {
            uint8_t snapshot = (uint8_t)((digitalRead(PIN_BUTTON_BACK)  ? 0x01 : 0) |
                                         (digitalRead(PIN_BUTTON_SLEEP) ? 0x02 : 0) |
                                         (digitalRead(PIN_BUTTON)       ? 0x04 : 0));
            if (snapshot != self->_lastPinSnapshot) {
                self->_lastPinSnapshot = snapshot;
                Serial.printf("PINDIAG: KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  KEY3/GPIO%d=%d\n",
                              PIN_BUTTON_BACK,  (snapshot & 0x01) ? 1 : 0,
                              PIN_BUTTON_SLEEP, (snapshot & 0x02) ? 1 : 0,
                              PIN_BUTTON,       (snapshot & 0x04) ? 1 : 0);
            }
        }

        
        // Manual KEY1 long press detection (PIN_BUTTON_BACK)
        // Read the button state directly
        int btnState = digitalRead(PIN_BUTTON_BACK);
        bool btnPressed = (btnState == LOW);  // Active low
        unsigned long now = millis();
        
        if (btnPressed) {
            // Button is pressed
            if (self->_btnBackPressTime == 0) {
                // Just pressed
                self->_btnBackPressTime = now;
                self->_btnBackLongPressSent = false;
                Serial.println("KEY1: Button pressed");
            } else if (!self->_btnBackLongPressSent &&
                       (now - self->_btnBackPressTime) >= BUTTON_LONG_PRESS_MS) {
                // Long press threshold
                Serial.println("INPUT: KEY1 Long Press -> GO TO MAIN MENU");
                BatteryMgr::getInstance().resetIdleTimer();
                self->enqueueAction(INPUT_GO_TO_MAIN_MENU);
                self->_btnBackLongPressSent = true;
            }
        } else {
            // Button released
            if (self->_btnBackPressTime != 0) {
                unsigned long pressDuration = now - self->_btnBackPressTime;
                Serial.printf("KEY1: Button released after %lu ms\n", pressDuration);

                // Shared classifier: rejects contact bounce below the debounce
                // floor, and releases where the long press already fired.
                // Without the floor a rebound sent a second INPUT_PREV and the
                // reader went back two pages on one press.
                if (classifyButtonRelease(pressDuration, self->_btnBackLongPressSent) ==
                    BUTTON_RELEASE_CLICK) {
                    Serial.println("INPUT: KEY1 Click -> PREV");
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_PREV);
                }
                
                self->_btnBackPressTime = 0;
                self->_btnBackLongPressSent = false;
            }
        }
        
        // Manual KEY2 detection (PIN_BUTTON_SLEEP): short click -> full
        // refresh, long press -> standby. Standby is handled inline rather
        // than dispatched through the callback so it works in every app and on
        // modal screens like the unsaved-changes prompt.
        int sleepState = digitalRead(PIN_BUTTON_SLEEP);
        bool sleepPressed = (sleepState == LOW);  // Active low

        if (sleepPressed) {
            if (self->_btnSleepPressTime == 0) {
                self->_btnSleepPressTime = now;
                self->_btnSleepLongPressSent = false;
                Serial.println("KEY2: Button pressed");
            } else if (!self->_btnSleepLongPressSent &&
                       (now - self->_btnSleepPressTime) >= BUTTON_LONG_PRESS_MS) {
                Serial.println("INPUT: KEY2 Long Press -> STANDBY");
                self->_btnSleepLongPressSent = true;
                self->enterStandby();  // Does not return: deep sleep
            }
        } else {
            if (self->_btnSleepPressTime != 0) {
                unsigned long pressDuration = now - self->_btnSleepPressTime;

                // Same shared classifier as KEY1. The debounce floor matters
                // here because contact bounce would otherwise cost a ~2s full
                // refresh.
                //
                // The long-press branch of the classifier is unreachable for
                // KEY2 today because enterStandby() never returns, but it's
                // kept so that making standby cancellable later can't produce
                // a stray refresh on button release.
                if (classifyButtonRelease(pressDuration, self->_btnSleepLongPressSent) ==
                    BUTTON_RELEASE_CLICK) {
                    Serial.printf("KEY2: Button released after %lu ms -> REFRESH\n", pressDuration);
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_REFRESH);
                }

                self->_btnSleepPressTime = 0;
                self->_btnSleepLongPressSent = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void InputMgr::enterStandby() {
    // v1.9.1 diagnostics: record which pins were actually held at the moment
    // standby was decided. If KEY2/GPIO3 reads 1 (released) here, the LOW that
    // triggered the long press was transient or came from another pin.
    Serial.printf("SLEEPDIAG: path=KEY2_LONG_PRESS  KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  KEY3/GPIO%d=%d\n",
                  PIN_BUTTON_BACK,  digitalRead(PIN_BUTTON_BACK),
                  PIN_BUTTON_SLEEP, digitalRead(PIN_BUTTON_SLEEP),
                  PIN_BUTTON,       digitalRead(PIN_BUTTON));
    Serial.flush();

    // Give the active app a chance to persist state first. The reader already
    // saves progress on stop(); the settings menu would otherwise lose an
    // unsaved draft to the deep sleep reset.
    // stop() is the app's own save hook: the reader persists reading progress
    // and the settings menu flushes an unsaved draft. Calling it through the
    // base interface keeps InputMgr free of any app-specific dependency.
    App* current = AppMgr::getInstance().getCurrentApp();
    if (current) current->stop();

    // Reuses the existing idle-sleep path: e-ink message, ext0 wake on KEY3,
    // then deep sleep. Wake still happens on KEY3 because ext0 supports a
    // single pin; adding KEY2 would require switching to ext1 with a pin mask.
    BatteryMgr::getInstance().enterIdleSleep("key2_long_press");
}

void InputMgr::enqueueAction(InputAction action) {
    if (action == INPUT_NONE) return;

    portENTER_CRITICAL(&_queueMux);
    uint8_t nextHead = (_queueHead + 1) % QUEUE_SIZE;
    if (nextHead != _queueTail) {
        _queue[_queueHead] = action;
        _queueHead = nextHead;
    }
    portEXIT_CRITICAL(&_queueMux);
}

bool InputMgr::dequeueAction(InputAction& action) {
    bool hasAction = false;
    portENTER_CRITICAL(&_queueMux);
    if (_queueTail != _queueHead) {
        action = _queue[_queueTail];
        _queueTail = (_queueTail + 1) % QUEUE_SIZE;
        hasAction = true;
    }
    portEXIT_CRITICAL(&_queueMux);
    return hasAction;
}

// Trampolines
void InputMgr::staticClick(void *ptr) {
    if(ptr) static_cast<InputMgr*>(ptr)->onClick();
}
void InputMgr::staticDoubleClick(void *ptr) {
    if(ptr) static_cast<InputMgr*>(ptr)->onDoubleClick();
}
void InputMgr::staticLongPress(void *ptr) {
    if(ptr) static_cast<InputMgr*>(ptr)->onLongPress();
}

// Handlers -> Dispatch to App
void InputMgr::onClick() {
    Serial.println("INPUT: Click -> NEXT");
    BatteryMgr::getInstance().resetIdleTimer();  // Reset idle timer on user interaction
    enqueueAction(INPUT_NEXT);
}

void InputMgr::onDoubleClick() {
    // Disabled for faster single-click response
    Serial.println("INPUT: Double-Click -> PREV");
    BatteryMgr::getInstance().resetIdleTimer();  // Reset idle timer on user interaction
    enqueueAction(INPUT_PREV);
}

void InputMgr::onLongPress() {
    Serial.println("INPUT: Long Press -> SELECT");
    BatteryMgr::getInstance().resetIdleTimer();  // Reset idle timer on user interaction
    enqueueAction(INPUT_SELECT);
}
