# Avaliação do usetrmnl/trmnl-firmware para melhorar o Book32

Data: 2026-08-23
Estado: avaliação — sem alterações de código associadas

## Contexto

O Book32 nasceu sobre o kit de hardware TRMNL (Seeed XIAO ESP32-S3, painel
E-Ink 7.5"), mas resolve um problema diferente do firmware oficial
`usetrmnl/trmnl-firmware`: o TRMNL original é um "dashboard" que acorda
periodicamente, descarrega uma imagem já renderizada de um servidor, mostra-a
e volta a deep sleep; o Book32 é um leitor de EPUB interativo, com o
utilizador a navegar páginas e menus em tempo real. Isto significa que nem
tudo no firmware oficial é transferível — mas há práticas concretas de
engenharia, não específicas do caso de uso "dashboard", que valem a pena
adotar.

Esta avaliação cobre o repositório `usetrmnl/trmnl-firmware` (branch `main`)
e compara-o com o estado atual do Book32 (`v1.10.7`).

## O que o Book32 já faz bem

Vale a pena registar isto primeiro, porque parte do que se esperaria
recomendar já está implementado com cuidado:

- **OTA fail-closed com digest incremental** (`lib/Book32_Core/OtaDigest.h`):
  o dispositivo recusa instalar um asset sem SHA-256 válido, calculado
  incrementalmente durante o download (sem segunda passagem pela flash).
- **Refresh completo automático no leitor** (`lib/Apps/AppReader/AppReader.cpp:910-920`):
  o EpubReader já força um refresh completo a cada N páginas (`_refreshEveryNPages`,
  default 10) para limpar o ghosting acumulado dos refreshes parciais — o
  mesmo padrão que o `trmnl-firmware` usa (`iRefreshMode = REFRESH_FULL;
  // force full refresh every 8 partials`).
- **Gestão de bateria cuidada**: leitura protegida por mutex, rejeição de
  picos de ADC, deteção de carregamento por tendência de tensão com período
  de graça — mais defensiva do que o necessário para um projeto hobby.
- **Testes de host no CI**: 11 ficheiros em `tools/tests/` cobrem lógica pura
  (SemVer, digest OTA, merge de progresso, ordenação de livros, debounce de
  botões) e correm em segundos em cada push/PR — um hábito que muitos
  firmwares hobby não têm.
- **Documentação de decisões** (`docs/plans/*.md`): o próprio projeto já
  documentou compromissos de segurança conscientes (remoção do login web,
  limites do digest OTA) em vez de os deixar implícitos no código.

## Recomendações

### Alta prioridade

**1. TLS não autenticado no canal de OTA (risco de MITM já auto-documentado)** —
✅ implementado em 2026-08-23 via assinatura Ed25519, ver
`docs/plans/2026-08-23-ota-ed25519-signing-design.md`.

`lib/Book32_Update/GitHubMgr.cpp:84` e `:209` chamam `http.begin(apiURL)` /
`http.begin(url)` sem `setCACert()`. O próprio `docs/plans/2026-07-21-ota-integrity-design.md`
já identifica isto: o digest SHA-256 vem da *mesma* resposta da API do
GitHub que fornece o URL do binário, por isso um adversário capaz de
interceptar essa ligação controla o binário e o hash esperado ao mesmo
tempo — o checksum não defende contra isto.

O documento já propõe as duas saídas e recomenda a primeira para uma v1.7.0:

1. Assinatura Ed25519 com chave pública embutida no firmware (independente
   de TLS/CA).
2. `setCACert()` com pinning do CA do GitHub (mais simples, mas quebra
   silenciosamente quando o CA raiz expira/roda).

Isto continua por fazer. É a recomendação de maior impacto desta avaliação,
precisamente porque o projeto já a identificou e ainda não a fechou.

**2. Leitura de bateria sem calibração de fábrica do ADC** —
✅ implementado em 2026-08-23 (`analogReadMilliVolts()` em
`lib/Book32_Core/BatteryMgr.cpp`), pendente de verificação com multímetro em
hardware real.

`lib/Book32_Core/BatteryMgr.cpp:170,181`:

```cpp
raw += analogRead(PIN_BAT_VOLT);
...
float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
voltage *= BATTERY_VOLTAGE_CALIBRATION;  // fudge factor manual, Config.h:50
```

O ADC do ESP32 é conhecidamente não-linear e varia de chip para chip; por
isso existe hoje `BATTERY_VOLTAGE_CALIBRATION 1.075f` como fator de correção
manual. O `trmnl-firmware`, no mesmíssimo padrão de hardware (pino de
tensão + pino de switch de medição — ver `src/battery/seeed_battery.cpp`),
usa `analogReadMilliVolts()` em vez de `analogRead()`:

```cpp
// src/battery/adc_battery.cpp
analogRead(_pin); // inicializa o ADC antes de analogReadMilliVolts()
for (uint8_t i = 0; i < 8; i++) adc += analogReadMilliVolts(_pin);
```

`analogReadMilliVolts()` usa a curva de calibração gravada em eFuse de
fábrica em cada chip ESP32-S3, o que dá uma leitura muito mais próxima da
tensão real sem depender de uma constante afinada à mão por unidade. Trocar
a leitura crua por esta chamada, mantendo o resto da lógica (divisor 2:1,
rejeição de picos, threshold crítico), é uma alteração pequena, testável
localmente com um multímetro, e reduz a necessidade de recalibrar
`BATTERY_VOLTAGE_CALIBRATION` por placa.

### Média prioridade

**3. Sem formatação de código automatizada** — ✅ implementado em 2026-08-23:
`.clang-format` + `.clang-format-ignore` + job "Formatação" em `ci.yml`.
Verifica só as linhas alteradas em cada push/PR (`git-clang-format`), não a
árvore inteira — ver a nota em `tools/format.sh` sobre porquê.

O `trmnl-firmware` tem `.clang-format`, `.clang-tidy` e um job de CI dedicado
(`format.yml`, usando `jidicula/clang-format-action`) que bloqueia PRs com
estilo inconsistente. O Book32 não tinha nenhum dos dois. Com várias sessões
(humanas e de agentes) a tocar no mesmo código ao longo do tempo, isto tende
a gerar diffs ruidosos por reformatação incidental.

**4. Sem opção explícita de "esquecer rede" nas definições** — ✅ implementado
em 2026-08-23: ecrã Sistema → "Esquecer rede", atrás de uma confirmação
dedicada (`SCREEN_CONFIRM_FORGET_WIFI` em `AppSettings.cpp`).

`lib/Book32_Apps/AppSettings.cpp` mostrava SSID, IP e RSSI, mas não expunha
`WiFiManager::resetSettings()`. Antes, a única forma de trocar de rede era
esperar que a ligação falhasse (o `autoConnect()` do WiFiManager só reabre o
portal de configuração nesse momento) ou reflashar.

**5. Ausência de confirmação pós-OTA (`app_valid`) apesar da tabela de
partições já suportar rollback** — ✅ parcialmente implementado em
2026-08-23: `esp_ota_mark_app_valid_cancel_rollback()` chamado no arranque
(`src/main.cpp`). Ver `docs/plans/2026-08-23-post-ota-rollback-design.md`
para uma limitação não verificada — a build `framework = arduino` pura pode
não ter o rollback do bootloader realmente ativo, o que só um teste em
hardware real confirma.

`partitions_16MB.csv` já define `app0`/`app1` como `ota_0`/`ota_1` — layout
de duas partições, compatível com o mecanismo de rollback do ESP-IDF. O
`trmnl-firmware` chama `esp_ota_mark_app_valid_cancel_rollback()` logo no
arranque (`src/main.cpp`, ramo OG) para confirmar ao bootloader que a
imagem atual arrancou com sucesso. O Book32 nunca fazia essa chamada.

### Baixa prioridade

**6. Dependências de biblioteca com ranges em vez de versões fixas** —
✅ implementado em 2026-08-23: todas as `lib_deps` e `platform` em
`platformio.ini` fixadas às versões exatas que o CI já tinha validado
(confirmadas a partir do "Dependency Graph" impresso pelo próprio `pio run`,
não adivinhadas).

`platformio.ini` usava `^` em todas as `lib_deps`
(ex.: `zinggjm/GxEPD2 @ ^1.5.0`), o que permitia que uma build futura
resolvesse uma versão de biblioteca diferente da testada, silenciosamente. O
`trmnl-firmware` fixa versões exatas por placa via ficheiros
`dependencies.lock.*`.

**7. Sem suite de testes de integração em hardware**

O `trmnl-firmware` tem uma suite Unity que corre no próprio dispositivo
(`test/integration/test_all`) e valida ligação WiFi, respostas da API e
comportamento de bateria fraca em hardware real, além dos testes de host. O
Book32 só tem testes de host. Para um projeto de um único board, montar um
harness assim em CI é provavelmente desproporcionado — mas um checklist de
fumo manual (arranque, upload de EPUB, OTA, standby/wake) corrido antes de
cada tag `vX.Y.Z` capturaria grande parte do valor a um custo muito menor.

## O que não é transferível (e porquê)

- **Deep sleep entre atualizações**: faz sentido para um dashboard que só
  precisa de estar acordado ~10s a cada 15-30 min; o Book32 é interativo, o
  utilizador está ativamente a virar páginas, por isso o sleep por
  inatividade que já existe (`BatteryMgr::enterIdleSleep`) é a adaptação
  correta, não uma lacuna.
- **Suporte a três variantes de hardware (OG/X/BWRY) via ESP-IDF+CMake**: o
  `trmnl-firmware` paga a complexidade de `CMakeLists.txt`,
  `sdkconfigs/`, flashing de modem celular, etc. porque suporta várias
  placas. O Book32 tem uma placa alvo e usa PlatformIO+Arduino puro —
  migrar para ESP-IDF só para ganhar acesso a algumas APIs (ex.: o
  `esp_ota_mark_app_valid_cancel_rollback` do ponto 5) não se justifica; essa
  API está disponível diretamente também no framework Arduino via
  `esp_ota_ops.h`, sem precisar da migração.
- **Autenticação por token de dispositivo na API**: aplica-se ao backend
  cloud do TRMNL. O Book32 não tem backend próprio — a decisão já
  documentada de remover o login web em favor do isolamento de rede
  (`docs/plans/2026-07-26-remover-login-web-design.md`) é a resposta
  adequada ao mesmo problema dentro das restrições reais do projeto.

## Resumo

| # | Recomendação | Impacto | Esforço |
|---|---|---|---|
| 1 | ✅ Autenticar TLS no canal OTA (assinatura ou CA pinning) | Alto (segurança) | Médio–Alto |
| 2 | ✅ `analogReadMilliVolts()` na leitura de bateria | Médio (precisão) | Baixo |
| 3 | ✅ `.clang-format` + CI de formatação | Médio (manutenção) | Baixo |
| 4 | ✅ "Esquecer rede" nas definições | Baixo (UX) | Baixo |
| 5 | ✅⚠️ Confirmar `app_valid` pós-OTA para rollback automático | Médio (robustez) | Médio |
| 6 | ✅ Fixar versões exatas em `lib_deps` | Baixo (reprodutibilidade) | Baixo |
| 7 | Checklist de fumo manual pré-release | Baixo (qualidade) | Baixo |

Os pontos 1 e 2 são os que trazem mais valor por esforço: o primeiro porque
o próprio projeto já sabe que está em aberto, o segundo porque é uma troca
de uma linha com efeito direto na precisão que os utilizadores veem no
ecrã.
