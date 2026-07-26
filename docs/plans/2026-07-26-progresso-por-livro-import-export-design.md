# v1.8.0 — Progresso por livro e import/export do estado da biblioteca

## Problema

O progresso de leitura já é guardado por livro em `reader_progress.json`, mas a
persistência tem três falhas que só aparecem com bibliotecas reais:

1. `DynamicJsonDocument doc(4096)` fixo. Acima dessa capacidade a leitura
   trunca, e a gravação seguinte serializa o documento **já truncado** por cima
   do ficheiro: perde-se o progresso dos restantes livros, sem erro visível.
2. Gravação directa sobre o ficheiro real. Uma falha de energia a meio deixa
   JSON inválido e leva o progresso de toda a biblioteca.
3. `updatedAt = millis()` reinicia a cada boot, portanto não serve para ordenar
   nem para resolver conflitos.

Além disso, a chave é o caminho do ficheiro. Como o upload trunca nomes a 28
caracteres e junta sufixo anti-colisão, a chave não é estável entre
dispositivos nem sobrevive a um re-envio do mesmo livro.

Correcção a uma afirmação anterior deste desenho: `/api/books/delete` **já**
chama `removeBookProgress()`, portanto apagar pela web UI não deixa órfãos. Só
ficam órfãs entradas de ficheiros removidos por outra via, e as vítimas da
truncagem do ponto 1. A poda é defesa, não correcção de um bug activo.

Não existe forma de levar o estado de leitura de um dispositivo para outro.

## Decisões

| Questão | Decisão |
| --- | --- |
| Conteúdo do export | Progresso + metadados + ordem manual. Sem `.epub`. |
| Identidade do livro | Nome original de `books_meta.json`. |
| Conflito no import | Merge; ganha a página mais avançada. |
| Versão | `1.8.0` (minor: funcionalidade nova, sem quebra). |

Nome original como chave é gratuito: `getOriginalFilename()` já devolve o
próprio nome do ficheiro quando não há entrada em `books_meta.json`, portanto
livros antigos degradam sozinhos para o nome real.

## Esquema v2

```json
{ "schema": 2, "seq": 41, "lastBook": "Os Maias.epub", "resumeOnBoot": true,
  "books": { "Os Maias.epub": { "chapter": 7, "nodeIndex": 152,
                                "charOffset": 0, "globalPage": 214,
                                "seq": 41, "pending": false } } }
```

`seq` é um contador monótono global, incrementado a cada gravação. Sobrevive a
reboots e não exige RTC nem NTP — que o dispositivo não tem.

Migração v1→v2 na primeira leitura: as chaves antigas (`/nome.epub`) perdem a
barra e passam pela resolução de nome original. Uma única escrita atómica.

## Arquitectura

`READER_PROGRESS_PATH` está hoje declarado em `AppReader.cpp` **e** em
`WebMgr.cpp`, e cada lado abre e reescreve o ficheiro à sua maneira. Import e
export acrescentariam um terceiro escritor. A correcção é extrair um dono
único.

- **`ProgressStore`** (`lib/Book32_Core/`) — singleton. Mantém em RAM um
  `std::map<String, BookProgress>` mais `lastBook`, `resumeOnBoot` e `seq`.
  Carrega uma vez, grava atomicamente (`.tmp` + rename). A capacidade do
  `DynamicJsonDocument` na leitura deriva do tamanho do ficheiro
  (`size * 2 + 1024`, com tecto); na escrita é dimensionada pelo número de
  entradas. Fim do 4096 fixo.
- **`ProgressMergeLogic.h`** (`lib/Book32_Core/`) — funções puras, sem
  ArduinoJson nem LittleFS: merge, migração de chave, poda, validação de
  esquema. Testável no host, no mesmo molde do `BookOrderLogic.h`.
- **`AppReader`** e **`WebMgr`** passam a consumir o store. A declaração
  duplicada desaparece.

### Ponteiro de conteúdo, não número de página

O que é restaurado é `chapter` + `nodeIndex` + `charOffset`, não a página. O
número de página não é estável: muda com o tamanho e a família da fonte. O
ponteiro sobrevive, e a repaginação arranca no parágrafo e carácter onde o
leitor ficou.

Nenhum caminho de saída perde a posição: cada virar de página chama
`saveReadingProgress(true)`, o `closeBook()` grava, e o deep sleep do
`BatteryMgr` não precisa de alteração porque a última página virada já está em
flash.

### Biblioteca com progresso visível

