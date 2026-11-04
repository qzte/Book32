# Arranque mais rápido e energia do painel

Data: 2026-11-04
Estado: implementado, falta verificar no dispositivo (ver `TODO.txt`)
Origem: ponto 2 de `2026-11-04-avaliacao-upstream-inkdeck.md`
(rolohaun/Book32, InkDeck 1.3.3 `737a98a` e 1.3.4 `ecff8bf`). O código é
reescrito sobre o nosso, e não copiado: o upstream é AGPLv3.

## 1. Ecrã de arranque numa tarefa à parte

O `setup()` chama `showBootScreen()` seis vezes, e cada chamada bloqueava
durante um refresh inteiro do e-ink. Agora `showBootScreen()` só guarda o
estado mais recente e acorda uma tarefa (`BootScreen`, núcleo 1) que desenha.
Se chegar um estado novo enquanto o painel está ocupado, o anterior é
descartado em vez de ficar em fila — o ecrã de arranque pode saltar passos,
de propósito.

`finishBootScreen()` espera que o último quadro chegue ao painel e devolve o
ecrã ao chamador; `main.cpp` chama-o antes de abrir o menu ou retomar o
livro. Depois disso `showBootScreen()` volta a desenhar de forma síncrona.
Se a tarefa não puder ser criada, o quadro é desenhado na hora, como antes.

Durante o arranque nada mais usa o SPI do ecrã (montar o sistema de ficheiros,
fontes, bateria e input não desenham), e `setRotation()` espera que a tarefa
termine antes de rodar o canvas.

## 2. Rotação guardada em NVS

O primeiro quadro do arranque saía sempre na orientação por omissão, porque
`/display_config.json` só é lido depois de montar o sistema de ficheiros; com o
ecrã rodado isso obrigava a um refresh completo extra a meio do arranque. A
rotação passa a ser copiada para NVS (`Preferences`, espaço `display`) sempre
que muda, e `DisplayMgr::init()` lê-a de lá. O JSON continua a ser a fonte de
verdade e é reaplicado depois de montar o sistema de ficheiros.

## 3. SPI a 10 MHz

O GxEPD2 usa 4 MHz por omissão e envia cada quadro de 48 KB byte a byte.
`selectSPI(SPI, SPISettings(10 MHz))` antes de `display.init()`. O GD7965
aceita mais; 10 MHz é conservador para os fios do kit.

## 4. Desligar o painel entre atualizações

O GxEPD2 só desliga o painel depois de um refresh completo. Depois de um
parcial o booster de alta tensão ficava ligado indefinidamente, o que no
upstream escurecia aos poucos a margem do menu. `DisplayMgr::update()` (no
`loop()`) chama `powerOff()` quando passaram 10 s sem tecla
(`BatteryMgr::getLastActivityTime()`), para viragens de página seguidas não
pagarem os ~150 ms de reacender o booster. `powerOff()` não faz nada se o
painel já estiver desligado.

O ecrã de progresso do OTA desenha a partir de outra tarefa; durante
`GitHubMgr::performFullUpdate()` um `DisplayMgr::PowerHold` impede o `loop()`
de mandar o `powerOff()` a meio de um quadro desse ecrã.

## 5. Bateria e menu

- **Deteção de carga só pela tendência de 90 s.** O ADC oscila mais de 20 mV
  entre leituras, e a deteção "leitura a leitura" em `updateCache()` ligava e
  desligava o estado de carga — e cada mudança redesenhava o painel. Fica só
  a análise de tendência em `update()`. Um refresh explícito da UI só retira o
  "a carregar" depois de 3 min sem subida (`STALE_CHARGING_MS`). Consequência
  aceite: o raio demora ~1,5–2 min a aparecer depois de ligar o carregador.
- **Ícone da bateria no menu** só é redesenhado quando a mudança se vê: o
  preenchimento tem 36 px (~3 % por pixel), por isso o limiar é 5 %, mais a
  mudança de estado de carga e a passagem por 0 %.
- **Refresh completo de limpeza no menu** a cada 20 atualizações parciais.

## 6. WiFiManager

`setConnectTimeout(15)`: com a rede guardada fora de alcance, o portal de
configuração abre em ~15 s em vez dos 60 s por omissão do ESP32.

## Fora deste trabalho

- A contagem de espaço do EbookFS no log do arranque (`usedBytes()` percorre
  o sistema de ficheiros); custo pequeno, fica como está.
- O refresh completo automático no leitor (ver
  `2026-11-04-driver-rapido-gd7965-design.md`).
