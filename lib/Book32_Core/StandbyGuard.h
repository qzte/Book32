#pragma once
#include "ButtonPressLogic.h"

// Book32 v1.9.2 — quem pode mandar o dispositivo para standby.
//
// O standby manual está actualmente desligado por omissão
// (BOOK32_KEY2_STANDBY_ENABLED=0 em Config.h): só o idle timeout automático
// do BatteryMgr adormece o dispositivo, e o código abaixo fica compilado
// mas fora do caminho de InputMgr até o flag voltar a 1. Mantido tal como
// estava para esse caso.
//
// Regra do produto (com o standby manual ligado): só o long press de KEY2 entra em standby manualmente.
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
// v1.10.2: essa defesa só actua no instante em que o limiar é atingido. Se
// ainda for reportado standby a disparar com KEY3, ou é ruído demasiado
// breve para o outro botão aparecer premido nessa amostra única, ou é
// simplesmente fácil de cruzar sem querer: KEY1/KEY3 chegam ao limiar de
// long press (BUTTON_LONG_PRESS_MS, 400ms) com o mesmo gesto rápido que se
// usa em qualquer navegação. O standby, que é caro de reverter (~2s de
// e-ink, o utilizador tem de ir literalmente premir outro botão para
// acordar o leitor), passa a exigir um premir muito mais longo e deliberado
// — STANDBY_HOLD_MS — para que nem ruído breve nem um long press comum de
// navegação o atinjam por acidente.
//
// v1.10.3 (revertida em v1.10.4): a hipótese A (acoplamento eléctrico) foi
// descartada por log real — um long press limpo e sustentado só em GPIO3,
// sem nenhum outro pino a mexer. Concluiu-se, errada e apressadamente, que a
// fiação de KEY2/KEY3 estava trocada, e trocou-se PIN_BUTTON com
// PIN_BUTTON_SLEEP no Config.h. Essa troca partiu o KEY3 que já funcionava:
// o primeiríssimo log desta investigação (antes de qualquer alteração de
// pinos) já mostrava um clique genuíno em KEY3/GPIO5 a produzir INPUT_NEXT
// correctamente. O long press em GPIO3 sozinho não provava que o botão de
// virar página estivesse ligado a GPIO3 — só provava que, naquele instante,
// foi GPIO3 que esteve premido, o que é perfeitamente compatível com o
// utilizador ter premido sem querer o KEY2 físico (adjacente) a tentar
// chegar ao KEY3. Ver a secção "Revertido" em
// docs/plans/2026-07-26-key1-key3-standby-diagnostics.md para o historial
// completo. O mapeamento de pinos voltou ao original em v1.10.4; este guarda
// e o STANDBY_HOLD_MS ficam como defesa contra esse tipo de premir por
// engano, que continua a ser a explicação mais provável do sintoma.
//
// Header puro, sem dependências do Arduino — testável no host
// (tools/tests/test_standby_guard.cpp).

// Limiar de long press exigido especificamente para o standby, muito acima
// do BUTTON_LONG_PRESS_MS usado por KEY1/KEY3 para as suas próprias acções.
// Deliberadamente diferente: standby é uma acção difícil de reverter, as
// outras não são. Isto por si só já reduz a hipótese de um premir comum de
// KEY1/KEY3 (400ms) ser confundido com um pedido de standby, e dá uma
// margem maior para o guarda acima detectar outro botão em baixo antes de o
// premir terminar.
constexpr unsigned long STANDBY_HOLD_MS = 1500;

enum StandbyDecision {
    STANDBY_DENY_TOO_SHORT,   // ainda não chegou ao limiar de standby
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
    if (heldMs < STANDBY_HOLD_MS) return STANDBY_DENY_TOO_SHORT;
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
