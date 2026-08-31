# Capas reais na biblioteca — design notes (v1.19.0)

Implementa a lacuna #2 de
[2026-08-31-avaliacao-koreader.md](2026-08-31-avaliacao-koreader.md)
(reforçando a lacuna #1 de
[2026-08-31-avaliacao-gestao-epub.md](2026-08-31-avaliacao-gestao-epub.md)):
capas reais na biblioteca, com cache por livro.

## O que já existia (descoberto durante a implementação)

`WebMgr.cpp`'s `/api/books/delete` já limpa ficheiros
`/covers/<nome>.thumb`, `.cover` e `.cover2` ao apagar um livro, desde um
commit bem anterior a esta funcionalidade (`a507243`). Nenhum código
escrevia esses ficheiros — era limpeza a postos para uma funcionalidade
nunca implementada. Esta alteração adopta essa convenção já existente
(`/covers/<nome>.thumb` em `EbookFS`, nome derivado da mesma forma: extensão
retirada pela posição do último ponto) em vez de inventar outra — a limpeza
ao apagar um livro já funciona sem tocar em `WebMgr.cpp`. `.cover`/`.cover2`
ficam por implementar (ver "Fora de âmbito" abaixo).

## Detecção da capa (EpubLoader)

`EpubLoader::parseOpf()` procura, por ordem de prioridade:

1. **EPUB3**: `<item properties="cover-image" href="...">` no manifest —
   não depende de resolver um id, é a forma actual do standard.
2. **EPUB2**: `<meta name="cover" content="ID"/>` em `<metadata>`, resolvido
   contra o manifest depois de este estar todo lido.

Guardado só como `coverHref` (o caminho dentro do ZIP) — o `media-type` do
OPF não é guardado nem é de confiança; `EpubLoader::getCoverImageData()`
devolve os bytes crus e quem descodifica decide se sabe abri-los.

## Descodificação: só JPEG, com dithering já testado

`CoverImage.h`/`.cpp` usa a biblioteca `JPEGDEC` (bitbank2) com
`ONE_BIT_DITHERED` — dithering Floyd-Steinberg **embutido na biblioteca**,
com um exemplo próprio para e-ink (`examples/dithering/`), em vez de
qualquer conversão preto-e-branco escrita de raiz. Esta sessão não tem
acesso a um dispositivo real para avaliar visualmente um algoritmo de
dithering próprio, por isso preferiu-se reutilizar um já testado e usado por
outros projectos de e-ink a arriscar um.

**Só JPEG é suportado nesta versão.** A maioria das capas de EPUB comerciais
são JPEG; um EPUB com capa PNG (ou outro formato) fica sem capa —
`decodeJpegCoverToBitmap()` devolve `false` (a `JPEGDEC::openRAM()` da
própria biblioteca rejeita dados que não sejam JPEG) e o item volta ao
desenho genérico de sempre, sem tratamento especial nenhum do lado do
Book32. PNG fica como lacuna conhecida, não como bug — evita duplicar o
risco de "descodificador não verificável visualmente" com uma segunda
biblioteca.

### Redução ao tamanho do item

A JPEGDEC só reduz por factores fixos de 2/4/8 (`JPEG_SCALE_*`). Escolhe-se
o maior factor que ainda deixa a imagem descodificada `>=` ao tamanho do
item da biblioteca (60×80), para poupar tempo de descodificação numa capa de
alta resolução; a redução final ao tamanho exacto é feita à parte, por
amostragem do vizinho mais próximo sobre o bitmap 1bpp já dithered —
determinística e barata de mais para precisar de ser vista para se
confiar nela.

### Convenção de bit — atenção ao inverso

A JPEGDEC em `ONE_BIT_DITHERED` devolve `0=preto, 1=branco`. O
`Adafruit_GFX::drawBitmap()` que o resto do Book32 já usa (ícones do menu,
actualização OTA) espera o oposto: `bit=1` pinta na cor pedida, `bit=0`
deixa o fundo como estava. `downsampleToOutput()` em `CoverImage.cpp` inverte
o bit ao copiar — não é só reamostragem, é também a troca de convenção.

