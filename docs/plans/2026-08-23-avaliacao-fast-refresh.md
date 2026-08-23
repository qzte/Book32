# O que é o "Fast Refresh" da TRMNL, e vale a pena no Book32?

Data: 2026-08-23
Estado: avaliação — sem alterações de código associadas
Fonte: artigo "What is fast refresh?" (help.trmnl.com, por Mario, 27 fev 2026).
`help.trmnl.com` está fora da política de rede desta sessão de agente
(bloqueio 403 confirmado via WebFetch e via `curl` direto); o conteúdo do
artigo foi colado pelo utilizador na conversa.

## O que o artigo diz

- Nos painéis EPD (E-Ink) do TRMNL (OG), o driver original do fabricante só
  suportava refresh completo: a cada mudança de ecrã, o painel tinha de
  passar por preto-total / branco-total antes de mostrar o conteúdo — um
  "flash" visível mas necessário para garantir que nenhum pixel ficava preso
  na cor errada.
- Em firmware 1.6.x (agosto de 2025), a TRMNL lançou "fast refresh": um
  refresh parcial que só troca os pixels que realmente mudam de preto para
  branco (ou vice-versa), deixando o resto do ecrã intocado. O resultado é
  uma transição quase instantânea, como um ecrã LCD.
- Mesmo com fast refresh é preciso fazer um refresh completo de vez em
  quando, por causa do "ghosting" (pixels que não mudam de estado
  corretamente e deixam um resíduo visual, tornando a imagem baça ou
  acinzentada). A TRMNL faz esse refresh completo a intervalos regulares, ou
  quando muda a profundidade de bits.
- Ao mesmo tempo, a TRMNL lançou suporte a 2-bit (4 tons: preto, branco,
  cinza-claro, cinza-escuro) num painel 1-bit, substituindo o driver do
  fabricante para o conseguir. Isto tem um custo: o hardware do painel não
  suporta fast refresh e 2-bit em simultâneo. Uma imagem 1-bit pode usar
  fast refresh; uma imagem 2-bit obriga a voltar ao flash completo. Não é
  uma escolha do utilizador — é o dispositivo que decide, consoante o
  conteúdo a mostrar (o artigo nota que a escolha manual 1-bit/2-bit por
  plugin ainda estava por lançar à data de publicação).

## Comparação com o Book32

1. **Refresh parcial já é o padrão em quase todo o Book32**, via
   `display.setPartialWindow(...)` do GxEPD2 (`zinggjm/GxEPD2 @ 1.6.9`,
   versão fixada em `platformio.ini`):
   - Viragem de página no leitor (`AppReader.cpp:775`)
   - Navegação no menu principal (`AppMainMenu.cpp:354-360`)
   - Indicador de bateria (`BatteryMgr.cpp:422`)
   - Barra de progresso no arranque (`DisplayMgr.cpp:113`)
   - Progresso de OTA (`GitHubMgr.cpp:33`)

   É exatamente o mecanismo que o artigo chama de "fast refresh": só os
   pixels que mudam são reescritos, sem passar por preto/branco total. A
   diferença é que no Book32 este comportamento vem "de fábrica" do GxEPD2
   para o painel usado (`GxEPD2_750_T7`, Waveshare 7.5" V2) — não foi preciso
   escrever um driver alternativo, ao contrário do que a TRMNL teve de fazer
   para o painel OG original, cujo driver de fabricante não expunha refresh
   parcial.

2. **Refresh completo periódico contra ghosting — já implementado e
   configurável**:
   - `SettingsStore.h:23`: `refreshFrequency` (default 10, "full e-ink
     refresh every N page turns"), ajustável no ecrã de definições
     (`AppSettings.cpp:199`) e persistido em `reader_config.json`.
   - `AppReader.cpp:911-918`: conta `_pageTurnsSinceRefresh` e força
     `setFullWindow()` quando atinge `_refreshEveryNPages`.
   - Já registado no doc de avaliação anterior
     (`docs/plans/2026-08-23-avaliacao-trmnl-firmware.md`), que aponta a
     mesma equivalência com o padrão do `trmnl-firmware`
     (`iRefreshMode = REFRESH_FULL; // force full refresh every 8 partials`).

3. **Refresh completo manual** — o KEY2 já permite ao utilizador forçar um
   refresh completo a qualquer momento, sem esperar pelo contador
   (`docs/plans/2026-07-26-key2-full-refresh-design.md`), cobrindo o caso em
   que o ghosting incomoda antes do intervalo configurado.

4. **Profundidade de bits** — o Book32 é 1-bit em todo o lado: o tipo de
   display é `GxEPD2_BW<GxEPD2_750_T7, ...>` (não `GxEPD2_3C`, apesar do
   header estar incluído em `DisplayMgr.h`), e as capas de livro
   (`B32Reader::getCover`) são bitmaps 1-bit descomprimidos e desenhados com
   `drawBitmap(..., GxEPD_BLACK)`. Ou seja, o Book32 nunca entra no lado do
   trade-off que o artigo descreve (2-bit ⇒ perde fast refresh): ao ficar em
   1-bit em todo o lado, mantém sempre o refresh rápido disponível — que é
   precisamente o que convém a um leitor de texto, onde nitidez de
   preto/branco importa mais do que tons de cinza.

## Vale a pena implementar?

O mecanismo central de "fast refresh" — não o nome, o comportamento — já
está implementado no Book32, com as mesmas boas práticas que a TRMNL
descreve: refresh parcial para transições rápidas, refresh completo
periódico para limpar ghosting, e uma válvula de escape manual. Não há aqui
uma funcionalidade em falta para portar.

A única peça do artigo que o Book32 não tem é o **suporte a 2-bit/grayscale
(4 tons)** — e isso não é uma parte em falta do "fast refresh" em si, é uma
funcionalidade separada, com o mesmo trade-off que a TRMNL descreve: ganhar
tons de cinza custaria o refresh rápido nas imagens que os usassem. Para o
caso de uso do Book32 (texto de EPUB, ocasionalmente uma capa de 60x80px)
isto tem valor marginal:

- Texto não beneficia de cinza — perde-se contraste/legibilidade, que é o
  oposto do que se quer num leitor.
- As capas já são convertidas para 1-bit no momento em que o `.b32` é
  gerado; adicionar grayscale exigiria mudar esse pipeline de conversão, o
  formato do blob de capa armazenado, e possivelmente trocar a classe de
  driver do GxEPD2 — esforço concentrado numa miniatura de 60x80px que só é
  visível na lista de livros.

**Recomendação: nenhuma ação.** O padrão que o artigo descreve já está
implementado e bem coberto no Book32; a única parte não coberta (grayscale)
não compensa o custo para este produto.

## Resumo

| Peça do "Fast Refresh" (TRMNL) | Estado no Book32 |
| --- | --- |
| Refresh parcial (só pixels que mudam) | ✅ Já é o padrão, via GxEPD2 `setPartialWindow`, nativo do driver do painel |
| Refresh completo periódico (anti-ghosting) | ✅ `_refreshEveryNPages`, configurável, 10 por default |
| Refresh completo manual (válvula de escape) | ✅ KEY2 click → `forceRedraw()` |
| Suporte 2-bit/grayscale (com perda de fast refresh) | ❌ Não implementado — trade-off não compensa para um leitor de texto 1-bit |
