# Avaliação de crosspoint-reader/crosspoint-reader para possível adoção

Data: 2026-08-29
Estado: avaliação — sem alterações de código associadas

## Contexto

`crosspoint-reader/crosspoint-reader` (CrossPoint Reader) é outro leitor de
EPUB open-source para hardware ESP32, mas não é um fork do Book32 — é um
projeto independente, muito mais maduro e com uma comunidade ativa (badge de
funding via Royalty.dev, `GOVERNANCE.md`, discussion board de ideias). Vale a
pena avaliá-lo pelo mesmo motivo que se avaliou o `rolohaun/Book32`: procurar
lacunas que o Book32 tem e que outro projeto do mesmo domínio já resolveu
bem.

Clonado em raso (`--depth 1`) a partir do commit `b42927b`.

### Diferença de hardware — a limitação central desta avaliação

- **CrossPoint**: Xteink X3/X4, MCU **ESP32-C3** (~380 KB RAM utilizável,
  **sem PSRAM**), com **slot de cartão SD**. Grande parte da arquitetura do
  projeto (cache agressiva em `.crosspoint/` no SD, fontes `.cpfont`
  carregadas do SD, dicionários StarDict no SD, temas a mover para o SD) só
  faz sentido porque o dispositivo tem RAM muito apertada e um SD disponível
  para descarregar esse peso.
- **Book32**: Seeed XIAO **ESP32-S3** (8 MB PSRAM, 16 MB flash), **sem slot
  de SD** — o armazenamento de ebooks é uma partição LittleFS interna de
  10 MB. Não há cartão removível.

Isto significa que nenhuma feature do CrossPoint é portável por
copy-paste: o driver de display é diferente (`GxEPD2`/`bb_epaard` vs a HAL
própria do CrossPoint para o painel Xteink), a camada de armazenamento é
diferente (SD vs LittleFS interna), e o Book32 tem bastante mais RAM
disponível, pelo que não precisa da mesma disciplina de cache em disco. O
valor está nas **ideias, formatos e desenho de funcionalidade**, não no
código em si.

## O que o Book32 já cobre bem (sem lacuna)

- OTA com assinatura Ed25519 + digest incremental — mais rigoroso que o
  update-checker simples do CrossPoint baseado no feed de releases do
  GitHub (o CrossPoint também usa releases do GitHub, mas sem verificação
  de assinatura documentada além do checksum do instalador web).
- Progresso de leitura com resume no arranque, exportação/importação do
  estado da biblioteca.
- Interface web responsiva para telemóvel, fila de upload com progresso
  (`/send`).
- Testes de host cobrindo lógica core (`ProgressMergeLogic`,
  `BookOrderLogic`, `WordFitLogic`, etc.), como o CrossPoint também faz em
  `test/` (17 suites).

## Lacunas genuínas identificadas — por prioridade

### Alta prioridade

**1. Marcadores (bookmarks) no EPUB**

O CrossPoint tem marcadores como funcionalidade de primeira classe (landed
na Fase 0 do roadmap deles). O Book32 não tem nenhum mecanismo de marcação —
só a posição de leitura corrente (`ProgressStore`). É uma lacuna de UX
concreta e de esforço moderado: guardar uma lista de posições nomeadas por
livro (reutilizando o mesmo formato de posição — capítulo/página — já usado
pelo `ProgressStore`), com uma tecla ou menu para adicionar/remover/saltar
para marcador. Não depende de SD nem de arquitetura diferente.

**2. Dicionário offline (lookup de palavras)**

O CrossPoint suporta dicionários StarDict (`.idx`/`.dict`/`.dict.dz`/`.syn`)
com indexação lazy em sidecars (`.qidx`/`.sidx`) para lookup rápido —
formato bem documentado em `docs/dictionary.md` do repositório deles. Sem
SD no Book32, os dicionários teriam de viver na partição de ebooks (10 MB)
partilhada com os livros, o que é apertado mas viável para um único
dicionário compacto. Esforço alto (parser StarDict + UI de seleção de
palavra + navegação por linha/palavra no texto renderizado), mas é a
funcionalidade de leitura mais pedida tipicamente neste tipo de dispositivo
e o formato de ficheiro já está resolvido a montante — não é preciso
desenhar do zero.

### Média prioridade

**3. Hifenização**

O CrossPoint trata a hifenização como prioridade da Fase 2 do roadmap deles
(inclui planos para mover os dicionários de hifenização para fora da
firmware por serem grandes — o alemão sozinho tem ~200 KB). O Book32 atual
não hifeniza; para PT-PT isto tem impacto real na justificação de texto em
ecrãs estreitos. Como o Book32 tem 16 MB de flash e só uma língua-alvo
(português, a avaliar pelos comentários no código e `TODO.txt`), o custo de
incluir um dicionário de hifenização PT embutido na firmware é muito mais
aceitável aqui do que no caso multi-língua do CrossPoint — não é preciso
replicar a complexidade deles de carregamento sob demanda.