## Cache: um ficheiro por livro, sem índice à parte

Ao contrário do `BookTitleStore`/`ChapterTocStore` (mapa `nome -> valor` num
único JSON), a capa é um bitmap binário — não há vantagem em o embrulhar em
JSON. `/covers/<nome>.thumb` em `EbookFS` é o próprio cache:

- **Presente, do tamanho certo** (`((60+7)/8)*80 = 640` bytes) = capa real.
- **Presente, vazio** (0 bytes) = já se tentou, este livro não tem capa
  extraível (sem `cover-image`/`meta cover` no OPF, ou não é JPEG) — marcador
  para nunca mais reabrir o ZIP deste livro à procura de uma capa.
- **Ausente** = por tentar.

`AppReader::scanBooks()` faz um simples `exists()`+`size()` por livro (barato,
como `hasProgress`/`totalPages`) para preencher
`BookEntry::coverAttempted`/`hasCoverThumb`; `AppReader::resolveNextBookCover()`
segue a forma exacta do `resolveNextBookTitle()` já existente — um livro por
passagem do `update()`, só na biblioteca, com a mesma guarda de
exclusividade do descritor global do ZIP (`EpubLoader.cpp`'s `zipFd`).

## Intercalado com a resolução de título, não simultâneo

`update()` já chamava `resolveNextBookTitle()` uma vez por passagem. Chamar
`resolveNextBookCover()` incondicionalmente na mesma passagem duplicaria o
custo de abrir um ZIP por `update()` — e, quando há capa para descodificar,
acrescentaria uma descodificação JPEG (potencialmente algumas centenas de ms
numa capa grande) por cima disso. `resolveNextBookTitle()` passou a devolver
`bool` (abriu um ZIP nesta chamada?); `update()` só tenta a capa quando o
título não tinha nada para fazer:

```cpp
if (!resolveNextBookTitle()) resolveNextBookCover();
```

Isto garante no máximo um ZIP aberto por passagem — título e capa
intercalam-se, nunca empilham o custo na mesma passagem. O TODO.txt já pede,
desde a v1.17.0, para confirmar que os botões continuam a responder durante
a resolução de títulos; esta alteração mantém essa margem em vez de a
gastar a dobrar.

## Dependência: JPEGDEC pinada por commit, não pelo registo

O registo do PlatformIO não estava acessível nesta sessão (mesma limitação
já documentada nalgumas sessões anteriores, ver TODO.txt), por isso não foi
possível confirmar uma versão publicada com `author/Lib @ x.y.z`. Seguiu-se a
mesma convenção já usada para `bb_epaper`/`Arduino-Log` no
`platformio.ini`: apontar directamente para um commit do GitHub (tag `1.8.4`
da JPEGDEC, clonada e inspeccionada nesta sessão para confirmar a API real —
`openRAM`, `setPixelType(ONE_BIT_DITHERED)`, `decodeDither()` — em vez de a
adivinhar de memória).

## Fora de âmbito nesta versão

- **PNG e outros formatos de capa** — ver acima.
- **`.cover`/`.cover2`** — a limpeza em `WebMgr.cpp` já os conhece, mas esta
  versão só produz `.thumb` (o tamanho do item da biblioteca). Um ecrã de
  detalhe do livro com uma capa maior ficaria para depois, reaproveitando os
  mesmos nomes já reservados.
- **Capas na "Biblioteca do PC" da web UI** — o pedido era sobre a biblioteca
  no dispositivo; os ficheiros `.thumb` já ficam no dispositivo prontos a
  servir por HTTP no futuro, se fizer sentido, mas não há nenhum endpoint
  novo aqui.

## Por verificar num dispositivo real

`pio run` não correu neste ambiente (mesma limitação do registo do
PlatformIO); só os testes de host (inalterados por esta funcionalidade, já
que não há lógica pura nova aqui — é tudo E/S e uma biblioteca de terceiros)
e `clang-format` correram. A qualidade visual do dithering, em particular,
**não foi vista** nesta sessão — ver a secção v1.19.0 do TODO.txt.
