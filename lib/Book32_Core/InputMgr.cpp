#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"
#include "AppMgr.h"
#include "ButtonPressLogic.h"
#include "StandbyGuard.h"

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
    // Standby pedido pela tarefa de input. Tratado primeiro: se houver acções
    // em fila, adormecer é melhor do que virar mais uma página (dois segundos
    // de refresh) antes de o fazer.
    if (_standbyRequested) {
        _standbyRequested = false;
        enterStandby();  // não regressa: deep sleep
    }

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

        // Estado cru dos três botões, lido uma vez por ciclo e partilhado pelo
        // diagnóstico e pelos blocos de detecção abaixo. KEY3 entra aqui porque
        // o guarda de standby precisa de saber se está premido: é lido em bruto
        // e não através do OneButton, que só reporta eventos já classificados.
        bool key1Pressed = (digitalRead(PIN_BUTTON_BACK)  == LOW);  // Active low
        bool key2Pressed = (digitalRead(PIN_BUTTON_SLEEP) == LOW);
        bool key3Pressed = (digitalRead(PIN_BUTTON)       == LOW);
        unsigned long now = millis();

        // v1.9.1 diagnostics (PINDIAG): snapshot cru dos três pinos em cada
        // transição. 1 = solto (pull-up), 0 = premido (activo a baixo).
        // Mantém-se depois da correcção: é o que mostra se um premir de KEY3
        // arrasta GPIO3 com ele, que é a hipótese que o guarda de standby
        // defende.
        {
            uint8_t snapshot = (uint8_t)((key1Pressed ? 0 : 0x01) |
                                         (key2Pressed ? 0 : 0x02) |
                                         (key3Pressed ? 0 : 0x04));
            if (snapshot != self->_lastPinSnapshot) {
                self->_lastPinSnapshot = snapshot;
                Serial.printf("PINDIAG: KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  KEY3/GPIO%d=%d\n",
                              PIN_BUTTON_BACK,  (snapshot & 0x01) ? 1 : 0,
                              PIN_BUTTON_SLEEP, (snapshot & 0x02) ? 1 : 0,
                              PIN_BUTTON,       (snapshot & 0x04) ? 1 : 0);
            }
        }


        // Qualquer botão em baixo é actividade do utilizador. O reset do
        // temporizador de inactividade vivia só nos eventos já classificados,
        // por isso um KEY3 mantido premido (ou premires que o OneButton ainda
        // não fechou) não contava como actividade e o timeout podia disparar
        // durante o uso — indistinguível, para quem está a ler, de "o KEY3
        // mandou o leitor dormir".
        //
        // Limitado a uma vez por IDLE_RESET_THROTTLE_MS: resetIdleTimer() pega
        // no mutex do BatteryMgr, que a leitura do ADC segura dezenas de ms, e
        // não vale a pena arriscar bloquear a amostragem dos botões a cada 5ms.
        if ((key1Pressed || key2Pressed || key3Pressed) &&
            (self->_lastIdleResetTime == 0 ||
             (now - self->_lastIdleResetTime) >= IDLE_RESET_THROTTLE_MS)) {
            self->_lastIdleResetTime = now;
            BatteryMgr::getInstance().resetIdleTimer();
        }

        // Manual KEY1 long press detection (PIN_BUTTON_BACK)
        bool btnPressed = key1Pressed;

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
        // refresh, long press -> standby. Standby é consumido pelo InputMgr no
        // loop principal, e não despachado pelo callback, para funcionar em
        // todos os apps e em ecrãs modais como o aviso de alterações por
        // guardar.
        bool sleepPressed = key2Pressed;

        if (sleepPressed) {
            if (self->_btnSleepPressTime == 0) {
                self->_btnSleepPressTime = now;
                self->_btnSleepLongPressSent = false;
                self->_btnSleepAborted = false;
                Serial.println("KEY2: Button pressed");
            } else if (!self->_btnSleepLongPressSent && !self->_btnSleepAborted &&
                       (now - self->_btnSleepPressTime) >= BUTTON_LONG_PRESS_MS) {
                // Guarda de standby: relê os três pinos e só aceita o pedido se
                // KEY2 estiver mesmo premido e mais nenhum botão estiver em
                // baixo. Sem isto, um LOW induzido em GPIO3 por premir KEY3
                // valia um standby (ver StandbyGuard.h).
                StandbyDecision decision = classifyStandbyRequest(
                    digitalRead(PIN_BUTTON_SLEEP) == LOW,
                    digitalRead(PIN_BUTTON_BACK)  == LOW,
                    digitalRead(PIN_BUTTON)       == LOW,
                    now - self->_btnSleepPressTime);

                if (decision == STANDBY_ALLOW) {
                    Serial.println("INPUT: KEY2 Long Press -> STANDBY requested");
                    self->_btnSleepLongPressSent = true;
                    // Só marca: quem adormece é o loop principal, em update().
                    // Aqui não se pode desenhar no e-ink (ver enterStandby).
                    self->_standbyRequested = true;
                } else {
                    // Recusa definitiva para este premir: sem o travão, o ciclo
                    // seguinte voltaria a testar e um único instante com os
                    // outros botões soltos deixava passar o standby espúrio.
                    // O premir só volta a contar depois de KEY2 ser largado.
                    self->_btnSleepAborted = true;
                    Serial.printf("SLEEPDIAG: standby denied  reason=%s  held=%lums\n",
                                  standbyDecisionName(decision),
                                  now - self->_btnSleepPressTime);
                }
            }
        } else {
            if (self->_btnSleepPressTime != 0) {
                unsigned long pressDuration = now - self->_btnSleepPressTime;

                // Same shared classifier as KEY1. The debounce floor matters
                // here because contact bounce would otherwise cost a ~2s full
                // refresh.
                //
                // O ramo de long press do classificador passou a ser
                // alcançável: o standby já não corre aqui, por isso o
                // largar do botão chega a esta linha antes de o loop
                // principal adormecer. Sem ele, soltar o botão depois de um
                // long press custava um refresh completo de ~2 s.
                //
                // Um premir recusado pelo guarda conta como consumido: se o
                // LOW em GPIO3 veio de outro botão, também não é um pedido de
                // refresh, e um refresh completo de ~2 s é caro demais para se
                // dar a ruído.
                if (classifyButtonRelease(pressDuration,
                                          self->_btnSleepLongPressSent || self->_btnSleepAborted) ==
                    BUTTON_RELEASE_CLICK) {
                    Serial.printf("KEY2: Button released after %lu ms -> REFRESH\n", pressDuration);
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_REFRESH);
                }

                self->_btnSleepPressTime = 0;
                self->_btnSleepLongPressSent = false;
                self->_btnSleepAborted = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Corre no loop principal (ver update()), nunca na tarefa de input.
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
