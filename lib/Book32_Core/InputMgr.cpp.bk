#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"

InputMgr::InputMgr() : btn(PIN_BUTTON, true, true) { // Active Low, Pullup
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
    }

    InputAction action = INPUT_NONE;
    while (dequeueAction(action)) {
        if(callback) callback(action);
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);
    while (true) {
        self->btn.tick();
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