`scanBooks()` já lê `books_meta.json`; passa a ler o store **de uma só vez**
(não uma leitura por livro). `BookEntry` ganha `hasProgress` e `globalPage`, e
a lista mostra `pág. N` sob o título de quem tem progresso.

Sem percentagem: `calculateTotalPages()` está desactivado por ser lento demais
em livros grandes, portanto uma percentagem seria inventada.

### Poda

Entradas sem ficheiro correspondente são removidas no `scanBooks()`. Não
substitui o `removeBookProgress()` do endpoint de apagar — é a rede de
segurança para ficheiros que saem por outra via.

## Endpoints

### `GET /api/library/export`

Serialização em stream, sem buffer fixo — o mesmo padrão que corrigiu o
`/api/books` truncado na v1.2.0.

```json
{ "book32": { "schema": 2, "version": "1.8.0", "seq": 41 },
  "progress": { "Os Maias.epub": { "chapter": 7, "nodeIndex": 152,
                                   "charOffset": 0, "globalPage": 214 } },
  "meta":  { "Os Maias.epub": "Os Maias - Eca de Queiros.epub" },
  "order": ["Os Maias.epub", "Memorial do Convento.epub"] }
```

`progress`, `meta` e `order` usam todos a chave "nome original", para o import
não depender de como o outro dispositivo truncou os nomes.

Sem auth, à semelhança de `/api/books` e do `/api/reader/progress` GET: não
expõe nada que esses dois já não exponham. O nome do ficheiro é construído no
browser com `Date` do cliente — o dispositivo não tem relógio.

### `POST /api/library/import`

Com auth, e como **upload multipart para ficheiro temporário**, não como
`AsyncCallbackJsonWebHandler`: esse handler acumula o corpo todo em RAM antes
de chamar o callback, e um bundle grande arrisca esgotar a heap a meio do
pedido. Streamar para `/import.tmp` e parsear depois reusa a mecânica de
single-flight e `.part` já existente para os EPUBs. Tecto de 64 KB → 413.

Resposta: `{"merged":3,"added":2,"pending":1,"skipped":0}`.

## Regra de merge

Ganha o maior `globalPage`; empate mantém o local.

Ressalva registada: `globalPage` depende do tamanho de fonte com que o livro
foi lido, portanto entre dispositivos com fontes diferentes a comparação é
aproximada. Alternativa exacta seria comparar `chapter` e depois `nodeIndex`;
ficou por escolha explícita a regra da página, por ser a que corresponde à
intuição de "onde é que eu ia mais adiantado".

## Trap: importar antes de enviar os EPUBs

Se o estado for importado **antes** dos ficheiros, a poda apaga o progresso
importado, porque não há ficheiro correspondente. Por isso as entradas
importadas sem ficheiro local ficam marcadas `pending` e são isentas de poda; a
marca cai na primeira vez que o ficheiro aparece.

## Metadados no import

`meta` só é aplicado a nomes de ficheiro que existam localmente e ainda não
tenham entrada. Importar mapeamentos de outro dispositivo para ficheiros que
não existem aqui criaria associações erradas.

## Testes

`tools/tests/test_progress_merge.cpp`, escrito antes da implementação:

1. importado mais avançado ganha
2. local mais avançado ganha
3. empate mantém local
4. livro só no import, sem ficheiro local, fica `pending`
5. livro só no import, com ficheiro local, não fica `pending`
6. livro só local sobrevive intacto
7. chave v1 `/livro.epub` migra para `livro.epub`
8. chave v1 truncada resolve para o nome original
9. `pending` isento de poda
10. ausente e não-`pending` é podado
11. `pending` cai quando o ficheiro aparece
12. `schema` desconhecido rejeita o bundle inteiro

Não coberto no host, a verificar no dispositivo: atomicidade do `.tmp` +
rename, tecto de 64 KB no upload, poda no `scanBooks()` real.

## Ficheiros

Novos: `ProgressStore.{h,cpp}`, `ProgressMergeLogic.h`,
`tools/tests/test_progress_merge.cpp`, este documento.

Alterados: `AppReader.{cpp,h}`, `WebMgr.cpp`, `data/index.html`,
`data/script.js`, `include/Config.h`, `README.md`, `TODO.txt`.

## Nota de versão

`SYSTEM_VERSION` estava em `1.6.4` enquanto o commit `0839710` diz "v.1.6.5".
O bump para `1.8.0` fecha a dessincronização. Confirmar que a 1.6.5 não saiu
como release publicada antes de assumir este valor.
