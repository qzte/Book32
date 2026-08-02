# KEY1/KEY3 a entrar em standby — instrumentação de diagnóstico (v1.9.1)

## Sintoma relatado
Premir KEY1 ou KEY3 faz o dispositivo entrar em idle sleep / standby.
Comportamento pretendido: só o long press de KEY2 entra em standby.

## Auditoria de código (v1.9.0)
Existem exactamente dois caminhos que chegam ao deep sleep:

1. `BatteryMgr::update()` → timeout de inactividade → `enterIdleSleep()`
2. `InputMgr::inputTask()` → long press de KEY2 (`PIN_BUTTON_SLEEP`) →
   `InputMgr::enterStandby()` → `enterIdleSleep()`

Nenhum outro sítio chama `enterIdleSleep()` / `enterStandby()`, e `INPUT_SLEEP`
está declarado mas nunca é enfileirado nem tratado por nenhuma app.

KEY1 e KEY3 fazem o oposto: ambos chamam `resetIdleTimer()` em cada evento.
- KEY1 (`PIN_BUTTON_BACK`, GPIO2): click → `INPUT_PREV`; long press → `INPUT_GO_TO_MAIN_MENU`
- KEY3 (`PIN_BUTTON`, GPIO5, via OneButton): click → `INPUT_NEXT`; long press → `INPUT_SELECT`

Conclusão: **o sintoma não é explicável pelo código fonte tal como está.**
Portanto não se altera comportamento antes de haver evidência.

## Mapeamento de pinos (verificado)
A wiki oficial do TRMNL 7.5" (OG) DIY Kit indica três botões de utilizador em
D1, D2 e D4 do XIAO ESP32-S3 → GPIO2, GPIO3, GPIO5. Coincide com `Config.h`.
O que **não** está verificado é qual serigrafia (KEY1/KEY2/KEY3) corresponde a
qual GPIO. Note-se que GPIO3 é o botão do meio em qualquer das duas ordens
possíveis, pelo que uma simples inversão de etiquetas não explica o sintoma.

## Hipóteses em aberto
| # | Hipótese | Como a evidência a distingue |
|---|----------|------------------------------|
| A | GPIO3 lê LOW quando se premem os outros botões (acoplamento mecânico/eléctrico) | `PINDIAG` mostra KEY2/GPIO3=0 ao premir KEY1 ou KEY3 |
| B | Etiquetas físicas não correspondem aos GPIO assumidos | `PINDIAG` mostra que o botão premido acende um GPIO diferente do esperado |
| C | É o timeout de inactividade e a coincidência com a tecla é aparente | `SLEEPDIAG: path=IDLE_TIMEOUT` |
| D | Reset/brownout ao premir (não é sleep) | não aparece nenhuma linha `SLEEPDIAG` |

## O que esta versão faz
Apenas instrumentação. Zero alterações de comportamento.

- `PINDIAG:` — snapshot cru dos três pinos em cada transição (1 = solto, 0 = premido)
- `SLEEPDIAG: path=KEY2_LONG_PRESS` — com o estado dos três pinos no instante da decisão
- `SLEEPDIAG: path=IDLE_TIMEOUT` — com `idle` e `timeout` em ms
- `SLEEPDIAG: enterIdleSleep() reached` — funil comum aos dois caminhos

## Procedimento de recolha
1. Flash + monitor série a 115200.
2. Premir e largar cada botão isoladamente (curto e longo), anotando qual se premiu.
3. Reproduzir o sintoma e capturar as últimas ~30 linhas.

A linha `SLEEPDIAG: path=` imediatamente anterior a `enterIdleSleep() reached`
identifica a causa raiz. Só depois disso se implementa a correcção.

## Correcção (v1.9.2) — guarda de standby

O sintoma persistiu com KEY3. Como o código continua sem nenhum caminho de
KEY3 para o sleep, a correcção ataca a hipótese A (acoplamento em GPIO3) em vez
de esperar por mais evidência, e fecha a hipótese C (timeout de inactividade a
disparar durante o uso) de caminho.

1. **Guarda de standby** (`lib/Book32_Core/StandbyGuard.h`). No instante em que
   o long press de KEY2 atinge o limiar, os três pinos são relidos. O standby
   só é aceite se KEY2 continuar premido **e** KEY1 e KEY3 estiverem soltos. Um
   LOW em GPIO3 acompanhado de outro botão em baixo é ruído, não uma ordem: quem
   quer standby carrega em KEY2 sozinho.
2. **Recusa definitiva por premir.** Um premir recusado fica marcado
   (`_btnSleepAborted`) até KEY2 ser largado, para que o mesmo LOW não seja
   reavaliado a cada 5ms até calhar passar. Esse premir também deixa de valer um
   refresh completo — ~2 s de e-ink é caro demais para se dar a ruído.
3. **Temporizador de inactividade reposto na amostragem crua.** Qualquer botão
   em baixo repõe o temporizador (limitado a uma vez por 250ms), e não só os
   eventos já classificados pelo OneButton. Um KEY3 mantido premido deixa de
   poder acabar num `IDLE_TIMEOUT`, que para quem está a ler é indistinguível de
   "o KEY3 mandou o leitor dormir".

Continua a haver exactamente dois caminhos para o deep sleep, e o manual
continua a ser só o long press de KEY2.

### Log
Uma recusa aparece como `SLEEPDIAG: standby denied  reason=other_key_held ...`.
Se o sintoma reaparecer **sem** essa linha e **com**
`SLEEPDIAG: path=IDLE_TIMEOUT`, a causa é a hipótese C e não o acoplamento.

### Testes
`tools/tests/test_standby_guard.cpp` (host, sem dependências do Arduino):
```
g++ -std=c++17 -I ../../lib/Book32_Core -o test_standby_guard test_standby_guard.cpp && ./test_standby_guard
```
