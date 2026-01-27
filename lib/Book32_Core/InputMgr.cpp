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
    // Attach static handlers that trampoline to member functions
    btn.attachClick(staticClick, this);
    btn.attachDoubleClick(staticDoubleClick, this);
    btn.attachLongPressStart(staticLongPress, this);
    
    // Configure timing - SNAPPY SETTINGS
    btn.setClickMs(180);        // Reduced from 300ms for faster response
    btn.setPressMs(600);        // Reduced from 800ms for faster select
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
    // Single press: Next / Turn page / Navigate down
    if(callback) callback(INPUT_NEXT);
}

void InputMgr::onDoubleClick() {
    // Not used currently
    if(callback) callback(INPUT_PREV);
}

void InputMgr::onLongPress() {
    // Long press: Select / Open app / Open book
    if(callback) callback(INPUT_SELECT);
}
