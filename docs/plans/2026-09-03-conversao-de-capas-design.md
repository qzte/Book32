# Conversão das capas para o e-ink — design notes

Revê a conversão de imagem introduzida em
[2026-08-31-capas-reais-biblioteca-design.md](2026-08-31-capas-reais-biblioteca-design.md)
(v1.19.0). A funcionalidade em si — capa real por livro, cache em
`/covers/<nome>.thumb`, extracção em segundo plano na biblioteca — mantém-se
como estava; o que muda é **como** os bytes da capa se transformam nos
60×80 pixeis de 1 bit que o item da biblioteca mostra.

## O que estava mal

1. **Reticular antes de reduzir.** A v1.19 pedia à JPEGDEC um bitmap já
   reticulado (`ONE_BIT_DITHERED`, Floyd-Steinberg embutido) no tamanho
   descodificado — 1/2, 1/4 ou 1/8 do original — e só depois o reduzia ao
   tamanho do item por amostragem do vizinho mais próximo. Reduzir uma
   imagem já reticulada é escolher pontos soltos do retículo e deitar fora
   os outros: o padrão que dava a ilusão de tons desaparece e o que fica é
   ruído. Uma zona de cinzento médio, que devia ficar meio preta meio
   branca, sai manchada ao acaso conforme os pontos que a amostragem calhou
   apanhar.
2. **Proporção ignorada.** A capa era esticada para a caixa 60×80 (3:4)
   qualquer que fosse a sua proporção. As capas de EPUB andam quase todas
   perto de 2:3, por isso saíam sistematicamente achatadas.
3. **Sem contraste.** Num ecrã de 1 bit não há tons a que recorrer: uma capa
   com os tons todos no meio da escala fica quase toda do mesmo lado do
   limiar, ou seja, um bloco preto ou um bloco branco.
4. **Só JPEG.** Um EPUB com capa PNG ficava com o desenho genérico. Era uma
   decisão deliberada da v1.19 (não duplicar o risco de um descodificador
   não verificável), mas é uma fatia grande dos livros.
5. **Cache sem versão.** O ficheiro `.thumb` era o bitmap cru e a única
   validação era o tamanho. Uma miniatura gerada por uma versão anterior
   ficava no ecrã para sempre, e o marcador "sem capa" (ficheiro vazio)
   impedia que um livro voltasse a ser tentado quando o suporte melhorasse.

## Pipeline novo

Pela ordem certa, que é o inverso da anterior:

1. **Descodificar em tons de cinzento.** JPEG com
   `setPixelType(EIGHT_BIT_GRAYSCALE)` e a opção `JPEG_LUMA_ONLY` (salta as
   componentes de cor, que não são usadas); PNG linha a linha, convertida
   para luminância. Mantém-se a escolha do maior factor de redução da
   JPEGDEC (2/4/8) que ainda deixa a imagem `>=` ao tamanho final.
2. **Reduzir por média de área** (`GrayBoxScaler`) ao tamanho final, com a
   proporção preservada: cada pixel de origem entra na média da célula de
   destino que lhe corresponde, nenhuma linha ou coluna é deitada fora.
3. **Esticar o contraste** (`autoLevelGray`), ignorando 1% dos pixeis mais
   escuros e 1% dos mais claros para que um logótipo preto ou um brilho
   branco não decidam sozinhos a escala — e só quando o intervalo original
   tem pelo menos 24 níveis, para não amplificar ruído de compressão numa
   capa genuinamente lisa.
4. **Dithering Floyd-Steinberg já no tamanho final**
   (`ditherToBitmap1bpp`), em varrimento serpentino (linhas ímpares da
   direita para a esquerda) para evitar as riscas diagonais do varrimento
   sempre no mesmo sentido, bem visíveis a 60 pixeis de largura.

Os passos 2 a 4 são aritmética inteira pura e vivem em
`lib/Book32_Core/ImageDither.h`, fora do código que depende do Arduino, para
correrem nos testes de host (`tools/tests/test_image_dither.cpp`, ligado ao
mesmo ciclo do CI que os restantes). O teste verifica o que interessa e não
o desenho em si: que a média de área dá a mesma coisa independentemente da
ordem por que os blocos chegam (a JPEGDEC entrega bandas de MCU, a PNGdec
linhas), que a fracção de pixeis pretos depois do dithering acompanha o tom
de origem, que preto e branco puros não ficam reticulados, e que nada é
escrito fora do bitmap de destino.

Continua a não haver forma de avaliar o resultado num e-ink real nesta
sessão. A diferença em relação à v1.19 é que agora a decisão não é "que
algoritmo parece melhor" mas "que ordem de operações não destrói
informação", que se verifica sem ecrã.

