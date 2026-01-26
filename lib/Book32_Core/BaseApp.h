#pragma once
#include <Arduino.h>

class App {
public:
    virtual ~App() {}
    
    // Called when the app is first loaded/switched to
    virtual void start() {}
    
    // Called every loop iteration
    virtual void update() {}
    
    // Called when screen updates needed
    virtual void draw() {}
    
    // Called when app is being switched away from
    virtual void stop() {}
    
    // Metadata
    virtual const char* getName() = 0;
    // Returns font icon char (if using font) or empty
    virtual const char* getIcon() { return ""; } 
    // Returns bitmap icon (if using bitmap) or nullptr
    virtual const uint8_t* getIconImage() { return nullptr; }
};
