# Segunda avaliação do upstream rolohaun/Book32 (agora "InkDeck")

Data: 2026-11-04
Estado: avaliação — sem alterações de código associadas
Avaliação anterior: `2026-08-25-avaliacao-upstream-rolohaun.md` (feita sobre o
commit `34a79cf`, versão 1.1 do upstream)

## Contexto

Desde a última avaliação o upstream mudou muito: são ~377 commits até
`c91391c` (InkDeck 1.4.7, 2026-11-02). O projeto passou a chamar-se
**InkDeck** e suporta três placas (Book32/TRMNL 7.5", Seeed Studio Sticky
3.97" com toque, LILYGO T5 S3 Pro 4.7") e ainda uma edição Python para
Raspberry Pi. Grande parte do trabalho novo (emuladores Game Boy/NES/Arduboy,
PDF/CBZ com pinch-to-zoom, ecrãs tácteis, FastEPD) **só existe nas outras
placas** — está atrás de `BOOK32_HAS_TOUCH`, `BOOK32_HAS_SD`,
`BOOK32_HAS_INKBOY` e `BOOK32_HAS_PAGE_BOOKS`, todos a `0` para o Book32. O
próprio upstream tirou o Inkade (jogos) do Book32 na 1.4.1 porque o painel
GD7965 deixava rasto e listras.

Continua a não haver história em comum (`git merge-base` falha): a avaliação
é, outra vez, funcionalidade a funcionalidade.

### Aviso de licença (ler antes de copiar código)

O upstream é agora **AGPL-3.0-or-later** (`LICENSE.md`/`COPYING` do upstream).
Este fork não tem ficheiro de licença. Copiar código do upstream obriga a
distribuir o fork sob AGPLv3 (fonte disponível a quem usa a interface web,
incluindo um link visível para o código). Duas opções:

1. Adotar AGPLv3 no fork (adicionar `COPYING`/`LICENSE.md`) e copiar à
   vontade, com atribuição.
2. Reimplementar a partir da ideia/medições descritas nos commits, sem
   copiar código.

Exceção: o driver `GxEPD2_750_T7_Fast` é uma modificação do GxEPD2, que já é
**GPL-3.0** — quem o copiar fica obrigado à GPLv3 de qualquer forma, e o fork
já liga o GxEPD2 ao firmware.

## Vale a pena trazer (aplica-se ao Book32, ordenado por valor/esforço)

### 1. Driver de ecrã mais rápido — `GxEPD2_750_T7_Fast` (upstream 1.3.4, `ecff8bf`)

É o item de maior valor. Mesmo painel e controlador que usamos
(`GxEPD2_750_T7`, GDEW075T7 / GD7965, `lib/Book32_Core/DisplayMgr.h:9`).
O upstream copiou o driver do GxEPD2 1.6.7 e mudou duas coisas:

- **Refresh parcial** com uma forma de onda mais curta mas ainda com carga
  equilibrada: 0,98 s em vez de 1,6 s.
- **Refresh completo** forçando a temperatura do controlador para que use a
  forma de onda OTP rápida (como o GxEPD2 já faz no GDEY075T7): 1,6 s em vez
  de 3,9 s.

Medido em hardware pelo upstream: virar página passa de ~1,8 s para ~1,15 s, e
de ~4,1 s para ~1,8 s numa página com refresh completo. O relógio SPI sobe
para 10 MHz no construtor (relevante também para o arranque, ver item 4).

Esforço: baixo — é um ficheiro `.h`/`.cpp` (~500 linhas) e trocar o `typedef`
em `DisplayMgr.h`. Risco: médio, por ser forma de onda do painel; o upstream
já o usa em produção desde 1.3.4 (11 de outubro). A nossa tecla KEY2 (refresh
completo) passa a ser mais barata, e o fantasma tem de ser verificado no
dispositivo durante uma leitura longa (TODO.txt) antes de fazer release.
Atenção: nós fixámos GxEPD2 1.6.9 e o driver deriva da 1.6.7 — confirmar que
a interface `GxEPD2_EPD` não mudou entre as duas.

