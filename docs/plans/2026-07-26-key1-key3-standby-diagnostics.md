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