## Proporção e moldura

`fitInsideBox()` calcula o rectângulo da capa dentro da caixa 60×80 com a
proporção preservada e centrado (uma capa 2:3 fica 53×80, com 3 px de margem
branca de cada lado). O rectângulo é guardado no cache e o contorno passa a
ser desenhado colado à capa, não à volta da caixa — de outra forma a capa
ficaria a flutuar dentro de uma moldura maior do que ela.

O tamanho da caixa não muda (60×80, `ITEM_HEIGHT` de 110): é layout da
biblioteca, não conversão de imagem, e mexer nele arrastava o resto do item.

## PNG

Acrescenta-se a `PNGdec` (bitbank2, mesma família da JPEGDEC já usada, pin
por commit pela mesma convenção). Suporta os cinco tipos de pixel do
standard, com 1/2/4/8 bits por amostra; alfa e transparência de paleta são
misturados com fundo branco, que é o fundo do item. PNG entrelaçado e PNG de
16 bits por canal não são suportados pela biblioteca — esses livros ficam com
o desenho genérico, como qualquer outro ficheiro que não se saiba abrir.

Dois cuidados que a biblioteca não trata sozinha:

- **Tamanho do buffer de linha.** `PNG_MAX_BUFFERED_PIXELS` está afinado por
  omissão para ~320 px de largura; uma capa passa disso à vontade. Sobe para
  33024 (ver `platformio.ini`). A guarda da própria PNGdec compara esse
  tamanho com **uma** linha, mas o buffer guarda **duas** (a actual e a
  anterior, para o filtro): `pngLineFits()` em `CoverImage.cpp` refaz a conta
  com as duas e recusa a capa antes de descodificar, em vez de confiar numa
  guarda que deixa passar um caso de escrita fora do buffer.
- **Tempo.** A JPEGDEC sabe descodificar já reduzida (1/2, 1/4, 1/8); um PNG
  tem de ser inflado por inteiro antes de se poder reduzir seja o que for, e
  uma capa gigante prenderia o `loop()` do leitor durante segundos. Há por
  isso um tecto de ~6 MP (`kMaxPngPixels`, dá para ~2000×3000): acima dele o
  livro fica com o desenho genérico. Abaixo, o custo é pago uma vez por livro,
  porque o resultado fica em cache.
- **Memória.** A `PNGIMAGE` embutida na classe `PNG` são ~65 KB (janela de
  32 KB do zlib + buffers de linha), muito acima do que a stack do loopTask
  aguenta — a mesma armadilha que a JPEGDEC criou na v1.19.1. Vai para
  PSRAM (`ps_malloc` + placement new), como o resto do trabalho da conversão.

O formato é decidido pelos **bytes** do ficheiro (assinatura JPEG/PNG), não
pela extensão nem pelo `media-type` do OPF: há EPUBs com um `cover.jpg` que é
afinal um PNG.

`EpubLoader::parseOpf()` ganha ainda um último recurso: se o OPF não declarar
capa por nenhuma das duas formas oficiais, usa-se uma imagem do manifest cujo
href ou id contenha "cover". Só depois de as duas formas do standard
falharem.

## Cache com versão

O ficheiro `/covers/<nome>.thumb` passa a ter um cabeçalho de 12 bytes:
`"B32C"`, versão, largura e altura da caixa, o rectângulo da capa dentro
dela, um byte reservado. `fitW = 0` é o marcador "já tentado, sem capa" e o
ficheiro acaba no cabeçalho.

Consequências, todas queridas:

- Um cache de outra versão (ou de outro tamanho de caixa, ou truncado a meio
  de uma escrita, ou no formato cru da v1.19) é apagado no arranque da
  biblioteca e o livro volta à fila de extracção — as miniaturas antigas
  regeneram-se sozinhas depois desta actualização, sem o utilizador ter de
  apagar nada.
- O marcador "sem capa" também traz a versão, por isso os livros de capa PNG
  que a v1.19 marcou como "sem capa" são tentados outra vez agora que PNG
  funciona.

A limpeza ao apagar um livro (`WebMgr.cpp`) não muda: continua a ser o mesmo
caminho de ficheiro.

## Fora de âmbito

- Tamanho da caixa e layout do item da biblioteca.
- Capa em grande (ecrã inteiro) ao abrir um livro — os `.cover`/`.cover2`
  que o `WebMgr.cpp` já limpa continuam por implementar.
- Imagens dentro do texto do livro: o leitor continua a ser só texto.
