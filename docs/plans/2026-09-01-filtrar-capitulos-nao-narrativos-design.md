# Filtrar capítulos não-narrativos do índice — design notes

Implementa a lacuna #2/#3 de
[2026-08-31-avaliacao-gestao-epub.md](2026-08-31-avaliacao-gestao-epub.md):
`getChapterCount()` conta cada `<itemref>` da spine como um "capítulo", o que
inclui a página de nav (`nav.xhtml`), a página de rosto, a página de créditos
e afins — todas aparecem no índice web (`/api/toc`, ver
[2026-08-31-toc-titulos-capitulo-design.md](2026-08-31-toc-titulos-capitulo-design.md))
com o rótulo genérico "Capítulo N", confundindo-se com conteúdo de leitura
real.

## A restrição que decide todo o design: os índices de capítulo nunca mudam

Antes de desenhar isto, confirmei onde `chapter` (= índice na spine) é usado
como coordenada persistida, não só como posição de iteração:

- `ProgressStore` grava `{chapter, nodeIndex, charOffset, globalPage}` por
  livro em `/reader_progress.json` (EbookFS) — é a posição de leitura, entra
  no export/import entre dispositivos (`fillExportJson`/`applyImportedJson`).
- `PageCountStore` grava um `PageCountCheckpoint{chapter, pagesSoFar}` como
  ponto de retoma da contagem total de páginas.
- `GoToPercentLogic::resolvePercentTarget` assume um `chapterLengths[i]` por
  capítulo, em ordem de livro, e devolve um `chapterIndex` que
  `AppReader::applyPercentJump` usa directamente como índice de spine.
- `ChapterTocStore`/`/api/toc` já indexam por posição no array = índice de
  spine (ver `WebMgr.cpp`, `AppReader::applyChapterJump`).

Ou seja: `chapter` é a moeda comum entre quatro subsistemas diferentes,
persistida em disco e sincronizada entre dispositivos via export/import.
**Renumerar a spine para excluir itens não-narrativos partiria
silenciosamente qualquer posição de leitura já guardada** num dispositivo
real assim que a firmware actualizasse — "capítulo 5" passaria a apontar
para um capítulo diferente do que apontava ontem. Isto descarta de vez
qualquer abordagem que mexa em `getChapterCount()` ou na ordem/índices da
`spine` do `EpubLoader`.

**Decisão de design:** a classificação narrativo/não-narrativo é uma camada
puramente informativa por cima dos índices existentes — nunca os
substitui, remove, nem renumera. `getChapterCount()` continua a devolver
`spine.size()` exactamente como antes desta funcionalidade.

## Sinal de classificação: `<guide>` do OPF (EPUB2)

Sem `toc.ncx`/`nav.xhtml`/`epub:type` lidos em lado nenhum do `EpubLoader`
(confirmado por grep antes de começar — ver a avaliação de gestão de EPUB),
a fonte mais barata e já ao alcance do parsing existente é o `<guide>` do
OPF: uma lista de `<reference type="..." href="...">` que a maioria dos
EPUB2 — e muitos EPUB3 gerados por ferramentas como o Calibre, por
compatibilidade — ainda inclui.

`EpubLoader::parseGuide()` (chamado no fim de `parseOpf()`, depois de a
spine estar completa) lê esse bloco e marca `nonNarrativeChapters[i] = true`
para cada item da spine cujo href apareça numa `<reference>` com um dos
tipos abaixo:

```
cover, toc, title-page, copyright-page, dedication, index, glossary,
bibliography, colophon, acknowledgements, loi, lot, notes
```

Lista deliberadamente conservadora: fica de fora tudo o que costuma ter
texto que o leitor quer mesmo ler — `preface`, `foreword`, `epigraph`,
`text` — para não marcar como "não-narrativo" um capítulo real por engano.
Um EPUB sem `<guide>` (comum em EPUB3 puro) não marca nada; todos os
capítulos ficam narrativos, o comportamento de sempre.

`EpubLoader::isChapterNarrative(int index)` expõe o resultado: `true` por
omissão (índice fora do intervalo, ou sem `<guide>` nenhum).

### Por que não o `nav.xhtml`/landmarks do EPUB3 nesta fase

O `nav.xhtml` com `<nav epub:type="landmarks">` é a forma "actual" do
standard (o `<guide>` está formalmente descontinuado no EPUB3), mas exige
abrir e analisar *outro* documento XHTML do ZIP — mais um ponto de I/O e
outro parser, para o mesmo resultado que o `<guide>` já dá de graça na
maioria dos EPUB reais. Fica documentado como extensão futura para EPUB3
puro sem `<guide>`, não implementado aqui.

## Onde persistir: `ChapterNarrativeStore`, não um campo a mais no `ChapterTocStore`

`ChapterTocStore` já grava `/chapter_toc.json` em dispositivos reais como
`{"<livro>": ["Título 1", "Título 2", ...]}` — um array de strings. Mudar
isso para um array de objectos (`{"t":"...","n":true}`) partiria em
silêncio qualquer ficheiro já gravado por firmware anterior: `deserializeJson`
não erra com um formato diferente, só devolve valores errados
(`JsonVariant::as<String>()` sobre um objecto não é o título que lá estava).