**4. Ir para percentagem (go-to-percent) e navegação de capítulos melhorada**

Funcionalidade simples do CrossPoint (saltar para X% do livro) que falta no
Book32 e é barata de implementar sobre a estrutura de secções/páginas já
existente em `AppReader`/`B32Reader`.

**5. Fontes TTF instaláveis pelo utilizador**

O Book32 usa apenas fontes GFX fixas compiladas na firmware
(`FontMgr.h`: FreeSans, Gelasio, Literata, Merriweather, SourceSerif4,
OpenSans — já com trabalho dedicado a acentuação PT-PT). O CrossPoint deixa
o utilizador converter TTF/OTF próprios num formato `.cpfont` e carregá-los
do SD sem reflash. Sem SD, a versão adaptada ao Book32 seria: aceitar upload
de um `.cpfont` (ou formato equivalente) pela interface web já existente, e
guardá-lo na partição de ebooks — reaproveitando a ferramenta de conversão
deles (`lib/EpdFont/scripts/fontconvert_sdcard.py`, referida como
reutilizável a partir do site deles) em vez de a reescrever. Esforço médio,
mas evita o ciclo de "só a equipa do Book32 pode adicionar fontes".

### Baixa prioridade / não recomendado sem mudança de hardware

- **OPDS browser, WebDAV, Calibre wireless connect, KOReader progress
  sync**: todas dependem de o dispositivo aceitar ligações de rede ativas
  para além do já existente servidor web de upload. São funcionalidades de
  valor real, mas o Book32 já tem uma via de transferência (upload web) que
  cobre o caso de uso principal; adicionar mais três protocolos de rede é
  esforço elevado para benefício incremental, e o próprio CrossPoint tem
  "novos conectores de rede externos" **fechado** no roadmap deles agora
  (fase de consolidação) — sinal de que nem eles consideram isto prioridade
  contínua.
- **Temas / múltiplas skins de UI**: o CrossPoint está a *tirar* isto da
  firmware para o SD por custar flash — não é uma direção a copiar sem SD
  disponível no Book32.
- **24 idiomas de UI (i18n) e RTL**: fora de âmbito. O Book32 é
  explicitamente um projeto de leitura em português; i18n completo com
  suporte RTL é um esforço enorme (MiniBidi, tabelas de tradução) sem
  utilizador-alvo.
- **Screenshots do ecrã, orientação configurável, auto page-turn, foco de
  leitura ("focus mode")**: features pequenas e independentes, sem
  dependência de SD. Vale considerar isoladamente no futuro, mas nenhuma
  resolve uma lacuna urgente hoje.
- **Camada HAL/SDK genérica para múltiplos dispositivos ESP32**: faz
  sentido para o CrossPoint porque eles suportam vários modelos Xteink e
  querem expandir para outro hardware. O Book32 tem um único alvo
  (`seeed_xiao_esp32s3` com o kit TRMNL) — introduzir essa abstração agora
  seria complexidade sem utilizador.

## Resumo

| # | Item | Esforço | Impacto | Bloqueado por falta de SD? | Recomendação |
|---|---|---|---|---|---|
| 1 | Marcadores (bookmarks) | Médio | Alto | Não | Adotar |
| 2 | Dicionário offline (StarDict) | Alto | Alto | Parcial (usar partição de ebooks) | Avaliar em detalhe / adotar |
| 3 | Hifenização (PT) | Médio | Médio-Alto | Não | Adotar |
| 4 | Ir para percentagem | Baixo | Médio | Não | Adotar |
| 5 | Fontes TTF instaláveis pelo utilizador | Médio | Médio | Parcial (usar partição de ebooks em vez de SD) | Considerar |
| 6 | OPDS / WebDAV / Calibre wireless / KOReader sync | Alto | Médio | Sim (arquitetura de rede) | Não priorizar agora |
| 7 | Temas / múltiplas skins | — | Baixo aqui | Sim | Não adotar |
| 8 | i18n multi-língua + RTL | Muito alto | Nulo (sem utilizador-alvo) | Não | Não adotar |
| 9 | HAL/SDK multi-dispositivo | Alto | Nulo (um único alvo) | Não | Não adotar |

O maior valor por esforço são os marcadores (#1) e o "ir para percentagem"
(#4) — ambos pequenos, sem qualquer dependência de SD, e resolvem lacunas de
UX imediatas na leitura. A hifenização (#3) é a próxima com melhor relação
custo/benefício dado o público português do Book32. O dicionário offline
(#2) é o item de maior impacto mas também o de maior esforço — vale abrir um
design doc próprio antes de arrancar, sobretudo para decidir onde viver
(partição de ebooks partilhada, com limite de tamanho) sem SD dedicado.
