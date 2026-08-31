# Avaliação da gestão de EPUB no leitor (parsing, capítulos, imagens)

Data: 2026-08-31
Estado: avaliação — sem alterações de código associadas

## Contexto

Pedido: avaliar como o Book32 lê e reproduz EPUB — parsing do ficheiro,
paginação/renderização, tratamento de capítulos e de imagens — e identificar
lacunas concretas para melhorar a gestão de EPUB. Cobre três ficheiros
centrais em `lib/Apps/AppReader/`: `EpubLoader.{h,cpp}` (parsing),
`TextRenderer.{h,cpp}` (layout/paginação) e `AppReader.cpp` (orquestração,
biblioteca, progresso).

## Como o EPUB é lido hoje

`EpubLoader::open()` (`EpubLoader.cpp:47`) abre o `.epub` como ZIP via
`unzipLIB`/ROM miniz, lê `META-INF/container.xml` para localizar o `.opf`
(`parseContainer`, linha 141) e depois faz o parsing do `.opf`
(`parseOpf`, linha 152).

Ponto central: **não há parser XML real**. Tudo — `container.xml`, o `.opf`
e o HTML de cada capítulo — é processado com `indexOf`/`substring` à procura
de strings literais (`extractAttribute`, `extractMetadata`,
`parseHtmlToRichContent`). Isto tem consequências:

- Um comentário XML que contenha `<item` ou `<itemref` (ex.:
  `<!-- <item id="old"/> -->`) confundiria o scanner do manifest/spine,
  porque a procura é por substring, não por uma árvore real.
- CDATA, namespaces com prefixos variáveis, ou atributos com aspas mistas
  são tratados de forma ad-hoc (`extractAttribute` tenta `"` depois `'`, mas
  não sabe lidar com XML genuinamente bem formado mas incomum).
- Já há proteções documentadas contra ficheiros truncados/malformados: um
  `<item` sem `>` a fechar interrompe o ciclo em vez de entrar em loop
  infinito (comentário em `EpubLoader.cpp:167-171`, mesmo padrão para
  `<itemref` em `205`). É um bom precedente a seguir em qualquer parsing
  novo — falhar em silêncio, nunca travar o dispositivo com um ficheiro do
  utilizador.

**Dois parsers de HTML independentes e duplicados**: `getChapterContent()`
(linha 64, texto simples) e `parseHtmlToRichContent()` (linha 471, nós
ricos). Ambos fazem a sua própria limpeza de entidades/encoding
(`¶Ç…` → aspas/travessões, sequências UTF-8 de aspas tipográficas), com o
mesmo bloco de substituições copiado duas vezes (linhas 111-118 e 614-619).
`getChapterContent()` só é hoje usado por `AppReader::prevChapter()`
(`AppReader.cpp:918`) para testar se um capítulo tem conteúdo antes de
recuar — podia ser substituído por `getChapterContentRich(...).size() > 0`,
eliminando um parser inteiro e a duplicação de correcções de encoding.

**Limite de tamanho por ficheiro**: `readFileFromZip` trunca a
`BOOK32_MAX_ZIP_TEXT_BYTES` (256 KB, linha 264) qualquer entrada do ZIP maior
que isso, com o motivo bem documentado (heap interna limitada). Um capítulo
gigante fica incompleto mas o dispositivo não morre — comportamento correcto
para um leitor embutido, mas vale ter presente que um EPUB com capítulos
muito grandes (ex.: um único ficheiro XHTML com o livro inteiro) perde texto
silenciosamente já do lado do parsing, antes até de chegar à paginação.

**Codificação**: todo o texto rico passa por `FontMgr::utf8ToLatin1()`
(`EpubLoader.cpp:635`) porque as fontes só têm glifos Latin-1 Supplement
(0x20-0xFF, ver `TextRenderer.h:82-85`). Correcto para o público-alvo
(português europeu), mas é uma limitação explícita: caracteres fora de
Latin-1 (cirílico, grego, a maioria dos emoji, aspas tipográficas exóticas)
degradam-se silenciosamente.

## Como os "capítulos" são geridos

`getChapterCount()` devolve simplesmente `spine.size()` — o número de
`<itemref>` do `<spine>` do `.opf` (`EpubLoader.cpp:62`, `parseOpf`
linha 197-213). Ou seja, **capítulo = documento XHTML da spine, 1:1**, sem
qualquer relação com a estrutura lógica do livro.

Lacunas concretas encontradas (confirmadas por grep — zero ocorrências de
`toc.ncx`, `nav.xhtml`, `epub:type` ou `NCX` em todo o `lib/`):