Em vez disso, `ChapterNarrativeStore` é um ficheiro novo e paralelo
(`/chapter_narrative.json`, `{"<livro>": [true, false, ...]}`), mesma forma
de sempre neste código para "um store espelha outro por um campo extra"
(`GoToChapterStore` espelha o `GoToPercentStore` da mesma maneira). Uma
adição puramente aditiva: nada que já existe muda de formato.

Como o `<guide>` já foi lido inteiro quando o EPUB abriu
(`EpubLoader::open` → `parseOpf` → `parseGuide`, sem custo de I/O extra por
capítulo), `AppReader::updateTocBuild()` persiste a classificação completa
no mesmo momento em que persiste os títulos — uma escrita, não uma por
capítulo, sem precisar do orçamento por passagem que o loop de títulos usa
(esse sim custa I/O e parsing por capítulo, `getChapterTitle`).

## `/api/toc` e a interface web

`GET /api/toc` ganha `"narrative":true/false` por entrada, lido do
`ChapterNarrativeStore` ao lado do `ChapterTocStore` — aditivo, o campo
`title`/`index` de sempre não muda. O cartão "Índice" da web UI
(`data/script.js::renderToc`) usa isto só para reduzir ênfase visual
(`opacity`, uma etiqueta "(não narrativo)") nas entradas não-narrativas —
continuam clicáveis e "Ir" continua a funcionar tal e qual: a classificação
é sobre o que mostrar com destaque, nunca sobre remover a possibilidade de
lá saltar.

## Limitação conhecida: livros já indexados antes desta alteração

`AppReader::startTocBuild()` só reconstrói o índice quando o `ChapterTocStore`
não tem entrada válida para o livro, ou o número de títulos guardados não
bate com `getChapterCount()`. Um livro já aberto e indexado por uma firmware
anterior continua com essa condição satisfeita — `updateTocBuild()` nunca
corre outra vez para ele, por isso o `ChapterNarrativeStore` fica sem
entrada e `/api/toc` devolve `narrative:true` para tudo (o mesmo "mostra
tudo" de sempre, sem marcação nenhuma, não um erro).

Decisão deliberada: **não** forçar a condição de `startTocBuild()` a exigir
também uma entrada em `ChapterNarrativeStore`, que obrigaria a reconstruir o
índice completo (reler todos os capítulos com `getChapterContentRich` para
os títulos) na primeira abertura de cada livro já indexado depois desta
actualização — um custo de CPU/SD real, ainda que único, só para
retroactivamente ganhar uma etiqueta visual. Um livro volta a ganhar
classificação assim que for reaberto depois de o `.epub` mudar (substituição
do ficheiro invalida o cache pelo mesmo mecanismo de sempre) ou se o
`ChapterTocStore`/`ChapterNarrativeStore` for limpo manualmente.

## Fora de âmbito nesta alteração

- **Fallback `nav.xhtml`/landmarks (EPUB3 sem `<guide>`)** — ver secção
  acima.
- **`nextChapter()`/`prevChapter()` no dispositivo saltarem páginas
  não-narrativas** — hoje só saltam páginas genuinamente vazias
  (`getChapterContentRich(...).size() > 0`, ver a limpeza de
  `getChapterContent()`). Estender isso à classificação do `<guide>` é uma
  mudança de comportamento de navegação visível no leitor físico, sem forma
  de a verificar num ecrã e-ink real nesta sessão — fica como extensão
  futura a validar em hardware antes de arriscar uma regressão.
- **`getChapterCount()`, `ProgressStore`, `PageCountStore`,
  `GoToPercentLogic`** — intocados, de propósito, pela razão explicada
  acima.

## Ficheiros novos/alterados

- `lib/Apps/AppReader/EpubLoader.{h,cpp}` — `parseGuide()`,
  `isChapterNarrative()`, campo `nonNarrativeChapters`.
- `lib/Book32_Core/ChapterNarrativeStore.{h,cpp}` — novo, espelha
  `ChapterTocStore`.
- `lib/Apps/AppReader/AppReader.cpp` — `updateTocBuild()` persiste a
  classificação junto dos títulos; `reconcile()` poda o novo store também.
- `lib/Book32_Web/WebMgr.cpp` — `/api/toc` ganha `"narrative"`.
- `data/script.js`, `data/style.css` — `renderToc()` mostra as entradas
  não-narrativas com menos destaque.

## Por verificar no dispositivo

`pio run` não correu neste ambiente (sem toolchain ESP32 disponível); só
revisão manual do diff. Ver a secção correspondente do `TODO.txt`: um EPUB
real com `<guide>` (a maioria dos ficheiros gerados por Calibre/Sigil tem
um) precisa de mostrar as entradas de capa/rosto/créditos visualmente
diferentes no cartão "Índice", continuar a permitir saltar para lá, e um
EPUB sem `<guide>` nenhum precisa de continuar exactamente como estava
antes desta alteração.
