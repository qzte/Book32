#include "InputMgr.h"
#include "../../include/Config.h"

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
}

void InputMgr::update() {
    btn.tick();
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
    if(callback) callback(INPUT_NEXT);
}

void InputMgr::onDoubleClick() {
    // Disabled for faster single-click response
    Serial.println("INPUT: Double-Click -> PREV");
    if(callback) callback(INPUT_PREV);
}

void InputMgr::onLongPress() {
    Serial.println("INPUT: Long Press -> SELECT");
    if(callback) callback(INPUT_SELECT);
}
