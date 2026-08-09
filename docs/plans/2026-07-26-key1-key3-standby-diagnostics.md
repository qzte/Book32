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

## Correcção (v1.10.2) — limiar de standby separado

O sintoma foi relatado uma terceira vez, com a v1.9.2 já em main. Reler o
guarda com atenção: `classifyStandbyRequest()` relê os três pinos no instante
da decisão, e KEY3 é lido do seu próprio GPIO (5), não inferido a partir de
GPIO3. Enquanto o utilizador estiver mesmo a premir KEY3 nesse instante, a
leitura tem de mostrar `key3Held=true` e o guarda tem de recusar — isso não
mudou e continua verdadeiro. O que ficou por corrigir foi o **limiar**: o
guarda só é avaliado quando `now - _btnSleepPressTime` atinge
`BUTTON_LONG_PRESS_MS`, que são uns meros 400ms — o mesmo valor que KEY1 e
KEY3 usam para os seus próprios long press (ir ao menu principal, SELECT).
400ms é fácil de atingir sem intenção de standby nenhuma, e é uma janela
curta a mais para um LOW espúrio em GPIO3 (ruído, solda marginal, terra
partilhado) coincidir por acaso com o instante exacto da amostra.

Introduzido `STANDBY_HOLD_MS` (1500ms) em `StandbyGuard.h`, um limiar só para
o standby, bem acima do `BUTTON_LONG_PRESS_MS` que as outras acções
continuam a usar. `InputMgr::inputTask()` só chama
`classifyStandbyRequest()` depois de KEY2 estar premido por
`STANDBY_HOLD_MS`, não por `BUTTON_LONG_PRESS_MS`. Efeitos:

- Um long press normal de KEY1/KEY3 (400ms) deixa de conseguir, por si só,
  aproximar-se sequer do limiar de standby — já não são a mesma duração.
- Qualquer ruído em GPIO3 tem de se sustentar por 1.5s inteiros, não só
  400ms, para ter alguma hipótese de ser mal interpretado como long press de
  KEY2 — o que reduz bastante a janela para coincidir com a amostra única do
  guarda.
- Um KEY2 largado entre 400ms e 1500ms não faz nada (nem refresh, nem
  standby): passou da janela de clique mas não chegou à de standby. Isto é
  intencional — um premir "a meio" não deve ter efeito nenhum.

Continua por confirmar em hardware real: nenhuma sessão até agora teve
acesso ao dispositivo físico, apenas aos testes de host
(`tools/tests/test_standby_guard.cpp`, 9 casos) e à leitura do código. Se o
sintoma for relatado uma quarta vez com esta versão em campo, os únicos
caminhos que sobram são (a) o LOW em GPIO3 realmente se sustenta por mais de
1.5s a par de KEY3 solto — o que já não é uma janela de amostragem
infeliz, é um curto-circuito ou ligação eléctrica persistente entre os dois
pinos — ou (b) o botão fisicamente rotulado "KEY3" na caixa não está de
facto ligado a GPIO5/`PIN_BUTTON`, e o que o utilizador prime é
electricamente GPIO3. Nenhuma das duas tem correcção por software: a
instrumentação `PINDIAG`/`SLEEPDIAG` já existente no código é o que
distingue as duas, e é isso que é preciso capturar do dispositivo real antes
de continuar a especular.

## Resolvido (v1.10.3) — era a hipótese B, confirmada por log real

Primeiro log real do dispositivo, capturado a pedido depois de uma quarta
ocorrência (desta vez KEY3 a click normal, a meio de virar página em vez de
long press). Duas capturas:

1. Utilização normal (KEY3 → NEXT, KEY2 → REFRESH, KEY1 → PREV): cada
   `PINDIAG` mostra **um único** pino a mudar por evento. Sem coincidência
   nenhuma entre pinos — a hipótese A (acoplamento eléctrico) fica
   directamente desmentida pela primeira vez que há dados reais para a testar.
2. O momento do sintoma:
   ```
   PINDIAG: KEY1/GPIO2=1  KEY2/GPIO3=0  KEY3/GPIO5=1
   KEY2: Button pressed
   INPUT: KEY2 Long Press -> STANDBY requested
   SLEEPDIAG: path=KEY2_LONG_PRESS  KEY1/GPIO2=1  KEY2/GPIO3=0  KEY3/GPIO5=1
   SLEEPDIAG: enterIdleSleep() reached  reason=key2_long_press
   ```
   Sem nenhuma linha `PINDIAG` intermédia entre o premir e a decisão, ou seja
   nenhum pino mudou de estado durante o hold inteiro (≥1.5s, já com o
   `STANDBY_HOLD_MS` da v1.10.2). GPIO3 sozinho, sustentado, KEY1 e KEY3
   soltos do princípio ao fim. Isto não é ruído nem acoplamento — é um long
   press limpo e genuíno em GPIO3. O guarda (`StandbyGuard.h`) e o limiar
   (`STANDBY_HOLD_MS`) fizeram exactamente o que foram desenhados para
   fazer.

O que estava errado não era o código: era a hipótese B, que tinha sido
descartada cedo demais (nota da v1.9.1: "GPIO3 é o botão do meio em
qualquer das duas ordens possíveis, pelo que uma simples inversão de
etiquetas não explica o sintoma" — verdade para uma troca KEY1↔KEY3, mas
não cobre uma troca KEY2↔KEY3). Este é um kit DIY montado à mão; o botão que
o utilizador usa fisicamente para virar página está ligado ao pino que o
`Config.h` chamava GPIO3/`PIN_BUTTON_SLEEP` (KEY2), não ao GPIO5/`PIN_BUTTON`
(KEY3) que a wiki do kit assume.

### Correcção
`include/Config.h`: `PIN_BUTTON` e `PIN_BUTTON_SLEEP` trocados (3 ↔ 5), para
corresponder à fiação real deste aparelho. Todo o resto do código lê os
pinos só através destas macros — confirmado por grep, sem nenhum número de
GPIO em bruto em `lib/` — por isso a troca não pediu mais nenhuma alteração
funcional. O aviso de acordar por KEY3 (`esp_sleep_enable_ext0_wakeup`) usa a
mesma macro `PIN_BUTTON`, por isso continua a acordar com o botão que o
utilizador já usa para tudo o resto.

O guarda de standby e o `STANDBY_HOLD_MS` da v1.10.2 ficam no código: não
fizeram mal nenhum, e continuam a ser uma defesa razoável contra ruído
genuíno se algum dia aparecer. Só deixam de ser, sozinhos, a explicação do
sintoma relatado.
