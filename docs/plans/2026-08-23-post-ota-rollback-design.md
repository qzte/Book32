# v1.11.0 — Confirmação pós-OTA (`esp_ota_mark_app_valid_cancel_rollback`)

Data: 2026-08-23
Estado: implementado, com uma limitação não verificável nesta sessão (ver
"Limites" abaixo) — ver recomendação 5 de
`docs/plans/2026-08-23-avaliacao-trmnl-firmware.md`.

## O que muda

`partitions_16MB.csv` já define `app0`/`app1` como `ota_0`/`ota_1` — o layout
de duas partições que o mecanismo de rollback do ESP-IDF espera. O
`trmnl-firmware` chama `esp_ota_mark_app_valid_cancel_rollback()` logo no
arranque para confirmar ao bootloader que a imagem atual arrancou com
sucesso; o Book32 nunca fazia essa chamada. `src/main.cpp` passa agora a
chamá-la como a primeira linha de `setup()`, antes de qualquer outra
inicialização — inclui `<esp_ota_ops.h>`, já indiretamente ligado no binário
porque `Update.h` (usado em `GitHubMgr.cpp`) chama as mesmas APIs `esp_ota_*`
por baixo.

## O que isto garante — e o que não garante

A chamada em si é inofensiva e correta de se ter, **independentemente** de o
rollback automático do bootloader estar ou não ativo: se não estiver, é um
no-op; se estiver, evita que o dispositivo fique preso num estado "a aguardar
confirmação" que nunca chega.

O que não consegui confirmar nesta sessão, por não ter acesso a hardware nem
à sdkconfig exata usada para compilar o pacote `framework-arduinoespressif32`
que o CI resolveu (`3.20017.241212+sha.dcc1105b` — ver
`platformio.ini`), é se o **rollback automático do próprio bootloader** está
de facto ativo nesta build. A minha melhor hipótese, que não pude verificar:

- O Book32 usa `framework = arduino` puro (não `framework = arduino, espidf`).
  Nesse modo, o PlatformIO usa o pacote `framework-arduinoespressif32`
  pré-compilado da Espressif — incluindo o bootloader, que vem já compilado
  de fábrica, não recompilado a partir do projeto.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` é uma opção do **bootloader**, não
  da aplicação. Sem recompilar o bootloader a partir de código-fonte (o que
  exigiria o modo híbrido `arduino, espidf` ou substituir o binário do
  bootloader por outro meio), um `sdkconfig.defaults` no projeto não tem como
  a alterar.

Se esta hipótese estiver correta, `esp_ota_mark_app_valid_cancel_rollback()`
não dá hoje proteção real contra um OTA mau que arranca em ciclo de crash —
só posiciona o código corretamente para o dia em que essa proteção exista.
Confirmar isto ao certo exigiria testar num dispositivo real: instalar um
firmware propositadamente crashado no arranque e observar se o bootloader
reverte sozinho para a partição anterior, ou queria inspecionar a sdkconfig
exacta com que a Espressif publicou este pacote do framework — nenhuma das
duas coisas foi possível aqui.

## Caminho para fechar isto por completo

Se a hipótese acima se confirmar, as opções são:

1. Migrar para `framework = arduino, espidf` (Arduino como componente
   ESP-IDF), o que dá acesso a `sdkconfig.defaults` e recompila o bootloader
   a partir daí. É uma mudança de build system bem maior do que o resto desta
   alteração, e só vale a pena com testes reais em hardware.
2. Manter o caminho de recuperação atual: USB (`pio run --target upload`),
   já documentado no `README.md` e no OTA integrity design doc.

Não persegui a opção 1 nesta sessão — é uma decisão maior a tomar com
hardware disponível para validar, não algo para decidir às cegas.
