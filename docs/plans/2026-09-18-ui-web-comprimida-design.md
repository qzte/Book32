# UI web comprimida na imagem do filesystem — design notes

Responde a D4 de
[2026-09-18-avaliacao-codigo-stores-web.md](2026-09-18-avaliacao-codigo-stores-web.md).
Saiu na v1.26.0 (PR #93).

## O problema

Os ficheiros de `data/` eram gravados no LittleFS tal e qual e servidos tal e
qual: 97 067 bytes de HTML/CSS/JS, dos quais o `script.js` sozinho são 59 944.
Isso é transferido por inteiro a cada carregamento da página. Numa LAN passa
despercebido; com o aparelho a servir pelo hotspot SoftAP — que é
precisamente o caso em que a UI web é a única forma de gerir o dispositivo —
nota-se bem.

## A solução

Comprimir na imagem, não no repositório. `tools/gzip_webui.py` corre como
*pre-script* do PlatformIO nos alvos `buildfs` e `uploadfs`, gzipa `data/`
para uma cópia dentro do `$BUILD_DIR` e aponta-lhe o `PROJECT_DATA_DIR`.

Medido no momento da alteração (o `script.js` cresceu depois, com a
verificação de updates do PR #95 — a proporção mantém-se):

```
index.html     16126 ->   3757  (23%)
script.js      59944 ->  15436  (26%)
send.html       7571 ->   2991  (40%)
style.css      13426 ->   3801  (28%)
TOTAL          97067 ->  25985  (27%)
```

**`data/` fica intocado.** É a decisão que mantém tudo o resto simples:
continua-se a editar os ficheiros normais, o CI continua a correr
`node --check data/script.js`, e nunca há duas versões do mesmo ficheiro no
repositório a divergirem. A alternativa — commitar os `.gz` — obrigaria a
regenerá-los à mão a cada edição e a confiar que alguém se lembrou.

O script só corre nos alvos que constroem a imagem (verifica
`COMMAND_LINE_TARGETS`); um `pio run` normal não paga por ele.

## Do lado do dispositivo: nada

Não foi preciso código de descompressão. O ESPAsyncWebServer já procura
`<caminho>.gz` quando o ficheiro simples não existe e responde com
`Content-Encoding: gzip` — tanto o `AsyncStaticWebHandler` (`serveStatic`)
como o `AsyncFileResponse` (`request->send(fs, caminho, tipo)`). **Verificado
no código da versão fixada** (`esphome/ESPAsyncWebServer-esphome 3.4.0`), não
assumido.

Um pormenor que valia a pena confirmar e se confirmou: o tipo de conteúdo sai
do nome **sem** `.gz` (o *handler* guarda o caminho pedido, não o do ficheiro
que abriu), por isso o `script.js.gz` é servido como JavaScript e não como
`application/x-gzip`.

### O que *era* preciso de nosso

Duas verificações de existência que decidem *se* há UI para servir:

```cpp
if (SystemFS.exists("/index.html"))   // registerStaticRoutes
if (SystemFS.exists("/send.html"))    // rota /send
```

Com só o `.gz` em disco, ambas dão `false` — e o aparelho responderia 404 à UI
inteira. Passaram por `webUiFileExists()`, que conhece as duas formas. Este é
o ponto que faz a diferença entre "funciona" e "a interface web desapareceu",
e não tem sintoma parcial nenhum que avise antes.

## Cache: `no-cache`, não um `max-age`

`serveStatic(...).setCacheControl("no-cache")`.

"no-cache" não é "não guardes", é "guarda mas confirma antes de reutilizar". O
browser passa a revalidar com o ETag que o próprio *handler* já põe (o tamanho
do ficheiro), e uma página que não mudou custa um 304 vazio em vez dos 26 KB
todos.

Um `max-age` a sério seria mais rápido ainda, e foi recusado: **a UI web é
substituída por OTA a qualquer momento**. Depois de actualizar, o browser
continuaria a servir a versão velha até o prazo passar — e isso é pior do que
uma ida ao aparelho numa rede local, que custa milissegundos.

O ETag ser o tamanho do ficheiro tem um caso patológico (duas versões que
comprimam para exactamente o mesmo número de bytes), aceite como
suficientemente improvável.

## Reprodutibilidade

O cabeçalho gzip leva um *timestamp* por omissão. Sem cuidado, duas
compilações do mesmo código produziriam `littlefs.bin` diferentes — e este
projecto **assina o SHA-256 desse ficheiro** (ver
[2026-08-23-ota-ed25519-signing-design.md](2026-08-23-ota-ed25519-signing-design.md)).
Daí o `mtime=0`. Confirmado determinístico.

É o tipo de detalhe que não dá erro nenhum: daria dois binários diferentes
para o mesmo código, e a discrepância só apareceria a quem tentasse reproduzir
uma release.

## Casos de borda

O script copia em vez de comprimir quando não compensa, e isso foi exercitado
contra um *stub* do env do PlatformIO:

- ficheiro incompressível (ruído) — fica o original, não cresce;
- ficheiro minúsculo — idem;
- não-texto (extensão fora da lista) — copiado tal e qual;
- subdirectórios — preservados.

E os quatro ficheiros reais descomprimem byte a byte iguais aos originais.

Na CI, o passo `buildfs` confirmou o resto:

```
Building FS image from '.pio/build/seeed_xiao_esp32s3/data_gz' directory
/index.html.gz
/style.css.gz
/send.html.gz
/script.js.gz
```

## Estado de verificação

É o único ponto da v1.26.0 **confirmado em hardware** antes da *tag*: a página
abre e o `/send` também. Foi verificado primeiro precisamente por ser o único
cujo modo de falha não tem sintoma parcial — ou a interface abre, ou fica
inacessível por inteiro em todos os aparelhos que actualizarem.

Fica por confirmar o 304 na segunda visita e a UI nova a aparecer depois de um
OTA em vez da antiga em cache.

## Fora de âmbito

- **Minificar** o `script.js`. O gzip já leva o grosso, e minificar obrigaria
  a uma ferramenta de build que este projecto não tem e a perder a
  correspondência entre o que se lê no repositório e o que corre no browser.
- **Servir os `.gz` a partir do repositório** — ver acima.
- **Comprimir os EPUB ou as capas** no EbookFS: já são formatos comprimidos.
