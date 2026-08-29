# Título do livro na biblioteca, em duas linhas — design notes (v1.17.0)

Na lista da biblioteca, no dispositivo, os títulos apareciam sempre numa linha
só e cortados a meio da palavra, sem sequer reticências:

```
Tempestade de Ónix Reb        pag. 112/1270
The Butchers Masquerade       pag. 1
The Gate of the Feral G       pag. 1
```

O sintoma parece um problema de desenho, mas não é: os três títulos têm
exactamente 23 bytes. O corte não acontece no ecrã, acontece muito antes.

## O corte estava no upload, não no desenho

`WebMgr.cpp` corta o nome do ficheiro aos 28 caracteres à chegada, porque é
esse o tecto que o resto do sistema assume para um nome no LittleFS
(`BOOK32_MAX_NAME_LEN`, ver `SafeName.h`). Menos a extensão `.epub`, sobram 23
bytes de nome — e a biblioteca derivava o título de exactamente esse nome
cortado (`titleFromFilename`).

O `books_meta.json` guarda o nome original longo, e a lista já o preferia
quando existia; mas para os livros que lá estão sem entrada de metadados (ou
cujo nome original já era ele próprio o nome do ficheiro) não há nada a
recuperar. A quebra em duas linhas que o desenho já fazia nunca disparava
porque nunca havia texto que a justificasse: com a fonte de 12pt, 23 bytes
medem ~295px numa linha útil de 324px.

## O título a sério vem de dentro do EPUB

O `<dc:title>` do OPF é o título verdadeiro, independente do nome do ficheiro,
e o `EpubLoader` já o lê — só que ao abrir o livro para leitura, tarde demais
para a lista.

Abrir o ZIP de cada livro no `scanBooks()` não serve: o `scanBooks()` corre
antes do primeiro desenho da biblioteca, e o custo passaria a crescer com o
tamanho da biblioteca — é exactamente o género de trabalho síncrono que o
leitor evita no caminho de abrir um livro (ver `startTotalPagesCounting`).

Por isso:

- `AppReader::resolveNextBookTitle()` lê **um** livro por passagem do
  `update()`, e só no estado `VIEW_LIBRARY`. Um livro sem `<dc:title>`, ou que
  não abre, é marcado como tentado e não volta a ser reaberto nessa sessão.
- O resultado vai para o `BookTitleStore` (`/book_titles.json`, no SystemFS),
  chaveado pelo nome original — a mesma chave do `ProgressStore` — e é podado
  no mesmo ponto do `scanBooks()` em que as posições e os marcadores de livros
  apagados são podados.
- A partir daí a lista já abre com os títulos certos: o custo é uma vez por
  livro, não uma vez por visita.
- Enquanto o lote não termina, a lista continua a mostrar o nome do ficheiro.
  O repintar é **um só**, no fim do lote (`_titlesDirty`): cada refresh e-ink
  custa perto de um segundo, e um por título dava uma sequência de piscadelas
  na primeira visita.

Cache local, como o `PageCountStore` e ao contrário do `ProgressStore`: não
entra no export/import de estado, porque qualquer dispositivo a reconstrói
sozinho a partir dos próprios ficheiros.

## Limpeza do `<dc:title>`

Um `<dc:title>` é texto de um ficheiro do utilizador e chega com marcação
(`<span>`), entidades XML (`&amp;`, `&#211;`) e as mudanças de linha da
indentação do OPF. `sanitizeBookTitleT()` tira a marcação, descodifica as
entidades para UTF-8, colapsa espaços e corta aos 96 bytes — nunca a meio de
uma sequência UTF-8, que no ecrã e na web UI daria lixo. Devolver "" é
resposta válida: o chamador é que decide o fallback (o nome do ficheiro), a
função não o inventa.

O ficheiro guarda UTF-8, porque é o que a web UI lê; a conversão para Latin-1
acontece só à entrada do desenho, como já acontecia com os nomes de ficheiro.

## Duas linhas, e agora com texto para as encher

A quebra saiu do meio do `drawLibrary()` para `BookTitleLogic.h`, testável em
host (`tools/tests/test_book_title.cpp`), com a medição a entrar por callback
para servir tanto a fonte normal como a negrito do item seleccionado. Três
diferenças em relação ao que lá estava:

- **Duas linhas para todos os itens.** O item seleccionado permitia três, e a
  terceira linha (base em y+90) aterrava por cima do `pag. x/y` (base em
  y+88). Com títulos curtos isto nunca chegou a aparecer; com títulos a sério
  aparecia de certeza.
- **Reticências que cabem mesmo.** O código antigo tirava três caracteres à
  linha e acrescentava `...`, sem voltar a medir — com caracteres estreitos, a
  linha voltava a transbordar. Agora encolhe até `...` caber.
- **Palavra maior que a linha é partida por caracteres.** Antes era desenhada
  inteira e o ecrã cortava-a — que é precisamente o aspecto que este trabalho
  vem resolver.

## O que isto não faz

Não mexe no tecto de 28 caracteres do upload: o nome em disco continua
cortado, e é indiferente, porque deixou de ser ele a decidir o que se lê no
ecrã. A web UI continua a listar os livros pelo nome original do ficheiro —
passá-la a usar o `<dc:title>` é uma mudança à parte, na API `/api/books`.

## Verificação

Testes de host em `tools/tests/test_book_title.cpp` (limpeza do título,
quebra, reticências, palavra sozinha demasiado larga, casos degenerados). O
resto — o custo real de abrir cada EPUB, e o número de refreshes na primeira
visita — só se vê no dispositivo; ver as entradas da v1.17.0 no `TODO.txt`.
