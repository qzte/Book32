#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"

InputMgr::InputMgr() : btn(PIN_BUTTON, true, true), btnBack(PIN_BUTTON_BACK, true, true) { // Active Low, Pullup
    callback = nullptr;
}

InputMgr& InputMgr::getInstance() {
    static InputMgr instance;
    return instance;
}

void InputMgr::init() {
    // Configure timing FIRST - ULTRA SNAPPY SETTINGS
    btn.setDebounceMs(30);      // Reduced debounce (default 50ms)
    btn.setClickMs(100);        // Very short click window - no waiting for double-click
    btn.setPressMs(400);        // Faster long press detection (400ms)

    // Attach static handlers that trampoline to member functions
    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);
    // Double-click disabled for faster response

    // KEY1 - dedicated Back button - Use long press for going to main menu
    // Just set up debounce, don't attach handlers via OneButton
    // We'll poll it manually for long press detection
    btnBack.setDebounceMs(30);
    btnBack.setPressMs(400);

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
        Serial.printf("InputMgr::update() - dispatching action %d to callback\n", action);
        if(callback) callback(action);
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);
    while (true) {
        self->btn.tick();
        // btnBack.tick() removed - using manual polling instead
        
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
            } else if (!self->_btnBackLongPressSent && (now - self->_btnBackPressTime) >= 400) {
                // Long press threshold (400ms)
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
                
                // If long press wasn't already sent, and press was short, send INPUT_PREV (page up)
                if (!self->_btnBackLongPressSent && pressDuration < 400) {
                    Serial.println("INPUT: KEY1 Click -> PREV");
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_PREV);
                }
                
                self->_btnBackPressTime = 0;
                self->_btnBackLongPressSent = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
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
