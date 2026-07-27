# Standby a disparar no KEY1/KEY3 — plano de diagnóstico (v1.9.1)

## Sintoma relatado

Premir KEY1 (GPIO2) ou KEY3 (GPIO5) faz o aparelho entrar em idle sleep /
standby. O comportamento correto é: só o long press do KEY2 (GPIO3) entra em
standby manualmente.

## O que a auditoria do código mostra

Só existem **dois** caminhos para `BatteryMgr::enterIdleSleep()` em todo o
código (confirmado por `grep` em `lib/` e `src/`):

1. `BatteryMgr::update()` — timeout de inatividade
   (`_sleepTimeoutMinutes > 0 && !charging`).
2. `InputMgr::enterStandby()` — chamado apenas no ramo do long press do KEY2
   dentro de `InputMgr::inputTask()`.

Não há qualquer chamada a partir de `AppMainMenu`, `AppReader` ou
`AppSettings`, e nenhuma ação de input (`INPUT_PREV`, `INPUT_SELECT`,
`INPUT_GO_TO_MAIN_MENU`) leva a sleep. Existe ainda um terceiro deep sleep,
`shutdownLowBattery()`, que não imprime a mensagem de sleep no e-ink.

O mapa de pinos em `include/Config.h` está de acordo com a documentação Seeed
do TRMNL 7.5" (OG) DIY Kit: KEY1 = GPIO2, KEY2 = GPIO3, KEY3 = GPIO5, todos
active-low com `INPUT_PULLUP`.

Conclusão: **não existe caminho lógico** de KEY1/KEY3 para o standby. As
hipóteses restantes são todas verificáveis por log.

## Hipóteses

| # | Hipótese | Assinatura no log |
| - | -------- | ----------------- |
| H1 | GPIO3 vai a LOW quando se prime KEY1/KEY3 (acoplamento, solda, botão em curto, massa partilhada com resistência) | `DIAG ... KEY2/GPIO3 -> LOW` no mesmo instante que KEY1/KEY3, e `KEY1/GPIO2=LOW` ou `KEY3/GPIO5=LOW` na linha do `-> STANDBY` |
| H2 | GPIO3 está permanentemente ou intermitentemente LOW (pull-up ineficaz / botão preso) e o standby dispara sozinho, apenas parecendo coincidir com os botões | `DIAG init ... KEY2/GPIO3=LOW`, ou transições em GPIO3 sem toque nenhum |
| H3 | É o timeout de inatividade, não o KEY2 | `Entering idle sleep... (reason=IDLE-TIMEOUT)` |
| H4 | É o corte por bateria crítica | `Battery critically low` e **sem** mensagem de sleep no ecrã |

## Instrumentação adicionada nesta versão (1.9.1, patch — só diagnóstico)

- `BOOK32_INPUT_DIAG` (Config.h, default 1): uma linha de log por transição de
  nível em GPIO2/GPIO3/GPIO5, com timestamp, mais o nível dos três pinos no
  arranque da task de input.
- `InputMgr::inputTask()`: a linha `-> STANDBY` passa a incluir há quanto tempo
  o GPIO3 está LOW e o nível de GPIO2/GPIO5 nesse instante.
- `BatteryMgr::enterIdleSleep(const char* reason)`: etiqueta do caminho
  (`KEY2-LONGPRESS` vs `IDLE-TIMEOUT`) registada imediatamente antes do deep
  sleep.
- `BOOK32_KEY2_STANDBY_ENABLED` (Config.h, default 1): pôr a 0 desliga
  temporariamente a entrada em standby pelo KEY2, mantendo todo o log. É uma
  mitigação para o aparelho ficar utilizável durante a recolha, **não** a
  correção.

Nenhum comportamento muda com os defaults acima — daí ser um incremento de
patch e não de minor.

## Correções candidatas (depois de ler o log, não antes)

- **H1/H2 confirmadas (elétrico):** exigir N leituras LOW consecutivas em GPIO3
  antes de aceitar o press (confirmação por amostragem, não só debounce no
  release), e recusar o standby se GPIO2 ou GPIO5 estiverem LOW ao mesmo tempo.
  Se o KEY2 nunca chegar a funcionar de forma fiável, mover o standby para
  outro gesto.
- **H3 confirmada:** o bug está no `resetIdleTimer()`/`_lastActivityTime`, não
  nos botões.
- **H4 confirmada:** calibração da bateria (`BATTERY_VOLTAGE_CALIBRATION`,
  `BATTERY_EMPTY_VOLTAGE`) e queda de tensão sob carga.

## Como recolher

```powershell
python -m platformio run --target clean
python -m platformio run --target upload
python -m platformio device monitor
```

Com o monitor aberto: premir KEY1 curto, KEY1 longo, KEY3 curto, KEY3 longo,
KEY2 curto e KEY2 longo — por esta ordem, com pausas — e enviar o log completo.
