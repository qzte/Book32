# Índice com títulos de capítulo — design notes (v1.18.0)

Implementa a lacuna #1 de
[2026-08-31-avaliacao-koreader.md](2026-08-31-avaliacao-koreader.md): TOC
com títulos de capítulo, reaproveitando a detecção de cabeçalhos que o
`EpubLoader` já faz ao analisar cada capítulo.

## O que já existia

`EpubLoader::parseHtmlToRichContent` já classifica `h1`-`h4` com estilo de
cabeçalho, e já promove blocos numéricos isolados e curtos (1-3 dígitos) a
`STYLE_HEADER1` como heurística de "número de capítulo"
(`EpubLoader.cpp:643-652` antes desta alteração). Isto já corria a cada
capítulo lido, mas o resultado nunca saía do fluxo de renderização — nada
guardava "qual foi o primeiro cabeçalho deste capítulo" como um título
utilizável.

## Extracção do título (EpubLoader::getChapterTitle)

Novo método em `EpubLoader`: obtém o conteúdo rico do capítulo (o mesmo
`getChapterContentRich` já usado para desenhar a página) e devolve o texto
do primeiro nó de texto com estilo `STYLE_HEADER1`-`STYLE_HEADER4`, cortado a
60 caracteres com reticências. Devolve `""` quando o capítulo não tem nenhum
cabeçalho detectável — o chamador decide o texto de recurso (`"Capítulo N"`,
por exemplo). Não introduz heurística nova nenhuma: usa exactamente os
mesmos nós que já saíam do parsing existente.

## Porquê construído em segundo plano, não ao abrir o livro

Construir o índice completo de um livro implica chamar
`getChapterContentRich` para *todos* os capítulos — o mesmo tipo de trabalho
que a contagem total de páginas já evita fazer de uma vez (ver
`AppReader::startTotalPagesCounting`). `startTocBuild()`/`updateTocBuild()`
seguem a mesma forma: fatias orçamentadas a `TOTAL_PAGES_BUDGET_MS` por
`update()`, chamadas a partir de `AppReader::openBook()`.

Diferença relevante face à contagem de páginas: o índice **não depende do
tamanho nem da família de letra** — ao contrário da contagem de páginas, só
precisa de ser construído uma vez por livro, nunca mais enquanto o EPUB não
for substituído. `ChapterTocStore` cacheia o resultado completo
(`/chapter_toc.json` no SystemFS, chaveado pelo nome original, como o
`BookTitleStore`), com uma escrita única no fim do scan em vez de uma por
capítulo. Sem checkpoint (ao contrário da contagem de páginas): o scan em si
é mais barato — não usa `TextRenderer` nem mede tipo de letra, é só o
`getChapterContentRich` que a paginação já fazia — por isso recomeçar do
capítulo 0 depois de um standby a meio é aceitável.

## Porquê gerido pela web UI, não por um gesto no dispositivo

Mesma razão já documentada em
[2026-08-29-bookmarks-and-goto-percent-design.md](2026-08-29-bookmarks-and-goto-percent-design.md):
os três botões físicos do Book32 estão todos comprometidos com gestos já
afinados, e não há gesto livre para um ecrã de "lista de capítulos". O
índice é por isso só consultável e accionável a partir da interface web
(novo cartão "Índice", ao lado de "Marcadores" e "Ir Para (%)"), com o mesmo
formato "a web define a intenção, o dispositivo aplica-a da próxima vez que
o livro abrir" que os marcadores e o "ir para %" já usam — funciona porque o
WiFi está sempre desligado enquanto um livro está aberto no dispositivo
(`AppReader::start()`).

Por omissão deliberada: **não foi tocado o rodapé de leitura no
dispositivo.** Mostrar o título do capítulo actual ali seria visível de
imediato sem precisar da web, mas implica medir e ajustar o layout do
rodapé (`AppReader::drawReading`) — algo que esta sessão não tem forma de
verificar num ecrã e-ink real. Fica como possível extensão futura, a
verificar num dispositivo real antes de se arriscar uma regressão visual.

## Ir para capítulo — mais simples do que "ir para %"

Ao contrário do "ir para %" (que precisa de percorrer o livro para saber
*onde* cai uma percentagem, ver `GoToPercentLogic.h`), um pedido de "ir para
capítulo" já vem com o índice exacto — foi a própria web UI que o leu de
`/api/toc`. `GoToChapterStore` é por isso um espelho simplificado do
`GoToPercentStore` (mesma forma, um único pedido pendente em memória, nunca
gravado em flash), mas `AppReader::applyChapterJump()` resolve-o de
imediato dentro de `openBook()` — chama `loadChapter()` directamente, sem
precisar de nenhum scan em segundo plano nem de estado "a decorrer" como o
`updatePercentSeek()`.

O número de página global mostrado depois do salto é uma aproximação — a
mesma lógica já usada pelo "ir para %" quando a contagem total já está em
cache (proporcional, aqui à posição do capítulo em vez de a uma
percentagem); sem contagem em cache, fica `1`, tal como acontece hoje no
"ir para %". Nunca é usado para retomar a leitura — a posição real vive em
capítulo + nó + deslocamento de caracteres, como sempre.

## Novos ficheiros

- `lib/Book32_Core/ChapterTocStore.h/.cpp` — cache local do índice completo
  por livro (mapa nome original → vector de títulos), mesma forma do
  `BookTitleStore`.
- `lib/Book32_Core/GoToChapterStore.h/.cpp` — pedido pendente de "ir para
  capítulo", mesma forma do `GoToPercentStore`.
- `GET /api/toc?book=...` — lista os capítulos e títulos já indexados
  (`"ready":false` e lista vazia enquanto o livro não tiver sido aberto e
  totalmente indexado no dispositivo).
- `POST /api/reader/goto-chapter` — corpo `{"book":"...","chapter":N}`,
  valida o intervalo contra o índice já guardado em `ChapterTocStore`.

## Por verificar no dispositivo

`pio run` não correu neste ambiente (registo do PlatformIO inacessível, como
já tinha acontecido nalgumas sessões anteriores — ver TODO.txt); só os
testes de host (inalterados por esta funcionalidade) correram. Ver a secção
v1.18.0 do TODO.txt para o que falta confirmar num dispositivo real.