### 2. Arranque mais rápido e mancha na margem do menu (upstream 1.3.3, `737a98a`)

- O ecrã de arranque desenha numa tarefa à parte enquanto o arranque continua,
  descartando estados já ultrapassados — o arranque deixa de esperar por
  cada refresh do painel de 7,5".
- Rotação guardada em NVS para a primeira imagem já sair na orientação
  certa.
- `WiFiManager` com timeout de ligação de 15 s.
- **Escurecimento ao longo da margem do menu**: causado pelo booster do
  painel ficar ligado depois de refreshes parciais. Correção: desligar o
  painel (`powerOff`) depois de cada atualização. Neste fork não há nenhuma
  chamada a `powerOff()`/`hibernate()` fora do sono — vale confirmar se o
  sintoma existe aqui. O upstream mantém o painel ligado 10 s depois de
  uma tecla (no 1.3.4) para viragens rápidas não pagarem o arranque do
  booster — é um compromisso a copiar junto.
- Ruído do ADC da bateria a alternar "a carregar"/"não carregar" e a
  redesenhar o ecrã inteiro: passaram a decidir só pela tendência de 90 s e
  a redesenhar o ícone só quando muda à vista.

Esforço: baixo a médio, separável em pedaços.

### 3. Capa do livro no ecrã de sono, em tons de cinzento (upstream 1.3.8 `6b024d7`, 1.4.0 `b2ea3b2`)

Ao adormecer, o ecrã mostra a capa do último livro lido em ecrã inteiro com
a mensagem de sono por baixo (ou só a mensagem, se não houver capa; o aviso de
bateria fraca tem sempre prioridade). No Book32 a capa sai em **cinzento real**
com impulsos curtos na LUT do GD7965 (`pulseBegin/pulseScan/pulseEnd`, tabela
de tons afinada com fotos de capas).

Já temos quase tudo o que é preciso: `CoverImage.cpp` converte capas JPEG/PNG
e a v1.22 já guarda `/covers/<nome>.cover` em ecrã inteiro. Uma primeira
versão a 1 bit (com o pontilhado que já fazemos) é de esforço baixo — basta
`BatteryMgr` desenhar essa cache antes de `esp_deep_sleep_start()`. O cinzento
por impulsos é mais arriscado (forma de onda) e fica para uma segunda fase.

### 4. Ilustrações dentro dos capítulos

O README do upstream anuncia "EPUB books, with illustrations": o
`TextRenderer` do upstream tem `ImageNode` e desenha imagens no meio do texto,
com tamanho a partir de `width`/`height`/percentagem do EPUB. Aqui o leitor só
desenha `CONTENT_TEXT` (`EpubLoader.cpp:763`). Como já temos descodificação
JPEG/PNG com pontilhado para as capas, o que falta é paginação e desenho.
Esforço: médio a alto (paginação, `PageCountStore`, cache de páginas).

### 5. Teclado/comando Bluetooth LE como virador de páginas (upstream 1.4.0 e `7e597c6`)

O upstream acrescentou NimBLE-Arduino 1.4.3, um descodificador de descritores
HID e emparelhamento a partir das definições e da web; desde `7e597c6` os
teclados BLE também funcionam (setas, Enter, Esc). Os "page turners"
Bluetooth baratos apresentam-se como teclados HID, por isso isto daria um
**comando à distância para virar páginas** — no upstream os comandos já
servem para menus e para o leitor. Custo: RAM/flash do NimBLE e consumo da
rádio (tem de estar desligada no sono). Esforço: médio.

### 6. Pequenas correções na web

