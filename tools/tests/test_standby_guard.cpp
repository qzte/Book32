// Host test for the standby guard: which button holds may enter standby.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_standby_guard test_standby_guard.cpp && ./test_standby_guard
#include "StandbyGuard.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // 1. O caso legítimo: KEY2 sozinho, mantido além do limiar de standby.
    assert(classifyStandbyRequest(true, false, false, STANDBY_HOLD_MS) == STANDBY_ALLOW);
    assert(classifyStandbyRequest(true, false, false, 5000) == STANDBY_ALLOW);

    // 2. KEY3 premido ao mesmo tempo: é o sintoma relatado. Um LOW em GPIO3
    //    acompanhado de KEY3 em baixo é acoplamento, não uma ordem de standby.
    assert(classifyStandbyRequest(true, false, true, 5000) == STANDBY_DENY_OTHER_KEY);

    // 3. O mesmo para KEY1, e para os dois em simultâneo.
    assert(classifyStandbyRequest(true, true, false, 5000) == STANDBY_DENY_OTHER_KEY);
    assert(classifyStandbyRequest(true, true, true, 5000) == STANDBY_DENY_OTHER_KEY);

    // 4. O LOW já desapareceu no instante da decisão: foi transitório.
    assert(classifyStandbyRequest(false, false, false, 5000) == STANDBY_DENY_RELEASED);

    // 5. "Solto" ganha a "outro botão premido": ambas negam, mas a razão
    //    registada no log tem de identificar o que realmente aconteceu.
    assert(classifyStandbyRequest(false, false, true, 5000) == STANDBY_DENY_RELEASED);

    // 6. Ainda não chegou ao limiar de standby.
    assert(classifyStandbyRequest(true, false, false, STANDBY_HOLD_MS - 1) ==
           STANDBY_DENY_TOO_SHORT);
    assert(classifyStandbyRequest(true, false, false, 0) == STANDBY_DENY_TOO_SHORT);

    // 7. Um clique curto de KEY2 nunca pode adormecer o dispositivo, mesmo
    //    sozinho: o standby fica no long press de propósito, para que um roçar
    //    no botão não deixe cair o leitor em deep sleep a meio da página.
    assert(classifyStandbyRequest(true, false, false, BUTTON_DEBOUNCE_MIN_MS) ==
           STANDBY_DENY_TOO_SHORT);

    // 8. v1.10.2: um long press comum de KEY1/KEY3 (BUTTON_LONG_PRESS_MS,
    //    400ms) sozinho não chega para o standby, que exige STANDBY_HOLD_MS.
    //    Antes desta versão os dois limiares eram o mesmo valor, por isso um
    //    gesto de navegação normal chegava exactamente ao ponto em que
    //    qualquer ruído breve em GPIO3 tinha margem para passar despercebido.
    assert(STANDBY_HOLD_MS > BUTTON_LONG_PRESS_MS);
    assert(classifyStandbyRequest(true, false, false, BUTTON_LONG_PRESS_MS) ==
           STANDBY_DENY_TOO_SHORT);

    // 9. Nomes das decisões, que vão para o log de diagnóstico.
    assert(strcmp(standbyDecisionName(STANDBY_ALLOW), "allow") == 0);
    assert(strcmp(standbyDecisionName(STANDBY_DENY_OTHER_KEY), "other_key_held") == 0);

    printf("All 9 tests passed.\n");
    return 0;
}
