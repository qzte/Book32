# Driver de ecrã mais rápido para o GD7965 (`GxEPD2_750_T7_Fast`)

Data: 2026-11-04
Estado: implementado, falta verificar no dispositivo (ver `TODO.txt`)
Origem: ponto 1 de `2026-11-04-avaliacao-upstream-inkdeck.md`

## Problema

Virar uma página demora ~1,8 s no TRMNL 7.5" e um refresh completo (KEY2,
abrir um livro) ~4,1 s. Quase todo esse tempo é a forma de onda do painel,
não o nosso código: o `GxEPD2_750_T7` do GxEPD2 usa uma LUT parcial de 70
frames (~1,6 s) e, no refresh completo, a forma de onda OTP que o controlador
escolhe pela temperatura lida no sensor (~3,9 s à temperatura ambiente).

## O que muda

`lib/Book32_Core/GxEPD2_750_T7_Fast.{h,cpp}` é uma cópia do
`src/epd/GxEPD2_750_T7.{h,cpp}` do **GxEPD2 1.6.9** (a versão fixada em
`platformio.ini`), com a classe renomeada e só duas alterações, ambas
comentadas no código:

1. **LUT parcial mais curta.** As quatro fases passam de 30/5/30/5 frames
   para 16/4/16/4 (70 → 40 frames). As duas fases encolhem na mesma
   proporção, por isso a forma de onda continua com carga equilibrada (a fase
   de pré-carga compensa a fase de mudança de cor), que é o que protege o
   painel de ficar com tensão DC residual.
2. **Refresh completo com temperatura forçada.** Antes do comando de refresh
   (`0x12`) escreve `CCSET = TSFIX` (`0xE0`, `0x02`) e `TSSET = 90`
   (`0xE5`, `0x5A`), para o controlador escolher a forma de onda OTP de alta
   temperatura, que é mais curta. É o mesmo que o próprio GxEPD2 faz no
   `GxEPD2_750_GDEY075T7` (painel irmão, mesmo controlador).

`full_refresh_time`/`partial_refresh_time` passam a 1600/950 ms. São só
estimativas para os logs de `_waitWhileBusy`: o fim real de cada refresh é
dado pelo pino BUSY.

`DisplayMgr.h` troca o `typedef Book32Display` para a classe nova; o resto do
código não muda porque a interface é a mesma.

## Porque assim, e não a cópia do upstream

O `rolohaun/Book32` (InkDeck 1.3.4, `ecff8bf`) fez estas duas alterações e
mediu-as no mesmo kit: página ~1,8 → ~1,15 s, página com refresh completo
~4,1 → ~1,8 s. Está em produção lá desde 11 de outubro de 2026. Mas a cópia
deles parte do GxEPD2 **1.6.7**, e o upstream é agora AGPLv3. Por isso:

- Partimos do ficheiro da 1.6.9 (que tem a mais `writeImageToPrevious` /
  `writeImagePartToPrevious`) e aplicámos só o delta das formas de onda. O
  resultado é código GxEPD2 (GPL-3.0, já ligado ao firmware) com uma
  alteração pequena e documentada, e não código do InkDeck.
- Ficaram de fora as funções `pulseBegin/pulseScan/pulseEnd` do upstream,
  que servem só a capa em tons de cinzento no ecrã de sono (ponto 3 da
  avaliação). Entram se e quando esse ponto for feito.

Os ficheiros estão em `.clang-format-ignore`: mantêm a formatação do GxEPD2
para continuarem comparáveis com um `diff` contra a versão original.

## Riscos

- **Fantasma.** Uma LUT parcial mais curta deixa mais resíduo. No leitor,
  virar página é sempre um refresh parcial (`AppReader.cpp`, `setPartialWindow`)
  e só o clique em KEY2 faz um refresh completo — que agora custa ~1,6 s em
  vez de ~3,9 s. Se numa leitura longa o fantasma incomodar antes de o
  utilizador carregar em KEY2, o passo seguinte é um refresh completo
  automático a cada N páginas (não incluído aqui).
- **Temperatura forçada a 90.** Com o painel frio (abaixo de ~10 °C) a forma
  de onda de alta temperatura pode deixar o refresh completo menos limpo. O
  GxEPD2 aceita isto no GDEY075T7; vale observar no inverno.
- **Atualizar o GxEPD2.** Se `platformio.ini` subir de versão, comparar o
  novo `GxEPD2_750_T7` com este ficheiro antes de o fazer.

## Reverter

Trocar o `typedef` em `DisplayMgr.h` (e o construtor em `DisplayMgr.cpp`) de
volta para `GxEPD2_750_T7` e apagar os dois ficheiros.