- **Porta 80 em `TIME_WAIT`** (`ac98dd8`): depois do portal do WiFiManager, as
  ligações do portal ficam em `TIME_WAIT` na porta 80, o `bind` do AsyncTCP
  falha e o `end()` liberta duas vezes o mesmo objeto (CORRUPT HEAP). Usamos o
  mesmo par WiFiManager + ESPAsyncWebServer (`src/main.cpp:33`), por isso o
  problema pode existir aqui depois da configuração inicial de WiFi. Vale
  verificar e, se aparecer, a correção são ~50 linhas em `WebMgr.cpp`.
- **Cartão WiFi no painel web** (`95ff276`): escolher/juntar uma rede a
  partir da página web quando o dispositivo está no hotspot. Baixo esforço,
  só `data/`.

## Opcional — só se houver interesse

- **Biblioteca em forma de estante** (`e5ba14b`, `bc03848`, `c572930`): três
  prateleiras com três livros, títulos por baixo, nome do dono ("Ron's
  Bookshelf"), rodapé com versão/bateria/IP. Cosmético; a nossa biblioteca
  tem layout e estado próprios (`LibraryLayout`, estados de leitura) que
  teriam de ser reconciliados.
- **Escolher apps no instalador** (`e5ba14b`): o instalador web escreve a
  escolha num setor do `app0`. Só faz sentido se adotarmos Klipper/Todo.
- **AppKlipper / AppTodo**: continuam iguais à avaliação anterior — fora do
  âmbito de um leitor.
- **Open Sans com tremas** (1.4.6): já temos cinco fontes com cobertura PT; só
  interessa se quisermos uma sans-serif adicional.

## Já temos — sem ação

- **KEY1 = página anterior, KEY2 = menu** (upstream 1.4.5, `4791044`): o fork
  já usa as três teclas (KEY2 = refresh completo / standby).
- **Gravação de progresso adiada** (1.3.4): já gravamos ao abrir e com
  `flushProgress()`, não antes de cada página.
- **Sono profundo** (1.3.8): já existe em `BatteryMgr.cpp` com `ext0`.
- **Progresso pelo índice EPUB, rodapé configurável, abertura mais rápida**
  (1.2.9–1.2.11): cobertos por `ChapterTocStore`, `BookIndexer` e trabalho
  próprio.
- **QR do hotspot offline**: já adotado (`platformio.ini:50`).

## Não se aplica ao Book32

PDF/CBZ com zoom, Inkade (Game Boy/NES/Arduboy), FastEPD, toque, cartão SD,
luz frontal, edição Raspberry Pi, placa Waveshare RLCD (ramo
`waveshare-rlcd-42`).

## Resumo

| # | Item | Origem | Impacto | Esforço | Recomendação |
|---|---|---|---|---|---|
| 1 | Driver `GxEPD2_750_T7_Fast` (página 1,8→1,15 s, completo 4,1→1,8 s) | 1.3.4 | Alto | Baixo | **Fazer primeiro**, validar fantasma no dispositivo |
| 2 | Arranque em tarefa, SPI 10 MHz, `powerOff` após refresh, bateria estável | 1.3.3 | Médio | Baixo–médio | Fazer, por partes |
| 3 | Capa no ecrã de sono (1 bit primeiro, cinzento depois) | 1.3.8/1.4.0 | Médio | Baixo (1 bit) | Fazer |
| 4 | Ilustrações dentro dos capítulos | 1.4.x | Médio | Médio–alto | Avaliar depois |
| 5 | Comando BLE para virar páginas | 1.4.0/`7e597c6` | Médio | Médio | Se houver interesse |
| 6 | Porta 80 / cartão WiFi na web | `ac98dd8`/`95ff276` | Baixo | Baixo | Verificar se o bug ocorre |
| 7 | Estante, escolha de apps, Klipper, Todo, Open Sans | vários | Baixo | — | Opcional |
| 8 | Jogos, PDF/CBZ, toque, Pi, RLCD | vários | — | — | Não se aplica |

Antes de copiar código, decidir a questão da licença (secção acima).