1. **Sem tabela de conteúdos.** Nem o `toc.ncx` (EPUB2) nem o
   `<nav epub:type="toc">` do `nav.xhtml` (EPUB3) são lidos. Não há como
   saber o título real de um capítulo, nem construir um ecrã de "ir para
   capítulo" — o rodapé de leitura mostra apenas "Page N of M"
   (`AppReader.cpp:1141`), nunca o nome do capítulo.
2. **Contagem de "capítulos" desalinhada com o livro.** Muitos EPUB dividem
   a spine em granularidade fina (uma secção, uma página de rosto, uma
   página de créditos, a própria página de navegação = um item cada), pelo
   que `getChapterCount()` não corresponde ao número de capítulos que um
   leitor reconheceria. A página de nav (`nav.xhtml`) e a página de rosto
   entram na contagem de páginas/percentagem como se fossem conteúdo de
   leitura normal.
3. **Sem navegação por capítulo na UI.** `handleInput()` só liga
   `INPUT_NEXT`/`INPUT_PREV` a `nextPage()`/`prevPage()`
   (`AppReader.cpp:435-436`). Existem métodos privados `nextChapter()`
   (linha 908) e `prevChapter()` (linha 913), mas **`nextChapter()` nunca é
   chamado em lado nenhum** — código morto. `prevChapter()` só dispara
   implicitamente quando se recua para trás da primeira página de um
   capítulo (dentro de `prevPage()`, linha 902). Não há um comando dedicado
   "saltar capítulo" nem uma lista de capítulos navegável.

## Como as imagens são tratadas

**Não são tratadas — são sempre descartadas.** Confirmado nos dois pontos de
parsing de HTML:

- `getChapterContent()` salta explicitamente `img`, `svg`, `figure`, `image`
  (`EpubLoader.cpp:92-98`).
- `parseHtmlToRichContent()` faz o mesmo: `img`/`image` são ignorados sem
  produzir nó nenhum (linha 582-584), e `figure`/`svg` têm o conteúdo inteiro
  saltado até à tag de fecho (linha 577-580).

O `ContentType` (`EpubLoader.h:70-73`) só tem `CONTENT_TEXT` e
`CONTENT_TABLE` — não existe um terceiro tipo para imagem em lado nenhum da
cadeia (parsing → `TextRenderer::renderRichPageDynamic` → desenho). Mesmo um
placeholder textual tipo "[imagem]" não é gerado; o `<img>` desaparece sem
deixar rasto no texto renderizado.

**Capas de livro**: a biblioteca (`AppReader::drawBookTile`,
`AppReader.cpp:371-388`) desenha sempre o mesmo ícone vectorial genérico
("página de livro" com linhas horizontais) para todos os títulos — nunca lê
a capa real do EPUB. O `EpubLoader` nem sequer procura a entrada de capa no
manifest (`<meta name="cover" content="...">` do OPF2, ou
`<item properties="cover-image">` do OPF3): só recolhe fontes do manifest
(`parseOpf`, linha 180). Não há, portanto, extracção de capa nenhuma hoje.

Curiosamente, existe um **pipeline paralelo já morto** com suporte de capa
melhor do que o caminho ativo: `lib/Apps/AppReader/B32Reader.{h,cpp}` +
`tools/converter/` (Node/Puppeteer/sharp) implementam um formato proprietário
`.b32` que pré-renderiza páginas para bitmap fora do dispositivo, incluindo
uma capa real (`B32Reader::hasCover()`/`getCover()`,
`B32Reader.h:27-31`). Este código compila (está em `lib/`), mas
**`B32Reader` nunca é instanciado nem referenciado em `AppReader.cpp`, nem em
mais nenhum ponto da app** — confirmado por grep. É código morto no firmware
atual; o fluxo real é sempre EPUB → `EpubLoader` → `TextRenderer`. Vale
decidir formalmente: remover (o formato `.b32` e o conversor deixaram de
estar ligados a nada), ou documentar como "arquitetura alternativa
descontinuada" para não confundir quem ler o código.

**Viabilidade técnica de imagens**: o driver de ecrã já suporta
`drawBitmap()` (usado para ícones do menu principal e da actualização OTA,
`AppMainMenu.cpp:422` e `:449`), portanto o obstáculo a mostrar imagens não é
de hardware/driver — é puramente a ausência de qualquer extracção/decodificação
de imagem (JPEG/PNG dentro do EPUB) e de um caminho de conversão para bitmap
mono-e-ink no firmware ou no upload.

## Como a paginação/renderização funciona

O modelo é razoável e bem pensado: **paginação dinâmica por
posição de conteúdo**, não por "ficheiro de página" persistido.
`PagePointer{nodeIndex, charOffset}` (`TextRenderer.h:29-32`) identifica uma
posição dentro do vector de `ContentNode` de um capítulo;
`renderRichPageDynamic()` mede/desenha a partir daí até encher o ecrã e
devolve onde a página seguinte começa. Isto permite mudar tamanho de letra
ou família em tempo real sem invalidar posições guardadas (o ponteiro é
independente de fonte — só a paginação em si é recalculada,
`AppReader::applyFontSize`/`applyFontFamily`, linhas 1177-1212).

