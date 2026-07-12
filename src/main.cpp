// DIAGNOSTIC SKETCH - identifica em qual GPIO o botao KEY1 esta realmente ligado.
// Substitua TEMPORARIAMENTE o conteudo de src/main.cpp por este arquivo,
// compile e faca upload. Abra o monitor serial (115200) e aperte o KEY1
// repetidamente. O pino que mudar de HIGH para LOW quando voce aperta
// o botao e o pino correto.
//
// NAO deixe isso no lugar do main.cpp definitivo - e so para teste.

#include <Arduino.h>

// Pinos candidatos: os que o Book32 NAO usa para nada (display, bateria, etc.)
// Baseado no include/Config.h atual.
const int candidatePins[] = {2, 3, 8, 43, 21, 44, 17, 18};
const int numPins = sizeof(candidatePins) / sizeof(candidatePins[0]);
int lastState[numPins];

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println();
    Serial.println("=== Scanner de pino do KEY1 ===");
    Serial.println("Aperte o botao KEY1 repetidamente.");
    Serial.println("O pino que aparecer como -> LOW e o correto.");
    Serial.println();

    for (int i = 0; i < numPins; i++) {
        pinMode(candidatePins[i], INPUT_PULLUP);
        lastState[i] = digitalRead(candidatePins[i]);
    }
}

void loop() {
    for (int i = 0; i < numPins; i++) {
        int state = digitalRead(candidatePins[i]);
        if (state != lastState[i]) {
            Serial.printf("GPIO%-3d mudou -> %s\n", candidatePins[i], state == LOW ? "LOW (pressionado!)" : "HIGH (solto)");
            lastState[i] = state;
        }
    }
    delay(20);
}
