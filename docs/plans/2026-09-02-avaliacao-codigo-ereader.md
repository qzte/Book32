# Avaliação do código do eReader (AppReader, EpubLoader, TextRenderer, CoverImage)

Data: 2026-09-02
Estado: avaliação — sem alterações de código associadas

## Âmbito

Revisão de código do leitor tal como está em `lib/Apps/AppReader/`
(`AppReader.{h,cpp}` 1.7k linhas, `EpubLoader.{h,cpp}` 970, `TextRenderer.{h,cpp}`
490, `CoverImage.{h,cpp}` 230) e das dependências directas em
`lib/Book32_Core/` (`WordFitLogic.h`, `HyphenationLogic.h`, os *stores*, `DisplayMgr`).
Só revisão estática: `pio run` não correu neste ambiente e não houve
dispositivo. Ao contrário de `2026-08-31-avaliacao-gestao-epub.md`, que olhou
para *o que falta ao EPUB* (TOC, capas, CSS), esta olha para *o código que
existe*: defeitos, trabalho repetido, estrutura e testabilidade. Os itens
dessa avaliação já feitos (capas, TOC, `<ol>`, `B32Reader`) não se repetem.

## O que está bem (e deve ser preservado)

- **Paginação por posição de conteúdo** (`PagePointer{nodeIndex, charOffset}`)
  em vez de páginas persistidas: as posições guardadas sobrevivem a mudanças
  de tipo/tamanho de letra e a novas versões do renderizador. É a decisão
  arquitectural mais importante do leitor e está certa.
- **Trabalho pesado em fatias a partir de `update()`** (contagem de páginas,
  índice, "ir para %", títulos e capas na biblioteca), sempre com "marcar
  antes de tentar" para um ficheiro mau nunca ser reaberto em ciclo, e com
  *checkpoint* por capítulo na contagem para sobreviver ao *deep sleep*.
- **Defesa contra ficheiros do utilizador**: tecto de 256 KB por entrada do
  ZIP, `<item` sem `>` não trava, `lineBuf` já não transborda
  (`WordFitLogic.h`), JPEGDEC fora da *stack*, *callback* de desenho com
  verificação de limites própria.
- **Escrita diferida do progresso** (4 s de quietude ou fecho do livro), com
  `stop()` chamado antes do standby por KEY2 — o flash não é reescrito a
  cada página.
- **Lógica pura separada e testada em host** (`WordFitLogic`, `Hyphenation`,
  `GoToPercentLogic`, `BookTitleLogic`, ...): é o padrão a estender, não a
  contrariar (ver secção "Testabilidade").
- **Comentários que explicam o porquê** e que citam a falha real que motivou
  cada guarda. Facilita muito uma revisão como esta.

## Defeitos encontrados

Por ordem de impacto no que o leitor mostra no ecrã.

### D1. Tabelas são analisadas mas nunca desenhadas

`parseHtmlToRichContent()` produz nós `CONTENT_TABLE` (`EpubLoader.cpp:620-634`,
com `parseTable()` a tratar `colspan`/`rowspan`/`th`), mas o único ramo do
ciclo de `renderRichPageDynamic()` é `if (node.type == CONTENT_TEXT)`
(`TextRenderer.cpp:160`); um nó de tabela cai para `currentNode++` sem
desenhar nada. Confirmado por grep: o único outro sítio que lê
`CONTENT_TABLE` é `chapterTextLength()` (`AppReader.cpp:995`), para o
"ir para %". Consequências:

- qualquer conteúdo em `<table>` desaparece sem rasto (comum em apêndices,
  cronologias, poemas paginados em tabela por alguns conversores);
- o "ir para %" conta o texto das tabelas como se fosse lido, por isso a
  percentagem fica ligeiramente desalinhada com o que se vê;
- todo o código de `parseTable()` (50 linhas) e os `struct Table/TableRow/
  TableCell` são peso morto no firmware.

Correcção mínima (baixo esforço): no renderizador, desenhar cada linha da
tabela como um parágrafo com as células separadas por " | " (ou uma célula
por linha, indentada, quando são mais de 2 colunas). Cabe no modelo actual de
linhas e devolve o conteúdo ao leitor. A alternativa honesta é remover
`parseTable()` e tratar `<table>` como blocos de texto simples — melhor do
que perder o texto.

### D2. `text-align` é lido e depois ignorado; itálico também

