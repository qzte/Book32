#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
    TaskHandle_t _taskHandle = nullptr;
    bool _taskRunning = false;

    static const uint8_t QUEUE_SIZE = 8;
    volatile uint8_t _queueHead = 0;
    volatile uint8_t _queueTail = 0;
    InputAction _queue[QUEUE_SIZE];
    portMUX_TYPE _queueMux = portMUX_INITIALIZER_UNLOCKED;

    void enqueueAction(InputAction action);
    bool dequeueAction(InputAction& action);
    static void inputTask(void* parameter);
    
    // We need to route static handlers to instance
    void onClick();
    void onDoubleClick();
    void onLongPress();
    
    static void staticClick(void *ptr);
    static void staticDoubleClick(void *ptr);
    static void staticLongPress(void *ptr);
};
