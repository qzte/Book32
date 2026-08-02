#pragma once
#include "ButtonPressLogic.h"

// Book32 v1.9.2 — quem pode mandar o dispositivo para standby.
//
// Regra do produto: só o long press de KEY2 entra em standby manualmente.
// KEY1 e KEY3 não têm caminho para o sleep no código, mas o utilizador via
// KEY3 a adormecer o leitor. A hipótese A do diagnóstico
// (docs/plans/2026-07-26-key1-key3-standby-diagnostics.md) é acoplamento nos
// pinos: premir KEY3/GPIO5 arrasta GPIO3 para LOW o tempo suficiente para a
// detecção de long press de KEY2 disparar.
//
// A defesa é exigir que, no instante da decisão, KEY2 continue premido e que
// mais nenhum botão esteja em baixo. Um LOW em GPIO3 acompanhado de outro
// botão premido é ruído, não é uma ordem de standby: o utilizador que quer
// standby carrega em KEY2 sozinho.
//
// Header puro, sem dependências do Arduino — testável no host
// (tools/tests/test_standby_guard.cpp).

enum StandbyDecision {
    STANDBY_DENY_TOO_SHORT,   // ainda não chegou ao limiar de long press
    STANDBY_DENY_RELEASED,    // o LOW desapareceu: foi transitório
    STANDBY_DENY_OTHER_KEY,   // outro botão está premido -> acoplamento
    STANDBY_ALLOW             // long press de KEY2 genuíno
};

// key2Held/key1Held/key3Held: estado dos pinos relido no instante da decisão
// (true = premido, ou seja o pino lê LOW por serem activos a baixo).
// heldMs: tempo desde o início do LOW contínuo em KEY2.
inline StandbyDecision classifyStandbyRequest(bool key2Held,
                                              bool key1Held,
                                              bool key3Held,
                                              unsigned long heldMs) {
    if (!key2Held) return STANDBY_DENY_RELEASED;
    if (key1Held || key3Held) return STANDBY_DENY_OTHER_KEY;
    if (heldMs < BUTTON_LONG_PRESS_MS) return STANDBY_DENY_TOO_SHORT;
    return STANDBY_ALLOW;
}

inline const char* standbyDecisionName(StandbyDecision decision) {
    switch (decision) {
        case STANDBY_DENY_TOO_SHORT: return "too_short";
        case STANDBY_DENY_RELEASED:  return "released";
        case STANDBY_DENY_OTHER_KEY: return "other_key_held";
        case STANDBY_ALLOW:          return "allow";
    }
    return "unknown";
}