`getAlignFromStyle()` preenche `RichTextNode::align` (`EpubLoader.cpp:593`),
mas o renderizador nunca lê `.align`: só centra `STYLE_HEADER1/2` por estilo.
Um `<p style="text-align:center">` (epígrafes, dedicatórias, assinaturas de
cartas) sai alinhado à esquerda. Pior: `currentAlign` nunca é reposto a
`ALIGN_LEFT` no fecho do bloco, por isso no dia em que o alinhamento passar a
ser usado, um parágrafo centrado "contamina" todos os seguintes sem `style`.

`STYLE_ITALIC` e `STYLE_BOLD_ITALIC` mapeiam para a fonte normal
(`getGFXFont`, `TextRenderer.cpp:120-126`): as famílias só têm Regular e Bold.
O itálico é a ênfase mais usada em ficção (pensamentos, estrangeirismos,
títulos de obras) e perde-se por completo.

Correcção: (a) usar `.align` no cálculo de `drawX` — o código já faz isso
para cabeçalhos, é generalizar; repor `currentAlign`/`currentIndent` no
fecho de `</p>`/`</div>`; (b) gerar as variantes Italic a 9/12/18 pt para a
família por omissão (mesmo pipeline dos `Fonts/*.cpp`; cada família completa
custa ~500 KB de fontes, ver `Fonts/README.md`, por isso só para uma ou duas
famílias, ou trocar Bold24 — quase não usado — por Italic). Enquanto não há
glifos itálicos, uma alternativa barata é sublinhar (`drawFastHLine` sob a
linha) — melhor do que nada.

### D3. `<br>` não produz mudança de linha

O parser acrescenta `"\n"` ao texto em `<br>` (`EpubLoader.cpp:643-644`), mas
o ciclo de quebra de linha do renderizador trata `\n` com `isspace()`
(`TextRenderer.cpp:228-231`), ou seja, como um espaço vulgar. Poesia,
moradas, versos de canções e diálogos com `<br/>` ficam todos numa linha
corrida. As substituições `"\n,"` → `","` etc. (`EpubLoader.cpp:686-689`)
são vestígios desta intenção nunca concluída.

Correcção (baixo esforço, no parser): ao encontrar `<br>`, fechar o nó de
texto actual e abrir outro com `isBlockStart = true` mas uma *flag* nova
(`softBreak`) para o renderizador não acrescentar o espaçamento de parágrafo
(`y += 8`) nem o recuo de primeira linha. É uma mudança só de dados, sem
tocar na quebra de palavras.

### D4. A hifenização portuguesa quase nunca dispara em prosa