Como não há um mapa de paginação persistido, o número total de páginas e o
"ir para %" são aproximados por **varrimentos em segundo plano, em fatias
limitadas por orçamento de tempo** (`TOTAL_PAGES_BUDGET_MS = 15`ms):
`startTotalPagesCounting`/`updateTotalPagesCount` (linhas 627-719) e
`startPercentSeek`/`updatePercentSeek` (linhas 742-815), com checkpoint por
capítulo para sobreviver a um standby a meio da contagem. É um desenho
cuidadoso para o hardware (ESP32-S3, sem SD, paginação não pode bloquear a
UI), mas tem o efeito colateral já descrito: como cada item da spine conta
como "capítulo" sem filtragem, páginas de rosto/créditos/nav entram na
contagem total.

**Estilo suportado**: apenas atributos inline `style="text-align:..."` e
`text-indent:...` (`getAlignFromStyle`/`extractIndentFromStyle`, linhas
315-392), negrito/itálico (`b`/`strong`/`i`/`em`), `h1`-`h6` (mas
`getStyleFromTag`, linha 304-313, só mapeia `h1`-`h4`; `h5`/`h6` acabam com
`STYLE_NORMAL` embora continuem a contar como início de bloco — inconsistência
menor), listas (`li` → marcador "• ", sem numeração para `<ol>`, sem níveis
de indentação aninhados), e tabelas básicas (sem fusão visual de
`colspan`/`rowspan`, só o atributo é lido). **Não há parsing de CSS externo
nem embutido** (`<link rel="stylesheet">`, `<style>` no `<head>` é
explicitamente ignorado, linha 99/577) — só o que estiver directamente no
atributo `style=` do próprio elemento é respeitado. Como a maioria dos EPUB
comerciais define tipografia (recuos, alinhamentos, classes de capítulo) na
folha de estilos e não inline, uma fracção significativa da formatação
pretendida pelo editor perde-se hoje, mesmo sem imagens.

## Resumo — lacunas por prioridade

| # | Item | Esforço | Impacto | Nota |
|---|---|---|---|---|
| 1 | Capa real do EPUB na biblioteca | Médio | Alto | Ler `cover-image`/`meta cover` do manifest, descodificar JPEG/PNG e converter para bitmap mono; `drawBitmap` já existe. `B32Reader` morto já mostra a forma (`hasCover`/`getCover`) a reaproveitar como referência, não como código a religar tal-e-qual (formato `.b32` é outro pipeline). |
| 2 | TOC real (`nav.xhtml`/`toc.ncx`) | Médio-Alto | Alto | Dá título de capítulo no rodapé de leitura e permite filtrar itens de nav/rosto da contagem de páginas. Pré-requisito natural para uma futura lista "ir para capítulo". |
| 3 | Filtrar itens não-narrativos da contagem de "capítulos" | Baixo (depende de #2) | Médio | `nav.xhtml`/páginas de rosto/créditos hoje contam como capítulo de leitura. |
| 4 | CSS externo/embutido (mesmo que parcial: classes comuns) | Alto | Médio-Alto | Maior impacto na fidelidade tipográfica do que imagens, para a maioria dos EPUB comerciais. |
| 5 | Placeholder para imagens no corpo do texto | Baixo | Baixo-Médio | Ex.: "[imagem]" com uma linha em branco reservada, em vez de desaparecer sem rasto — intermédio antes de suporte a imagem real. |
| 6 | Consolidar `getChapterContent()`/`parseHtmlToRichContent()` | Baixo | Manutenção | Elimina parser e correções de encoding duplicadas; `prevChapter()` passa a usar `getChapterContentRich().size()>0`. |
| 7 | Numeração de `<ol>` / indentação de listas aninhadas | Baixo | Baixo | Cosmético. |
| 8 | Decidir o destino de `B32Reader`/`tools/converter` | Baixo | Manutenção | Código morto no firmware actual; remover ou documentar como descontinuado evita confusão futura. |

**Maior valor por esforço**: capa real na biblioteca (#1) é a melhoria de
imagem mais visível e mais barata (o driver já suporta bitmap; falta só
decodificação + conversão), seguida da TOC real (#2), que resolve de raiz
tanto a ausência de nomes de capítulo como a contagem de páginas inflacionada
por itens de navegação/rosto (#3). O suporte a CSS externo (#4) é o item que
mais eleva a fidelidade geral de renderização mas é também o de maior
esforço — justifica um design doc próprio antes de arrancar, dado o parser
actual ser inteiramente baseado em `indexOf`/`substring` sem árvore DOM.
