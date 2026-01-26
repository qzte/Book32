#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include "Config.h"

enum InputAction {
    INPUT_NONE,
    INPUT_NEXT,
    INPUT_PREV,
    INPUT_SELECT,
    INPUT_BACK
};

class InputMgr {
public:
    static InputMgr& getInstance();
    
    void init();
    void update();
    
    // Allow apps to register a callback function
    using InputCallback = std::function<void(InputAction)>;
    void setCallback(InputCallback cb) { callback = cb; }
    void clearCallback() { callback = nullptr; }
    
private:
    InputMgr();
    OneButton btn;
    InputCallback callback;
    
    // We need to route static handlers to instance
    void onClick();
    void onDoubleClick();
    void onLongPress();
    
    static void staticClick(void *ptr);
    static void staticDoubleClick(void *ptr);
    static void staticLongPress(void *ptr);
};
