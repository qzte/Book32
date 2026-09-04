# Capa em ecrã inteiro na página de capa — design notes

Continua
[2026-09-03-conversao-de-capas-design.md](2026-09-03-conversao-de-capas-design.md),
que deixou explicitamente de fora a "capa em grande ao abrir um livro". É
isso que esta alteração faz: onde o leitor mostrava a página de capa do EPUB
como texto — quase vazia, muitas vezes só com a palavra "capa" — passa a
mostrar a imagem de capa a ocupar o ecrã.

## Porque é que a página de capa saía como texto

O leitor não desenha imagens dentro do texto: `parseHtmlToRichContent()`
salta as tags `<img>`/`<image>` (é código deliberado, não um esquecimento —
uma imagem no meio de um parágrafo não tem sítio na paginação actual). Ora o
XHTML da página de capa de um EPUB é, quase sempre, **só** uma imagem. O que
sobra é a página vazia, ou o pouco texto alternativo que o ficheiro traga —
donde a "página a dizer capa".

Havia ainda um segundo comportamento, que explicava por que uns livros
mostravam essa página e outros nem isso: `loadChapter()` salta capítulos sem
conteúdo nenhum, por isso uma página de capa que fosse *só* imagem
desaparecia por completo e o livro abria directamente no primeiro capítulo.

## Qual é o capítulo da capa

`EpubLoader::findCoverChapterIndex()`, por esta ordem:

1. `<guide><reference type="cover" href="...">` (EPUB2) — já está lido em
   `chapterGuideType` desde a classificação de capítulos não-narrativos, por
   isso não custa I/O nenhum;
2. o primeiro dos **três** primeiros capítulos da spine cujo HTML refira o
   ficheiro da imagem de capa. É o que apanha os EPUB3 puros, que não têm
   `<guide>`. Compara-se só o nome do ficheiro, porque o href dentro do XHTML
   é relativo à pasta dele (`../Images/capa.jpg`) e o `coverHref` do manifest
   é relativo à raiz do OPF — os caminhos completos não bateriam certo.

O limite de três capítulos é o que impede que uma imagem repetida a meio do
livro seja tomada por capa. Resolve-se uma vez por abertura do livro
(`openBook`), nunca a cada página desenhada: o passo 2 lê capítulos do ZIP.

## O desenho

Na página de capa — primeira página do capítulo de capa —, `drawReading()`
desenha o bitmap e mais nada: sem rodapé e sem número de página, como num
leitor comercial. Uma capa 2:3 fica 320×480 num ecrã de 800×480, centrada,
com o resto branco; é o mesmo `fitInsideBox()` da miniatura, só com uma caixa
maior.

Se o capítulo de capa tiver texto que chegue para mais do que uma página
(acontece com créditos na mesma página), as páginas seguintes continuam a ser
desenhadas como texto, com rodapé: só a primeira é substituída.

**A paginação não muda.** A página de capa não passa pelo renderizador, e
`_currentPageRenderValid` fica a `false` — o `nextPage()` volta a medir o
capítulo exactamente como faria com um cache invalidado. Nenhum número de
página, progresso guardado ou contagem total muda por causa disto.

`loadChapter()` e `prevChapter()` ganham uma excepção ao salto de capítulos
vazios: o capítulo de capa conta como tendo conteúdo mesmo sem texto, desde
que o livro tenha imagem de capa. Sem isto a funcionalidade só apareceria nos
livros cuja página de capa tem texto — precisamente os que já mostravam
alguma coisa.

## Cache

O bitmap de ecrã inteiro fica em `/covers/<nome>.cover` — a extensão que a
limpeza de livros em `WebMgr.cpp` já apaga desde antes de existir esta
funcionalidade, tal como aconteceu com `.thumb` na v1.19.

O cabeçalho do cache passa a ser comum aos dois tamanhos e sobe para a
**versão 3**: os campos de caixa e rectângulo passam de 8 para 16 bits, sem o
que uma caixa de 800 px não caberia. Consequência conhecida e desejada: as
miniaturas da v1.21 são apagadas e reconvertidas sozinhas na primeira visita
à biblioteca depois desta actualização, pelo mesmo mecanismo que já tratou a
subida anterior.

A conversão da capa grande é feita à primeira vez que a página de capa é
mesmo mostrada, não ao abrir o livro: quem nunca chega à capa não paga o
custo. Falhar deixa um marcador "sem capa" no ficheiro, para o livro não
voltar a tentar a cada abertura.

## Memória

A caixa grande obrigou a mudar onde vivem os acumuladores do `GrayBoxScaler`:
uma redução para 320×480 são ~150 000 células, perto de 1 MB entre somas e
contadores. Isso não cabe na RAM interna do ESP32-S3, e um `std::vector` que
falha **aborta** o programa (as excepções estão desligadas no Arduino-ESP32)
em vez de devolver `false`. Passaram a ser alocados à mão, com
`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` e recurso a `malloc()` — na PSRAM,
que é onde o resto da conversão já trabalha, e uma falha volta a ser só um
livro sem capa. Nos testes de host o mesmo código usa `malloc()`.

O bitmap final são 48 KB (800×480 a 1 bit), também em PSRAM, lido uma vez por
desenho e libertado a seguir — nunca dentro do ciclo
`firstPage()`/`nextPage()`, que a GxEPD2 pode percorrer várias vezes.

## Fora de âmbito

- Página de capa em livros cujo EPUB não a traz na spine: não se inventa uma.
- Título e autor por cima ou ao lado da capa.
- Imagens dentro do texto do livro: o leitor continua a ser só texto.
