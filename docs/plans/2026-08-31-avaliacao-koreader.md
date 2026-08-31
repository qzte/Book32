# Avaliação do KOReader (`koreader/koreader`) para possível adoção de ideias

Data: 2026-08-31
Estado: avaliação — sem alterações de código associadas

## Contexto

Pedido: avaliar o repositório
[`koreader/koreader`](https://github.com/koreader/koreader) e identificar
melhorias aplicáveis ao Book32. Clonado em raso
(`git clone --depth 1`, commit `28b4f2d`) para inspeção directa do código —
`frontend/` (interface, em Lua), `plugins/` (funcionalidades opcionais,
`.koplugin`), `base/` é um submódulo git não obtido no clone raso (é o
`koreader-base`, que por sua vez embrulha o `crengine` — o motor de
renderização real, em C++).

### Diferença de escala — mais extrema do que nas avaliações anteriores

O KOReader não é comparável em escala ao `rolohaun/Book32` nem ao
`crosspoint-reader` (ambos firmware ESP32 dedicado, já avaliados em
`docs/plans/2026-08-29-avaliacao-crosspoint-reader.md`). É um leitor de
documentos maduro (licença AGPL, mais de uma década de desenvolvimento,
comunidade grande) que corre sobre **Linux real** — Kindle, Kobo, Cervantes,
PocketBook, reMarcável, Android, e um emulador para Linux/macOS — com
armazenamento em cartão/flash de GBs, RAM tipicamente na ordem das
centenas de MB a poucos GB, e um runtime **Lua** completo por cima de um
núcleo em C/C++ (`crengine`, derivado do CoolReader, mais `MuPDF` para
PDF/DjVu, `k2pdfopt` para reflow de PDF digitalizado).

O Book32 é firmware bare-metal para um único alvo (Seeed XIAO ESP32-S3, 8 MB
PSRAM, sem SD, partição LittleFS de 10 MB para livros), sem sistema
operativo nem linguagem de scripting embutida. **Nada do código do KOReader
é directamente portável** — nem o motor de renderização (`crengine` é uma
árvore DOM completa com layout CSS, muito maior do que a memória disponível
no ESP32-S3 seria capaz de sustentar tal como está), nem a arquitectura de
plugins Lua (exigiria embutir um interpretador Lua, algo que o projecto não
tem e que mudaria a natureza do firmware). O valor está exclusivamente em
**ideias de funcionalidade e desenho de UX**, tal como nas avaliações
anteriores — a diferença aqui é que a distância de arquitectura é maior
ainda, por isso as recomendações abaixo são deliberadamente sobre *o quê*
fazer, não *como o KOReader o faz por dentro*.

## O que o KOReader confirma de avaliações anteriores (sem nada de novo)

- **Dicionário offline via StarDict** (`.ifo`/`.idx`/`.dict`,
  `frontend/apps/reader/modules/readerdictionary.lua`): o KOReader também
  usa este formato como referência. Reforça a recomendação já feita na
  avaliação do CrossPoint (`docs/plans/2026-08-29-avaliacao-crosspoint-reader.md`,
  item 2) — dois leitores de referência independentes convergem no mesmo
  formato de ficheiro.
- **Hifenização multi-língua embutida**: o Book32 já implementou hifenização
  PT-PT (`docs/plans/2026-08-29-pt-hyphenation-design.md`), pelo que este
  ponto do KOReader já não é uma lacuna.
- **"Ir para %"**: já implementado no Book32
  (`docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md`).

## Lacunas genuínas que o KOReader ajuda a concretizar — por prioridade

### Alta prioridade

**1. TOC real, com geração heurística de recurso quando o EPUB não tem uma
boa**

A avaliação anterior de gestão de EPUB
(`docs/plans/2026-08-31-avaliacao-gestao-epub.md`, lacuna #2) já apontava a
ausência de leitura de `toc.ncx`/`nav.xhtml` no `EpubLoader`. O KOReader
acrescenta uma ideia concreta que vale a pena copiar: quando o EPUB não tem
TOC nenhuma (ou tem uma má), o `crengine` sabe construir uma
**"TOC alternativa"** por heurística, a partir dos títulos (`h1`-`h6`) e dos
saltos de tamanho de letra no documento
(`CreDocument:buildAlternativeToc()` /
`isTocAlternativeToc()` em `frontend/document/credocument.lua:1517-1527`).
O Book32 já tem o embrião exacto disto: `EpubLoader::parseHtmlToRichContent`
já classifica `h1`-`h4` como cabeçalhos e já tem uma heurística própria para
detectar números de capítulo (bloco curto e só dígitos → `STYLE_HEADER1`,
`EpubLoader.cpp:643-652`). Falta ligar essas pistas, já recolhidas durante o
parsing normal de cada capítulo, a uma lista de "capítulos com título" que a
UI possa mostrar e usar para saltar — sem precisar de um parser de
`nav.xhtml`/`toc.ncx` à parte para o caso comum em que o EPUB nem tem TOC
formal.

**2. Capas reais na biblioteca (com cache)**

Confirma a lacuna #1 da avaliação anterior. O KOReader tem isto como
funcionalidade central da lista de livros — o plugin
`coverbrowser.koplugin` substitui a lista de texto por uma grelha com
miniaturas reais, geridas por um gestor de cache dedicado
(`BookInfoManager`, referido em `plugins/coverbrowser.koplugin/main.lua:6`)
para não descomprimir a capa do ZIP a cada desenho da lista. A mesma ideia
aplica-se directamente ao Book32: extrair a capa (`cover-image` do manifest
OPF, já não lido hoje pelo `EpubLoader`), converter para bitmap mono uma vez
por livro, e guardar essa conversão (ex.: num ficheiro por livro na
partição de ebooks, ou junto do `BookTitleStore`) em vez de a repetir a cada
entrada na biblioteca — o mesmo raciocínio de cache que já existe no Book32
para o título (`BookTitleStore`, v1.17.0) e para a contagem de páginas
(`PageCountStore`). `drawBitmap()` já existe no driver
(`AppMainMenu.cpp:422`), por isso falta apenas a extracção/descodificação
da imagem, não o desenho.

### Média prioridade

**3. Popup de notas de rodapé em vez de saltar de página**

`frontend/apps/reader/modules/readerlink.lua:284-324`: quando um link
interno do EPUB parece ser uma nota de rodapé, o KOReader mostra o conteúdo
num popup em vez de navegar para lá e obrigar o leitor a voltar atrás. É uma
melhoria de UX genuína para livros académicos/anotados, mas tem um
pré-requisito que o `EpubLoader` não tem hoje: seguir `href="#id"` dentro do
mesmo capítulo (ou entre capítulos) até ao nó de destino. O parser actual
descarta toda a estrutura de links e âncoras durante `parseHtmlToRichContent`
— seria preciso guardar `id`/`href` como metadados dos nós antes de os
poder resolver. Esforço médio-alto; vale como funcionalidade a prazo, não
imediata.

**4. Rodapé de leitura mais informativo: páginas restantes no capítulo**

`frontend/apps/reader/modules/readerfooter.lua:38-51,262-304`: o KOReader
deixa escolher o que aparece no rodapé — página actual, páginas restantes
*no capítulo*, páginas restantes *no livro*, percentagem do capítulo. O
Book32 já mostra "Page N of M" (`AppReader.cpp:1141`) para o livro inteiro,
mas nada por capítulo. Como a paginação já é dinâmica por capítulo
(`TextRenderer::renderRichPageDynamic`), mostrar "faltam X páginas neste
capítulo" é sobretudo uma questão de, tal como já acontece para o total do
livro (`startTotalPagesCounting`), paginar o resto do capítulo actual em
fatias de orçamento de tempo e cachear o resultado por capítulo — reaproveita
o mesmo padrão já usado, não introduz um conceito novo.

**5. Estatísticas de leitura leves**

`plugins/statistics.koplugin/main.lua`: o KOReader guarda tempo de leitura
por página numa base SQLite, com vista de calendário. Uma base SQLite
embutida não faz sentido no Book32 (sem essa dependência hoje, memória
apertada), mas a ideia em versão leve — tempo total lido por livro, nº de
dias consecutivos de leitura ("streak"), última sessão — dá para guardar
como mais um campo pequeno no `ProgressStore` (que já guarda datas de
início/conclusão desde v1.16.0, ver
`docs/plans/2026-08-29-gestao-biblioteca-e-estado-design.md`) sem precisar
de SQL nem de um ficheiro novo.

### Baixa prioridade / não recomendado sem mudança de âmbito

- **OPDS, Calibre wireless, Wallabag, Google Translate, Wikipedia**: todos
  dependem de o dispositivo manter ligações de rede activas para serviços
  externos. Mesmo raciocínio já aplicado ao CrossPoint (item "Baixa
  prioridade" dessa avaliação): o Book32 tem uma via de transferência
  (upload web) que cobre o caso de uso principal; multiplicar protocolos de
  rede é esforço alto para benefício incremental num dispositivo de leitura
  offline.
- **KOSync (sincronização de progresso entre dispositivos)**: resolve um
  problema — o mesmo livro em vários leitores — que não existe no Book32:
  cada dispositivo é a única cópia do utilizador. Precisaria de um servidor
  próprio ou de conta num serviço externo; sem outro dispositivo Book32 para
  sincronizar, não há para onde sincronizar.
- **Outros formatos (PDF, DjVu, MOBI, CHM, FB2, CBZ/CBT)**: cada um destes
  exige um motor de renderização totalmente diferente do `EpubLoader`
  actual (o PDF do KOReader vem do MuPDF, um motor gigantesco). Fora de
  âmbito para um leitor cujo propósito é EPUB em português.
- **Arquitectura de plugins Lua**: o KOReader é extensível porque tem um
  interpretador Lua embutido como base de toda a interface. Embutir Lua (ou
  outra linguagem de scripting) no Book32 só para ter plugins seria uma
  mudança de arquitectura enorme sem utilizador que a peça hoje.
- **k2pdfopt / reflow de digitalizações**: não aplicável — o Book32 não lê
  PDF.

## Resumo

| # | Item | Esforço | Impacto | Nota |
|---|---|---|---|---|
| 1 | TOC com títulos de capítulo (heurística sobre o parsing já existente) | Médio | Alto | Aproveita heurísticas de cabeçalho já implementadas no `EpubLoader`. |
| 2 | Capas reais na biblioteca, com cache por livro | Médio | Alto | `drawBitmap` já existe; falta extracção/descodificação + cache no padrão do `BookTitleStore`/`PageCountStore`. |
| 3 | Popup de notas de rodapé | Médio-Alto | Médio | Exige resolver `href`/`id` internos, hoje descartados no parsing. |
| 4 | "Páginas restantes no capítulo" no rodapé | Baixo-Médio | Médio | Mesma técnica de contagem em fatias já usada para o total do livro. |
| 5 | Estatísticas de leitura leves (tempo total, streak) | Baixo | Baixo-Médio | Versão sem SQLite, integrada no `ProgressStore` existente. |
| 6 | OPDS / Calibre wireless / Wallabag / tradutor / Wikipedia | Alto | Médio | Não priorizar — dependência de rede persistente, fora do modelo actual. |
| 7 | Sincronização entre dispositivos (KOSync) | — | Nulo aqui | Sem segundo dispositivo Book32 do mesmo utilizador, não resolve nada. |
| 8 | Outros formatos de documento (PDF, MOBI, CBZ, …) | Muito alto | Fora de âmbito | Motor de render diferente por formato; produto é "leitor de EPUB em PT". |
| 9 | Plugins via linguagem de scripting embutida | Muito alto | Nulo sem utilizador | Mudança de arquitectura, não uma funcionalidade. |

O maior valor por esforço é a TOC com títulos de capítulo (#1) precisamente
porque o Book32 já faz metade do trabalho (detecção de cabeçalhos) sem o
saber usar para navegação — e junta-se de forma natural à lacuna já
identificada na avaliação de gestão de EPUB. As capas reais (#2) continuam
a ser a melhoria de imagem mais visível, como já concluído nessa mesma
avaliação; o KOReader só confirma que vale a pena cachear a conversão em
vez de a repetir. #3-#5 são incrementos de UX de leitura razoáveis para
depois dessas duas; o resto exige mudanças de âmbito ou arquitectura que não
se justificam para o Book32 tal como está definido hoje.
