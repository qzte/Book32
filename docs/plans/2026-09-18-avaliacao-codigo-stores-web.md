# Avaliação do código (stores, camada web, gestão de memória)

Data: 2026-09-18
Estado: avaliação — as alterações associadas saíram na v1.26.0 (PRs #92 a #99)

## Âmbito

Revisão estática do que a
[avaliação do eReader](2026-09-02-avaliacao-codigo-ereader.md) não olhou: os
*stores* de `lib/Book32_Core/` (onze classes, treze ficheiros JSON), a camada
web (`lib/Book32_Web/WebMgr.cpp`, 1710 linhas) e a gestão de tempo de vida dos
objectos do leitor. Só revisão estática: o `pio run` não correu neste
ambiente — a política de rede bloqueia `api.registry.platformio.org` — e a
compilação ficou por conta do CI.

Ao contrário da avaliação do eReader, esta não ficou em avaliação: seis pontos
foram levantados, cinco foram feitos e um foi recusado com fundamento (ver
"O que não se fez").

## O que está bem (e deve ser preservado)

- **Dependências fixadas por versão exacta ou por *commit***, com o raciocínio
  escrito no `platformio.ini`: um *range* `^x.y.z` deixaria uma biblioteca
  mudar por baixo de uma *tag* já publicada, o que é precisamente o contrário
  do que a assinatura do OTA promete.
- **Testes de host a cobrir toda a lógica pura.** Antes desta avaliação eram
  18 ficheiros e *todos* os `*Logic.h`/puros tinham teste. A convenção — pôr
  a decisão que pode estar errada num cabeçalho sem Arduino e testá-la no PC
  — é a melhor coisa deste repositório e foi seguida nas alterações novas.
- **Comentários que explicam o *porquê* e citam o *postmortem*.** O comentário
  do `PIN_BUTTON` no `Config.h` a apontar para o documento da troca falhada de
  pinos vale mais do que qualquer quantidade de documentação genérica.
- **`Lock.h` com a ordem de aquisição escrita**, e com os três exemplos
  concretos de corrida que existiam antes dele.

## Defeitos encontrados

### D1. Escrita não atómica em nove dos onze *stores*

O mais grave. `ProgressStore` e `BookmarkStore` gravavam com `.tmp` + `rename`;
os outros faziam `open(FILE_WRITE)` — que trunca o ficheiro bom antes de
escrever fosse o que fosse — e ignoravam o valor devolvido pelo
`serializeJson`. Num aparelho que chama `esp_deep_sleep_start()` directamente,
uma falha de energia a meio deixa JSON cortado; e JSON cortado não falha a
meias, o `deserializeJson` recusa o ficheiro inteiro e leva consigo as
entradas de todos os outros livros.

Onde mais custava: `books_meta.json`, o mapa nome-truncado → nome-original.
Sem ele, o progresso de leitura (chaveado pelo nome original) deixa de
corresponder a qualquer ficheiro no aparelho.

Feito em [escrita-atómica](2026-09-18-escrita-atomica-json-design.md).

### D2. O corpo partilhado que faltava aos *stores*

Três coisas estavam escritas à mão em cada um dos onze: a estimativa de
capacidade do ArduinoJson, a decisão do que conta como gravação bem sucedida,
e o `reconcile()`. Os quatro *caches* de capítulos
(`ChapterToc`/`Narrative`/`GuideType`/`Length`) eram literalmente o mesmo
ficheiro escrito quatro vezes.

Nenhuma das dez cópias da aritmética de capacidade tinha protecção contra
transbordo do `size_t` — e um transbordo aí devolve um documento *pequeno*
para uma biblioteca *grande*, que é exactamente o caminho para o D1.

Feito no mesmo documento do D1. 593 linhas removidas.

### D3. `WebMgr::setupEndpoints()` com 1127 linhas

Uma única função com todas as rotas HTTP do projecto, do `/api/status` ao
`serveStatic`. O ponto mais difícil de navegar do repositório.

Dividida em oito funções por domínio (sistema, livros, OTA, definições,
leitor, estado da biblioteca, WiFi, ficheiros estáticos). **É só uma divisão**,
e isso foi verificado e não assumido: as 1007 linhas não vazias do corpo antigo
são exactamente as 1007 do conjunto das funções novas, e o conjunto de rotas
registadas é idêntico.

A ordem de registo só conta para o *handler* de `/`, que apanha tudo o que
sobra e por isso fica em último; entre rotas de API não há sobreposição, cada
uma tem um URI exacto. Duas mudaram de lugar por causa disso:
`/api/app/switch` e `/api/settings/sleep`.

Saíram de caminho cinco declarações mortas no `WebMgr.h` (`handleAPIStatus` e
companhia): declaradas, nunca definidas, nunca chamadas.

### D4. UI web servida sem compressão

97 KB de HTML/CSS/JS transferidos por inteiro a cada carregamento da página,
com o aparelho muitas vezes a servir por SoftAP.

Feito em [UI web comprimida](2026-09-18-ui-web-comprimida-design.md).

### D5. A verificação de actualizações mentia quando falhava

`checkUpdate()` colapsava todos os resultados num único `bool available`. Sem
rede, 403, 404, resposta ilegível — tudo voltava igual a "não há nada novo", e
os três sítios que falam com o utilizador diziam "estás actualizado".

Feito em
[verificação de updates](2026-09-18-verificacao-de-updates-design.md).

### D6. Ponteiros donos crus no leitor

`BookIndexer::_renderer` com `delete` em quatro sítios, `AppReader::_epubLoader`
e `_textRenderer` em três. Mantê-los em sincronia era trabalho à mão e nada o
verificava. Passaram a `std::unique_ptr` — primeiro uso de `<memory>` no
projecto. Sem mudança de comportamento: o tempo de vida é o mesmo, só deixou
de ser escrito à mão. Os seis sítios que passam o `_epubLoader` como argumento
levam `.get()`, porque quem o recebe não é dono.

## O que não se fez

### Fixar o CA da GitHub no OTA — recusado, com fundamento

Foi levantado nesta avaliação que o `http.begin(url)` estabelece TLS **sem
validar o certificado**. Está correcto, e foi confirmado no código do core
(`HTTPClient::begin(String)` com uma URL `https` cai em `begin(url, NULL)` →
`TLSTraits(nullptr)` → `wcs.setInsecure()`).

O que a avaliação inicial falhou foi o enquadramento: isto **já tinha sido
avaliado e rejeitado** em
[2026-07-21-ota-integrity-design.md](2026-07-21-ota-integrity-design.md), com
a razão exacta que se voltou a identificar como risco — os certificados raiz
rodam, e um CA embutido que caduque deixa o aparelho sem conseguir
actualizar, recuperável só por USB. A assinatura Ed25519 foi escolhida em vez
disso e implementada na v1.11.0.

Lição para avaliações futuras: **ler `docs/plans/` antes de propor**. Uma
decisão registada não é uma lacuna.

O que sobra desse risco — alguém a interferir com a ligação para manter o
aparelho numa versão antiga — foi fechado pelo lado do nosso código, no D5,
sem tocar na cadeia de confiança.

## Testabilidade

Os testes de host passaram de 18 para 20 ficheiros. Os dois novos seguem a
convenção: a decisão que pode estar errada num cabeçalho sem Arduino.

- `test_json_store.cpp` — a saturação da capacidade, a tabela de decisão de uma
  escrita (incluindo a escrita parcial num filesystem cheio, que antes passava
  por sucesso) e o `reconcile`.
- `test_update_check.cpp` — os dois estados que podem ser apresentados como
  resposta, cada falha em separado, e que as sete chaves da API não colidem.

O que continua sem cobertura automática é o mesmo de sempre e por boa razão:
ecrã, botões, WiFi e OTA. É o que o `TODO.txt` e o
[`RELEASE_CHECKLIST.md`](../RELEASE_CHECKLIST.md) existem para cobrir.

## Estado de verificação

Honestamente: **quase nada disto foi visto num e-ink real**. O CI compila e os
testes de host cobrem a lógica pura nova. Antes da *tag* `v1.26.0` foi
confirmado em hardware **um** ponto — o mais importante, a UI comprimida a
abrir — e esse saiu do `TODO.txt`. O resto continua lá por riscar.

## Resumo por prioridade

| | Ponto | Estado |
| --- | --- | --- |
| 1 | D1 escrita atómica | feito (#92) |
| 2 | D2 corpo partilhado dos *stores* | feito (#92) |
| 3 | D3 divisão do `setupEndpoints()` | feito (#93) |
| 4 | D4 UI web comprimida | feito (#93) |
| 5 | CA do OTA | recusado, ver acima |
| 6 | D6 `unique_ptr` | feito (#94) |
| + | D5 verificação de updates | feito (#95) |
