# Escrita atómica dos ficheiros JSON, e o corpo partilhado dos stores — design notes

Responde a D1 e D2 de
[2026-09-18-avaliacao-codigo-stores-web.md](2026-09-18-avaliacao-codigo-stores-web.md).
Saiu na v1.26.0 (PR #92).

## O problema

Onze classes gravavam treze ficheiros JSON. Duas — `ProgressStore` e
`BookmarkStore` — faziam-no bem: documento para um `.tmp`, depois `rename`
por cima do destino. As outras nove faziam isto:

```cpp
File f = FS.open(PATH, FILE_WRITE);   // trunca o ficheiro bom já aqui
if (!f) return false;
serializeJson(doc, f);                // valor de retorno ignorado
f.close();
return true;                          // "gravado" mesmo com o FS cheio
```

Duas falhas distintas, com a mesma consequência:

1. **Falha de energia a meio.** `FILE_WRITE` trunca antes de escrever fosse o
   que for. Este aparelho chama `esp_deep_sleep_start()` directamente (ver
   `BatteryMgr::enterIdleSleep`) e vive de bateria, por isso a janela não é
   teórica.
2. **Filesystem cheio.** O `serializeJson` escreve o que cabe, devolve um
   número maior que zero, e a função dizia que tinha gravado.

A consequência é a mesma porque **JSON cortado não falha a meias**: o
`deserializeJson` recusa o ficheiro inteiro. Perde-se a entrada que estava a
ser gravada *e* as de todos os outros livros.

Onde isso mais custa é o `books_meta.json`, o mapa nome-truncado →
nome-original (o *upload* corta os nomes aos 28 caracteres). O progresso de
leitura é chaveado pelo nome **original**; sem esse mapa, o progresso de toda
a biblioteca fica órfão e os títulos voltam aos nomes cortados.

## A solução: uma só escrita

`JsonFileStore.h` expõe uma função, e todas as gravações passam a usá-la:

```cpp
bool writeJsonAtomic(fs::FS& fs, const char* path, const JsonDocument& doc,
                     const char* tag);
```

É a sequência que o `ProgressStore` já tinha, com **uma verificação a mais que
faltava lá também**: comparar os bytes escritos com `measureJson(doc)` em vez
de os comparar com zero. Era assim que a escrita parcial num filesystem cheio
passava por sucesso mesmo no caminho "bom".

Fail-closed em todos os passos — transbordo do documento, `.tmp` que não abre,
escrita curta, `rename` falhado. Qualquer um deles deixa o ficheiro anterior
intacto e apaga o temporário, para não ficar a ocupar espaço nem a ser lido
como se fosse bom.

### O caminho do temporário

O `.tmp` deriva do destino trocando a extensão, não acrescentando: de
`/reader_progress.json` sai `/reader_progress.tmp`. É de propósito — são
exactamente os caminhos que o `ProgressStore` e o `BookmarkStore` já usavam,
por isso um aparelho actualizado não fica com restos de um `.tmp` com outro
nome.

O `rename` do littlefs substitui o destino atomicamente. O `remove` + segunda
tentativa que lá está é só para portes que recusem um destino existente; é um
caminho que na prática não se usa.

## A parte pura

`JsonStoreLogic.h`, sem Arduino, com teste de host — a mesma convenção do
`SemVer.h` e do `ProgressMergeLogic.h`. Leva as três coisas que estavam
escritas à mão em cada *store*:

**`jsonStoreCapacity(base, perUnit, units, max)`.** As dez cópias faziam
`fileSize * 2 + slack` ou `base + entries * per`, com constantes diferentes e
nenhum teste. Nenhuma tinha protecção contra transbordo — e um transbordo aí
devolve um documento *pequeno* para uma entrada *grande*, que é precisamente
como se chega ao documento que transborda e à escrita recusada. Agora satura.

**`jsonWriteOutcome(attempt)`.** A tabela de decisão de uma gravação, separada
das chamadas ao filesystem: o `JsonFileStore.cpp` executa os passos e pergunta
a esta função o que eles significam. É o que permite testar "escreveu 200 dos
420 bytes" sem um filesystem.

**`reconcileStoreKeys(map, present)`.** Larga as entradas de livros que já não
existem. Cada cópia era o mesmo par de ciclos encaixados,
`O(entradas × livros)`; agora é uma procura ordenada por chave. Templated no
tipo do mapa, para o aparelho passar `String` e o teste de host passar
`std::string`.

## O template dos quatro caches de capítulos

`ChapterTocStore`, `ChapterNarrativeStore`, `ChapterGuideTypeStore` e
`ChapterLengthStore` eram o mesmo ficheiro escrito quatro vezes:
`map<String, vector<T>>` em SystemFS, um JSON de `{nome: [valores]}`, `load()`
preguiçoso, `get`/`set`/`reconcile` com o mesmo mutex recursivo. ~480 linhas de
cópia a cópia — e a correcção acima teria de ser feita quatro vezes e ficaria
a diferir na quinta.

`PerBookListStore<T>` leva o corpo. Cada *store* mantém a sua classe, a sua API
pública e o **seu próprio ficheiro em disco**: continuam separados de
propósito, pela razão que o cabeçalho do `ChapterNarrativeStore` já explicava —
um formato já gravado em aparelhos reais muda de forma em silêncio se se lhe
tentar meter um campo a mais, porque o `deserializeJson` não erra, só devolve
valores errados.

Os parâmetros de capacidade de cada um são exactamente os que já usava. Nada
aqui muda o perfil de memória de nenhum.

Um pormenor de implementação que não é óbvio: ao gravar, copia-se cada
elemento para uma variável de tipo `T` em vez de usar uma referência, porque
em `std::vector<bool>` o `operator[]` devolve um *proxy* que o ArduinoJson não
sabe converter.

## O que isto não resolve

- **Não é journaling.** Uma gravação interrompida perde-se; o que se garante é
  que se perde *só ela*, e que o ficheiro anterior continua legível. Para
  *caches* isso significa recalcular; para o `books_meta.json` significa ficar
  com o estado anterior em vez de nenhum.
- **O `remove` + `rename` da segunda tentativa** tem uma janela em que nenhum
  dos dois ficheiros existe. É um caminho que o littlefs não usa, e fechá-lo
  implicaria um esquema de dois ficheiros alternados que não se justifica.
- **Não há teste em hardware.** A aritmética e a tabela de decisão têm teste de
  host; o comportamento real perante uma falha de energia não foi exercitado.
  Ver o `TODO.txt`, secção `v1.26.0`.

## Ficheiros afectados

Passaram a gravar atomicamente: `BookMeta`, `SettingsStore` (×3 ficheiros),
`BookTitleStore`, `PageCountStore`, os quatro *caches* de capítulos, e a ordem
manual da biblioteca no `WebMgr`. O `ProgressStore` e o `BookmarkStore`
mantiveram o comportamento e ganharam a verificação de escrita curta.

Sem alterações de formato em disco: um aparelho actualizado lê exactamente os
mesmos ficheiros.