Ordem de operações em `renderRichPageDynamic()`: primeiro decide-se que a
palavra não cabe e **passa-se para a linha seguinte** (`TextRenderer.cpp:236-
270`, "Word doesn't fit on this line"), e só depois se chama
`hyphenationPoints()`/`fitWordIntoLineHyphenated()` (`:304-306`) com
`pixelBudget` já igual à largura total da linha. Logo, a hifenização só
acontece quando a palavra é mais larga do que a coluna inteira (~60
caracteres a 9 pt): em português corrente, nunca. O design doc da v1.15.0
descreve exactamente este comportamento ("palavra demasiado larga para caber
numa linha só para ela"), por isso não é um bug de implementação, mas é uma
funcionalidade com efeito visível praticamente nulo, apesar de ter um
módulo e testes de host dedicados.

O que os leitores fazem (KOReader, CrossPoint, qualquer motor de texto): no
ponto em que a palavra não cabe no espaço restante da linha, tentar
primeiro um corte silábico que caiba **nesse resto**, e só se nenhum couber
passar a palavra inteira para a linha seguinte. É uma reordenação de ~15
linhas no ramo "Word doesn't fit": chamar `fitWordIntoLineHyphenated()` com
o `pixelBudget` do resto da linha antes do salto, e aceitar o resultado se
`fit.hyphen` for true. Ganha-se uma margem direita muito menos denteada, e o
módulo de hifenização passa a valer o que custou. Convém uma regra de
"não hifenizar duas linhas seguidas" e um mínimo de 3 letras de cada lado
para não ficar pior do que a versão sem hífenes.

### D5. Ao virar para trás no início de um capítulo, cai-se no início do anterior

`prevPage()` (`AppReader.cpp:1150-1171`) com `_pageHistory` vazio chama
`prevChapter()` e aterra na **primeira** página do capítulo anterior, não na
última. O próprio código admite-o ("For now, we go to the start of the
previous chapter", `:1164`). O mesmo acontece depois de retomar um livro:
`openBook()` repõe `_currentPagePointer` mas `_pageHistory` fica vazio, por
isso o primeiro "atrás" depois de ligar o dispositivo salta para o início do
capítulo, mesmo que se estivesse a meio. Para quem lê um parágrafo e quer
reler o anterior, é a acção mais frustrante do leitor.

Correcção: a infra-estrutura já existe. `updateTotalPagesCount()` pagina um
capítulo inteiro com `draw=false` num renderizador à parte; basta uma função
`paginateUntil(chapter, pointer)` que corra `renderRichPageDynamic(draw=false)`
desde `{0,0}` acumulando cada início de página em `_pageHistory` até chegar
(ou passar) o ponteiro alvo. Um capítulo típico (10-30 páginas) pagina em
poucas dezenas de ms, tolerável dentro de um virar de página, e em
`openBook()` reconstrói também o histórico correcto para a posição retomada.
O mesmo mecanismo dá o "páginas restantes no capítulo" do rodapé
(item 4 da avaliação KOReader).

### D6. Cada livro é analisado duas vezes ao abrir: contagem e índice em paralelo

Ao abrir um livro sem cache, `update()` corre `updateTotalPagesCount()` **e**
`updateTocBuild()` (`AppReader.cpp:1435-1441`) na mesma passagem. A contagem
chama `getChapterContentRich(k)` (`:854`); o índice chama `getChapterTitle(k)`
(`:954`), que internamente volta a chamar `getChapterContentRich(k)`
(`EpubLoader.cpp:736`). Ou seja, todo o EPUB é descomprimido e analisado
**duas vezes** por abertura, e ambos os varrimentos concorrem pelo mesmo
descritor de ZIP global (`zipFd`) e pelo mesmo `_epubLoader` — funciona
porque tudo corre na tarefa do loop, mas é o dobro do trabalho e o dobro do
tempo em que os botões respondem mais devagar.

Além disso, o orçamento `TOTAL_PAGES_BUDGET_MS = 15` (`AppReader.h:141`) é
verificado só **entre** capítulos: a unidade mínima de trabalho é "ler +
descomprimir + analisar um capítulo inteiro", que num capítulo de 100 KB
custa centenas de ms. O orçamento nominal de 15 ms é enganador; na prática
cada scanner activo bloqueia o loop uma vez por capítulo.

Correcção: unificar num só varrimento de capítulos que faça as três coisas
(contar páginas, extrair o título do primeiro cabeçalho, medir o comprimento
para "ir para %") a partir do mesmo `_countChapterContent` já analisado.
`getChapterTitle()` passa a ser uma função sobre um `vector<ContentNode>`
(pura, testável) em vez de reabrir o capítulo. O "ir para %" pendente também
pode reutilizar o vector de comprimentos que esse varrimento deixa em cache
(`PageCountStore` já persiste por livro e por fonte; os comprimentos de texto
são independentes da fonte, por isso cabem num store mais simples).

### D7. Páginas em branco no fim de um livro com itens finais vazios

`nextPage()` no fim de um capítulo (`AppReader.cpp:1138-1145`) faz
`_globalPageNumber++` e `loadChapter(_currentChapter + 1)`. Se todos os
capítulos seguintes forem vazios (páginas de nav, créditos finais, imagens
sem texto — frequente em EPUB comerciais), `loadChapter()` não encontra
conteúdo, deixa `_currentRichContent` vazio e `_currentChapter =
originalIndex`. O ecrã mostra uma página em branco e cada "Next" repete o
ciclo: mais uma página em branco, mais um `_globalPageNumber++`, até esgotar
a spine. O contador do rodapé ultrapassa o total contado e o livro "termina"
numa sequência de páginas vazias.

Correcção: `loadChapter()` devolver `bool` (encontrou conteúdo) e, em
`nextPage()`, não avançar (nem incrementar página, nem gravar) quando devolve
false; opcionalmente mostrar "Fim do livro" no rodapé.

### D8. Heurística "número curto = título de capítulo" é demasiado agressiva

Qualquer nó de texto com 1-3 dígitos que inicie um bloco vira `STYLE_HEADER1`
(`EpubLoader.cpp:702-711`): fonte Bold 24 pt, centrado, 30 px antes e 25
depois. Apanha números de capítulo em `<p>` (o caso pretendido) mas também
marcadores de nota de rodapé isolados, números de página de índices, anos
numa cronologia, numeração de versos. Não há maneira de o utilizador desligar
isto. Sugestão: só promover quando o nó for o **primeiro** do capítulo (ou
estiver entre os 3 primeiros) — é aí que os números de capítulo vivem.

### D9. Comentários HTML e CDATA não são tratados

O tokenizador trata `<!-- ... -->` como uma *tag* que termina no primeiro
`>` (`EpubLoader.cpp:550`). Um comentário com `>` lá dentro, ou com marcação
(`<!-- <p>rascunho</p> -->`, comum em EPUB produzidos à mão ou por Sigil)
despeja o resto do comentário como texto visível. Mesma coisa para `<![CDATA[`.
Correcção de 10 linhas: ao ver `<!--`, saltar até `-->`; ao ver `<![CDATA[`,
saltar até `]]>`.

### D10. Estado de estilo não é reposto no fecho de bloco

- A pilha de estilos só despeja `HEADER1/2/3` em `</p>`/`</div>`
  (`EpubLoader.cpp:603-606`), nunca `HEADER4` nem `BOLD`/`ITALIC`: um `<b>`
  sem `</b>` (HTML tolerante, mas ocorre) deixa **o resto do capítulo** a
  negrito.
- `currentIndent`/`currentAlign` só se repõem quando se cria um nó, não no
  fecho do bloco (ver D2).

Correcção: em `</p>`/`</div>`/`</hN>`/`</li>`, repor a pilha ao tamanho que
tinha quando o bloco abriu (guardar o tamanho numa pilha de blocos) e repor
`currentAlign = ALIGN_LEFT`, `currentIndent = 0`.

### D11. Rodapé de leitura com a fonte de sistema de 6x8 px e em inglês

`drawReading()` desenha "Page N of M" com `setFont(NULL)` (`AppReader.cpp:1414-
1425`), a fonte bitmap de 6x8 px do Adafruit GFX — a única string do
dispositivo nessa fonte, e a mais pequena do ecrã. A biblioteca também está em
inglês ("Library", "Back to Menu", "No books found.", "Next: Move | Hold:
Open", `:1263-1369`) enquanto o web UI, os comentários e o público-alvo são em
português. Por resolver há muito; agora que o índice de capítulos existe em
cache (`ChapterTocStore`), o rodapé pode mostrar "Cap. 7 · Título · 42/310 ·
14%" em `FreeSans9pt8b` sem custo de E/S (o título já está em memória
depois do `updateTocBuild`).

### D12. Menores

- `readFileFromZip()` faz `str += buffer` com terminador em `\0`
  (`EpubLoader.cpp:327-328`): um byte nulo dentro do XHTML (raro, mas visto em
  ficheiros UTF-16 mal convertidos) trunca o capítulo em silêncio. Usar
  `str.concat(buffer, bytesRead)`.
- `parseHtmlToRichContent()` constrói `currentText` carácter a carácter com
  `String::operator+=` (`:653`) e lê com `charAt(i)`: cada `+=` pode realocar.
  Em capítulos de 100+ KB são dezenas de milhares de realocações. Fazer
  `currentText.reserve()` por bloco, ou acumular índices [início, fim) e
  fazer um único `substring` por nó.
- `getFontData()` (usado para a capa) não verifica o retorno de
  `readCurrentFile()` (`:262`): uma leitura curta entrega bytes não
  inicializados ao JPEGDEC. Falha em silêncio, mas devia devolver `nullptr`.
- `extractAttribute()` procura `attr="` em qualquer ponto da *tag*
  (`:276`), sem verificar o que vem antes: `type` casaria com
  `media-type="` e `href` com `data-href="`. Hoje nenhum dos chamadores cai
  nisso (as *tags* onde se procura `type` não têm `media-type`), mas é uma
  armadilha latente para o próximo atributo. Acrescentar a verificação de
  que o carácter anterior é espaço.
- `extractIndentFromStyle()` usa `toInt()` em "1.5em" → 1 (`:427`);
  `atof` resolve.
- `_currentPageRender` só é escrito em `drawReading()` e lido em `nextPage()`;
  o comentário "Dynamic Pagination" em `AppReader.h` já não descreve o fluxo
  actual (medir com `draw=false`, depois desenhar). Vale um comentário de
  3 linhas a explicar a dupla passagem.
- O comentário de `updateTotalPagesCount()` (`AppReader.cpp:823-826`) diz que
  o standby por KEY2 vai directo para `esp_deep_sleep_start()` sem
  `closeBook()`; já não é verdade — `InputMgr::enterStandby()` chama
  `current->stop()` primeiro. Só o *idle timeout* (`BatteryMgr.cpp:165`)
  salta o `stop()`, e nesse caso o progresso já foi gravado pelos 4 s de
  quietude. Corrigir o comentário para não induzir em erro.

## Estrutura e manutenção

### M1. `AppReader.cpp` acumula cinco responsabilidades

1.5k linhas com: biblioteca (scan, scroll, desenho, títulos, capas),
leitura (abrir, paginar, desenhar), progresso (gravar, retomar), três
scanners de fundo (contagem, índice, "ir para %") e ligação a sete *stores*.
Cada funcionalidade nova (v1.14, v1.17, v1.18, v1.19) acrescentou mais um
bloco `_xxxActive/_xxxChapter/startXxx/updateXxx` à classe. Proposta, sem
mudar comportamento:

- `LibraryView` (scan, ordem, títulos, capas, desenho da lista) e
  `ReadingView` (livro aberto, paginação, rodapé) como classes separadas,
  `AppReader` a fazer só a máquina de estados e o *routing* de input;
- os três scanners de fundo fundidos num `BookIndexer` com uma única
  passagem por capítulo (ver D6) e uma interface `step(budgetMs)`;
- as constantes de layout repetidas (`HEADER_H`, `BACK_ITEM_HEIGHT`,
  `ITEM_HEIGHT` aparecem em `libraryItemRect()`, `libraryItemsPerPage()` e
  `drawLibrary()`) num só `struct LibraryLayout`.

### M2. `TextRenderer::renderRichPageDynamic()` é uma função de 200 linhas com 6 níveis de ciclo

Faz medição, quebra de palavra, hifenização, cache de linhas, cabeçalhos,
marcadores de lista e desenho no mesmo corpo. É difícil de alterar com
confiança (D1-D4 tocam-lhe todos) e impossível de testar em host por depender
de `Book32Display`. Separação sugerida:

- `LineBreaker` (puro: texto + tabela de larguras + largura útil →
  `vector<Line{start,len,width,hyphen}>`), a viver em `lib/Book32_Core/`
  como `WordFitLogic.h` — testável com um caso por regra de D3/D4;
- `PageComposer` (puro: sequência de nós + alturas → páginas de linhas
  posicionadas, devolve o `PagePointer` seguinte);
- `TextRenderer` fica só a desenhar linhas posicionadas com a fonte certa.

A medição por `_gfxCharWidths[256]` (xAdvance por byte) é a correcta para
fontes GFX e deve ficar como está; só precisa de ser injectada nos módulos
puros como já é feito em `fitWordIntoLine()`.

### M3. O parser de HTML por `indexOf` chegou ao limite

A avaliação anterior já o descreveu; o que esta acrescenta é que D2, D9 e
D10 são todos sintomas do mesmo desenho — uma máquina de estados sem pilha
de elementos. Não é preciso um DOM: um *tokenizer* SAX-like (evento
`startTag(name, attrs)`, `endTag(name)`, `text(bytes)`) com uma pilha de
elementos abertos resolve os três de uma vez e é o que qualquer CSS parcial
(item 4 da avaliação anterior) vai precisar de qualquer maneira. ~250 linhas,
pode viver em `Book32_Core` como código puro sobre `const char*` e ser
testado em host com os EPUB reais que causaram bugs passados (v1.3.13,
v1.3.14 — os títulos gigantes por classe CSS).

### M4. Strings de UI em inglês espalhadas pelo código

Ver D11. Juntar as strings do leitor num `ReaderStrings.h` (PT por omissão)
custa pouco e permite corrigir a língua da biblioteca e do rodapé de uma vez.

## Testabilidade

O projecto tem 15 testes de host que cobrem a lógica pura extraída para
`lib/Book32_Core/*Logic.h`, e nenhum para o que mais falha: o parser de
HTML e a quebra de linha. Ambos dependem de `Arduino.h` (`String`,
`yield()`) e de `Book32Display`. Duas vias, por ordem de custo:

1. **Stub mínimo de `Arduino.h`/`WString.h` para host** (~150 linhas em
   `tools/tests/stubs/`): `String` sobre `std::string` com `indexOf`,
   `substring`, `replace`, `charAt`, `toLowerCase`, `trim`, `startsWith`,
   `endsWith`, `toInt`; `yield()` vazio. Chega para compilar
   `parseHtmlToRichContent()` e `decodeHtmlEntities()` em host e escrever
   testes para D3, D8, D9, D10 com fragmentos de XHTML reais. O CI já compila
   e corre `tools/tests/test_*.cpp` com `g++`; só precisa de `-I
   tools/tests/stubs`.
2. **Extrair `LineBreaker`/`PageComposer`** (M2) para código puro; a partir
   daí D4 e D5 testam-se com uma tabela de larguras sintética (todos os
   glifos a 10 px), sem fontes nem ecrã.

Sem (1) ou (2), cada correcção ao parser ou à paginação continua a só poder
ser verificada num dispositivo, com um EPUB, à mão — e o `TODO.txt` já tem
quatro lotes de "a verificar no dispositivo" acumulados.

## Resumo por prioridade

| # | Item | Esforço | Impacto | Ref. |
|---|---|---|---|---|
| 1 | Hifenização no fim de linha (não só palavras maiores que a coluna) | Baixo | Alto (tipografia em todas as páginas) | D4 |
| 2 | Histórico de páginas reconstruído: "atrás" aterra na última página do capítulo anterior e funciona depois de retomar | Médio | Alto (UX do botão mais usado) | D5 |
| 3 | `<br>` como quebra de linha; `text-align` respeitado; estado reposto no fecho de bloco | Baixo | Médio-Alto | D3, D2, D10 |
| 4 | Um só varrimento de fundo por capítulo (contagem + título + comprimento) | Médio | Médio (abertura mais leve, botões mais responsivos) | D6 |
| 5 | Tabelas: desenhar como texto ou remover `parseTable()` | Baixo | Médio | D1 |
| 6 | Fim de livro sem páginas em branco em cascata | Baixo | Médio | D7 |
| 7 | Rodapé em PT com capítulo e %, fonte legível; biblioteca em PT | Baixo | Médio | D11, M4 |
| 8 | Itálico (variantes Italic para a família por omissão, ou sublinhado) | Médio (flash) | Médio | D2 |
| 9 | Comentários/CDATA no parser; heurística numérica só no início do capítulo | Baixo | Baixo-Médio | D9, D8 |
| 10 | Stub Arduino para host + testes do parser | Médio | Alto para manutenção | Testabilidade |
| 11 | Separar `LineBreaker`/`PageComposer` do `TextRenderer` | Alto | Alto para manutenção; pré-requisito de CSS/justificação | M2 |
| 12 | Separar `LibraryView`/`ReadingView`/`BookIndexer` de `AppReader` | Alto | Manutenção | M1 |
| 13 | Tokenizer SAX com pilha de elementos | Alto | Pré-requisito de CSS parcial | M3 |
| 14 | Menores (nulo no ZIP, `reserve`, `readCurrentFile`, `extractAttribute`, comentários desactualizados) | Baixo | Baixo | D12 |

**Sugestão de sequência**: 1 → 3 → 6 → 5 (quatro alterações pequenas ao
renderizador/parser, cada uma com efeito visível e verificável num livro
real), depois 10 (para que o que vier a seguir tenha rede), depois 2 e 4
(médios, ambos sobre a paginação com `draw=false`), e só então os
refactors 11-13 se o suporte a CSS parcial avançar. Os itens 1, 3, 5 e 6
cabem numa única versão de manutenção sem tocar em formatos persistidos
(`reader_progress.json`, `PageCountStore`) — os ponteiros continuam a ser
`{capítulo, nó, offset}` e os nós não mudam de índice com nenhuma destas
alterações, excepto D3 (`<br>` cria nós novos), que desloca `nodeIndex`
guardados em livros com `<br>`: convém acompanhar de um *bump* de versão no
`PageCountStore` (a contagem muda) e aceitar que uma posição guardada num
livro com muitos `<br>` pode aterrar umas linhas ao lado.
