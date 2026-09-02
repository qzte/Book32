# Enviar Livros Diretamente Para o Book32 — Implementation Plan

Estado: implementado — `UploadGuard.h`, `data/send.html`, mDNS (`book32.local`)
e a escrita para `.part` com rename atómico estão todos em `main`. Os
checkboxes abaixo ficam por marcar porque documentam os passos originais, não
o estado actual; mantido como referência de arquitectura.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adicionar uma página de envio dedicada (`GET /send`) servida pelo device, com mDNS (`book32.local`), e endurecer o endpoint de upload para reportar erros reais.

**Architecture:** A lógica de decisão (aceitar/rejeitar upload) vive num header puro `UploadGuard.h`, testado no host com g++, seguindo a convenção de `FileExt.h`/`SafeName.h`. O handler `/api/books/upload` em `WebMgr.cpp` passa a ser uma casca fina à volta dessa função, escreve para `<nome>.part` e só renomeia no fim. `data/send.html` é uma página single-file servida pela partição `spiffs`.

**Tech Stack:** C++17 (Arduino/ESP32-S3, PlatformIO), ESPAsyncWebServer, LittleFS, ESPmDNS, HTML/CSS/JS vanilla.

**Spec:** `docs/plans/2026-07-24-send-to-device-design.md`

---

## Estrutura de ficheiros

| Ficheiro | Responsabilidade | Estado |
| --- | --- | --- |
| `lib/Book32_Core/UploadGuard.h` | decisão pura: extensão, nome seguro, espaço | criar |
| `tools/tests/test_upload_guard.cpp` | teste host da decisão | criar |
| `data/send.html` | UI de envio single-file | criar |
| `lib/Book32_Web/WebMgr.cpp` | handler de upload, rota `/send`, mDNS, limpeza `.part` | modificar |

Ordem: primeiro a lógica pura com teste (Tasks 1–2), depois o handler (Tasks 3–5), depois a página e o mDNS (Tasks 6–7).

---

### Task 1: `UploadGuard.h` — decisão pura de aceitação

**Files:**
- Create: `lib/Book32_Core/UploadGuard.h`
- Test: `tools/tests/test_upload_guard.cpp`

Contexto que o implementador precisa: `lib/Book32_Core/SafeName.h` já expõe
`isSafeBookName(name)`, que valida separadores, `..`, caracteres de controlo,
comprimento máximo **e** a allow-list de extensões (`.epub`, `.ttf`).
`lib/Book32_Core/FileExt.h` expõe `hasExtensionCI(name, ".epub")`. Ambos são
templates que funcionam com `std::string` e com `String` do Arduino.

`checkUpload` distingue extensão inválida (HTTP 415) de nome inseguro (HTTP
400), por isso testa a extensão **primeiro**, com `hasExtensionCI`, e só depois
delega o resto em `isSafeBookName`.

O parâmetro `contentLength` é o tamanho do corpo multipart, que é sempre
**maior** que o ficheiro em si — usá-lo é conservador, portanto seguro. A
margem `BOOK32_UPLOAD_SLACK` cobre o overhead de metadados do LittleFS.

- [ ] **Step 1: Escrever o teste que falha**

Criar `tools/tests/test_upload_guard.cpp`:

```cpp
// Book32 — host test for upload admission control.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_upload_guard.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "UploadGuard.h"

int main() {
    using std::string;
    const size_t BIG = 10u * 1024u * 1024u;

    // Extensões aceites, incluindo maiúsculas.
    assert(checkUpload(string("book.epub"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("Book.EPUB"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("font.TTF"), 1000, BIG) == UploadVerdict::Ok);

    // Extensões rejeitadas.
    assert(checkUpload(string("book.pdf"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string("book"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string(""), 1000, BIG) == UploadVerdict::BadExtension);

    // Nome com extensão válida mas inseguro.
    assert(checkUpload(string("../a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir/a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir\\a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);

    // A extensão é verificada antes do nome: um nome inseguro com extensão
    // inválida reporta BadExtension.
    assert(checkUpload(string("../a.pdf"), 1000, BIG) == UploadVerdict::BadExtension);

    // Espaço: cabe exatamente com a folga incluída.
    assert(checkUpload(string("a.epub"), 1000, 1000 + BOOK32_UPLOAD_SLACK) == UploadVerdict::Ok);
    // Falta 1 byte.
    assert(checkUpload(string("a.epub"), 1000, 1000 + BOOK32_UPLOAD_SLACK - 1) == UploadVerdict::NoSpace);
    // Partição cheia.
    assert(checkUpload(string("a.epub"), 1000, 0) == UploadVerdict::NoSpace);

    // contentLength desconhecido (0) não é motivo para rejeitar: o handler
    // valida depois, byte a byte, pelo retorno de write().
    assert(checkUpload(string("a.epub"), 0, BIG) == UploadVerdict::Ok);
    // ...mas com a partição cheia continua a ser NoSpace.
    assert(checkUpload(string("a.epub"), 0, 0) == UploadVerdict::NoSpace);

    printf("All 16 tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Correr o teste e confirmar que falha**

```bash
g++ -std=c++17 -I lib/Book32_Core tools/tests/test_upload_guard.cpp -o /tmp/tug
```

Esperado: FALHA na compilação, `fatal error: UploadGuard.h: No such file or directory`.

- [ ] **Step 3: Escrever a implementação mínima**

Criar `lib/Book32_Core/UploadGuard.h`:

```cpp
#pragma once
// Book32 — admission control for book/font uploads.
//
// Rationale: /api/books/upload respondia 200 mesmo quando o ficheiro não era
// aberto ou a partição estava cheia, por isso a UI não conseguia distinguir
// sucesso de falha. A decisão de aceitar um upload é lógica pura e vive aqui,
// separada do handler assíncrono, para ser testável sem hardware.
//
// Pure template — funciona com Arduino String e std::string.
// Host-testable: tools/tests/test_upload_guard.cpp.

#include <cstddef>
#include "FileExt.h"
#include "SafeName.h"

// Folga para metadados do LittleFS (blocos, entradas de diretório). Um EPUB
// que caiba "à justa" ainda assim falharia a escrever sem esta margem.
#ifndef BOOK32_UPLOAD_SLACK
#define BOOK32_UPLOAD_SLACK 8192
#endif

enum class UploadVerdict { Ok, BadExtension, UnsafeName, NoSpace };

// `filename` deve já ser um basename (sem componentes de diretório) — o
// handler extrai-o antes de chamar. `contentLength` é o tamanho do corpo
// multipart, sempre maior que o ficheiro, logo uma estimativa conservadora;
// 0 significa desconhecido e não bloqueia por si só.
template <typename S>
UploadVerdict checkUpload(const S& filename, size_t contentLength, size_t freeBytes) {
    if (!hasExtensionCI(filename, ".epub") && !hasExtensionCI(filename, ".ttf")) {
        return UploadVerdict::BadExtension;
    }
    if (!isSafeBookName(filename)) {
        return UploadVerdict::UnsafeName;
    }
    if (freeBytes < contentLength + BOOK32_UPLOAD_SLACK) {
        return UploadVerdict::NoSpace;
    }
    return UploadVerdict::Ok;
}
```

- [ ] **Step 4: Correr o teste e confirmar que passa**

```bash
g++ -std=c++17 -I lib/Book32_Core tools/tests/test_upload_guard.cpp -o /tmp/tug && /tmp/tug
```

Esperado: `All 16 tests passed.`

- [ ] **Step 5: Commit**

```bash
git add lib/Book32_Core/UploadGuard.h tools/tests/test_upload_guard.cpp
git commit -m "feat(core): UploadGuard com decisao pura de aceitacao de uploads"
```

---

### Task 2: Confirmar que os testes host existentes continuam a passar

`UploadGuard.h` inclui `SafeName.h` e `FileExt.h`; esta task garante que nada
regrediu neles.

**Files:**
- Test: `tools/tests/*.cpp` (sem alterações)

- [ ] **Step 1: Compilar e correr todos os testes host**

```bash
for t in tools/tests/test_*.cpp; do
  echo "== $t"
  g++ -std=c++17 -I lib/Book32_Core -I lib/Book32_Update "$t" -o /tmp/t.out && /tmp/t.out
done
```

Esperado: cada teste imprime a sua linha `All N tests passed.` e nenhum
comando falha. Se algum teste não compilar por falta de include path, ajustar
apenas o `-I` desse teste, sem tocar no código de produção.

- [ ] **Step 2: Sem commit**

Nada mudou. Se algum teste falhar, parar e reportar antes de avançar.

---

### Task 3: Handler de upload — validação e resposta com estado real

**Files:**
- Modify: `lib/Book32_Web/WebMgr.cpp:549-621` (handler `/api/books/upload`)

Estado atual: o body handler abre `EbookFS.open(savedPath, FILE_WRITE)` e
escreve sem verificar retornos; o response handler responde sempre
`200 "Upload Complete"`. A sanitização do nome é inline (strip de `/` e `\`,
truncagem a 28 chars, sufixo `_1..99`).

Alvo desta task: manter a sanitização existente, mas passar o nome já
sanitizado por `checkUpload`, guardar o veredito/erro em estado partilhado, e
responder com o status correto e JSON. O `.part` entra na Task 4.

Nota importante sobre a ordem de execução: no ESPAsyncWebServer o body handler
corre **antes** do response handler (já documentado no comentário existente na
linha 559), portanto o veredito calculado no body handler está disponível
quando a resposta é montada.

- [ ] **Step 1: Adicionar o include**

Em `lib/Book32_Web/WebMgr.cpp`, junto aos outros includes de core (a seguir a
`#include "../Book32_Core/SafeName.h"`):

```cpp
#include "../Book32_Core/UploadGuard.h"
```

- [ ] **Step 2: Substituir o handler**

Substituir todo o bloco `server->on("/api/books/upload", ...)` (linhas 549-621)
por:

```cpp
    // API: Upload Book to EbookFS
    //
    // Estado partilhado entre o body handler e o response handler. O
    // ESPAsyncWebServer corre o body handler primeiro, por isso o veredito
    // aqui guardado já está decidido quando a resposta é montada. Uploads são
    // servidos em série (o LittleFS é single-writer), logo `static` é seguro.
    server->on("/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!requireAuth(request)) return;
            switch (g_uploadState.status) {
                case UploadStatus::Ok: {
                    String body = "{\"ok\":true,\"name\":\"" +
                                  jsonEscape(g_uploadState.finalName) + "\"}";
                    request->send(200, "application/json", body);
                    break;
                }
                case UploadStatus::BadExtension:
                    request->send(415, "application/json",
                        "{\"ok\":false,\"error\":\"tipo de ficheiro nao suportado\"}");
                    break;
                case UploadStatus::UnsafeName:
                    request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"nome de ficheiro invalido\"}");
                    break;
                case UploadStatus::NoSpace:
                    request->send(507, "application/json",
                        "{\"ok\":false,\"error\":\"sem espaco na particao de ebooks\"}");
                    break;
                case UploadStatus::WriteFailed:
                default:
                    request->send(500, "application/json",
                        "{\"ok\":false,\"error\":\"falha a escrever no armazenamento\"}");
                    break;
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            // v1.5.0: the body handler runs before the response handler, so it
            // must reject unauthenticated uploads itself or data would be
            // written to flash before the 401 is sent.
            if (!request->authenticate(BOOK32_AUTH_USER, WebMgr::devicePassword())) return;

            if (index == 0) {
                g_uploadState.reset();

                String safeName = filename;
                int lastSlash = safeName.lastIndexOf('/');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);
                lastSlash = safeName.lastIndexOf('\\');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);

                if (safeName.length() > 28) {
                    int dotPos = safeName.lastIndexOf('.');
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    safeName = safeName.substring(0, 28 - ext.length()) + ext;
                }

                size_t freeBytes = EbookFS.totalBytes() - EbookFS.usedBytes();
                switch (checkUpload(safeName, request->contentLength(), freeBytes)) {
                    case UploadVerdict::BadExtension:
                        g_uploadState.status = UploadStatus::BadExtension;
                        return;
                    case UploadVerdict::UnsafeName:
                        g_uploadState.status = UploadStatus::UnsafeName;
                        return;
                    case UploadVerdict::NoSpace:
                        g_uploadState.status = UploadStatus::NoSpace;
                        return;
                    case UploadVerdict::Ok:
                        break;
                }

                String testPath = "/" + safeName;
                if (EbookFS.exists(testPath)) {
                    int dotPos = safeName.lastIndexOf('.');
                    String baseName = (dotPos != -1) ? safeName.substring(0, dotPos) : safeName;
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";

                    if (baseName.length() > 20) baseName = baseName.substring(0, 20);

                    int suffix = 1;
                    while (suffix < 100) {
                        safeName = baseName + "_" + String(suffix) + ext;
                        testPath = "/" + safeName;
                        if (!EbookFS.exists(testPath)) break;
                        suffix++;
                    }
                }

                g_uploadState.finalName = safeName;
                g_uploadState.originalName = filename;
                int origSlash = g_uploadState.originalName.lastIndexOf('/');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);
                origSlash = g_uploadState.originalName.lastIndexOf('\\');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);

                g_uploadState.path = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n",
                              g_uploadState.path.c_str(), filename.c_str());
                g_uploadState.file = EbookFS.open(g_uploadState.path, FILE_WRITE);
                if (!g_uploadState.file) {
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                g_uploadState.status = UploadStatus::Ok;
            }

            // Um chunk depois de um erro é descartado: nada foi aberto.
            if (g_uploadState.status != UploadStatus::Ok) return;

            if (g_uploadState.file && len) {
                if (g_uploadState.file.write(data, len) != len) {
                    Serial.println("Upload: write failed (disco cheio?)");
                    g_uploadState.file.close();
                    EbookFS.remove(g_uploadState.path);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
            }

            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
            }
        }
    );
```

- [ ] **Step 3: Adicionar o estado partilhado**

Imediatamente antes de `void WebMgr::setupEndpoints() {` (linha 450), inserir:

```cpp
// Estado de um upload em curso. Partilhado entre o body handler (que decide) e
// o response handler (que reporta). Uploads são feitos em série pelo cliente e
// o LittleFS é single-writer, por isso não há concorrência a proteger aqui.
enum class UploadStatus { Ok, BadExtension, UnsafeName, NoSpace, WriteFailed };

struct UploadState {
    UploadStatus status = UploadStatus::WriteFailed;
    File file;
    String path;
    String finalName;
    String originalName;

    void reset() {
        if (file) file.close();
        status = UploadStatus::WriteFailed;
        path = "";
        finalName = "";
        originalName = "";
    }
};

static UploadState g_uploadState;
```

`jsonEscape` já existe neste ficheiro (é usada em `/api/books`), por isso não
precisa de ser declarada. Confirmar que `jsonEscape` está definida **acima**
deste ponto; se estiver definida mais abaixo, mover apenas a sua declaração
(protótipo) para junto do topo do ficheiro.

- [ ] **Step 4: Compilar**

```bash
python -m platformio run
```

Esperado: `SUCCESS`. Erros típicos: `File` requer `<LittleFS.h>` (já incluído);
`jsonEscape` não declarada (ver Step 3).

- [ ] **Step 5: Commit**

```bash
git add lib/Book32_Web/WebMgr.cpp
git commit -m "fix(web): upload reporta erros reais (415/400/507/500) em vez de 200 sempre"
```

---

### Task 4: Escrita atómica com `.part`

**Files:**
- Modify: `lib/Book32_Web/WebMgr.cpp` (handler de upload, dentro do bloco da Task 3)
- Modify: `lib/Book32_Web/WebMgr.cpp:~180` (fim de `mountFilesystems`)

Problema: se a ligação cair a meio, o `final` nunca chega, o ficheiro fica
fechado pelo destrutor e aparece na biblioteca como um EPUB truncado. A
solução é escrever para `<nome>.epub.part` e renomear só no `final`.
`/api/books` filtra por `hasExtensionCI(name, ".epub")`, por isso um `.part`
é automaticamente invisível na listagem — não é preciso alterar essa rota.

- [ ] **Step 1: Escrever para `.part`**

No body handler, substituir a linha:

```cpp
                g_uploadState.file = EbookFS.open(g_uploadState.path, FILE_WRITE);
```

por:

```cpp
                g_uploadState.tempPath = g_uploadState.path + ".part";
                g_uploadState.file = EbookFS.open(g_uploadState.tempPath, FILE_WRITE);
```

Substituir, no ramo de erro de escrita:

```cpp
                    EbookFS.remove(g_uploadState.path);
```

por:

```cpp
                    EbookFS.remove(g_uploadState.tempPath);
```

E substituir o bloco `final`:

```cpp
            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
            }
```

por:

```cpp
            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                if (!EbookFS.rename(g_uploadState.tempPath, g_uploadState.path)) {
                    Serial.println("Upload: rename do .part falhou");
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
            }
```

- [ ] **Step 2: Acrescentar `tempPath` ao estado**

Na `struct UploadState` adicionada na Task 3, acrescentar o campo e limpá-lo no
`reset()`:

```cpp
    String tempPath;
```

e dentro de `reset()`, junto às outras limpezas:

```cpp
        tempPath = "";
```

- [ ] **Step 3: Limpar `.part` órfãos no arranque**

Em `WebMgr::mountFilesystems()`, dentro do bloco `if (ebookOK) { ... }`, logo
a seguir a `listFiles(EbookFS, "/", 1);`, inserir:

```cpp
        // Uploads interrompidos deixam ficheiros .part. Como não são listados
        // por /api/books nem abertos pelo leitor, só ocupam espaço — limpar no
        // arranque, que é o único momento em que nenhum upload está em curso.
        {
            std::vector<String> stale;
            File root = EbookFS.open("/");
            if (root && root.isDirectory()) {
                File f = root.openNextFile();
                while (f) {
                    String n = f.name();
                    if (hasExtensionCI(n, ".part")) stale.push_back(n);
                    f.close();
                    f = root.openNextFile();
                }
                root.close();
            }
            for (const String& n : stale) {
                Serial.printf("A remover upload incompleto: %s\n", n.c_str());
                EbookFS.remove("/" + n);
            }
        }
```

O vetor intermédio é necessário porque remover ficheiros durante a iteração de
um diretório LittleFS tem comportamento indefinido.

- [ ] **Step 4: Compilar**

```bash
python -m platformio run
```

Esperado: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add lib/Book32_Web/WebMgr.cpp
git commit -m "fix(web): upload atomico via .part e limpeza de parciais no arranque"
```

---

### Task 5: Rota `GET /send`

**Files:**
- Modify: `lib/Book32_Web/WebMgr.cpp` (dentro de `setupEndpoints`, junto às outras rotas)

O `serveStatic("/", SystemFS, "/")` existente (linha ~985) só serve
`/send.html`. A rota curta `/send` tem de ser registada explicitamente, e tem
de ser registada **antes** do `serveStatic`, porque este é apanha-tudo.
`setupEndpoints()` já corre antes de `setupStaticRoutes` — confirmar essa
ordem ao implementar; se o `serveStatic` estiver na mesma função e mais acima,
mover o registo de `/send` para cima dele.

- [ ] **Step 1: Registar a rota**

Em `setupEndpoints()`, a seguir ao bloco `/api/status`, inserir:

```cpp
    // Página de envio dedicada: caminho curto e memorizável para o atalho no
    // ecrã principal do telemóvel. Sem auth aqui — a página em si não expõe
    // nada; o POST para /api/books/upload é que exige Basic Auth.
    server->on("/send", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SystemFS.exists("/send.html")) {
            request->send(SystemFS, "/send.html", "text/html");
        } else {
            request->send(404, "text/plain", "send.html nao encontrado - correr uploadfs");
        }
    });
```

- [ ] **Step 2: Compilar**

```bash
python -m platformio run
```

Esperado: `SUCCESS`.

- [ ] **Step 3: Commit**

```bash
git add lib/Book32_Web/WebMgr.cpp
git commit -m "feat(web): rota GET /send"
```

---

### Task 6: `data/send.html` — página de envio

**Files:**
- Create: `data/send.html`

Requisitos: single-file, sem dependências externas (o device não tem saída para
a internet garantida), legível em ecrã de telemóvel, envio em série,
progresso por ficheiro, erro por ficheiro com retry.

- [ ] **Step 1: Criar a página**

```html
<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Enviar para o Book32</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: system-ui, sans-serif; margin: 0; padding: 1.25rem;
         max-width: 34rem; margin-inline: auto; }
  h1 { font-size: 1.25rem; margin: 0 0 1rem; }
  #drop { border: 2px dashed currentColor; border-radius: 12px; padding: 2rem 1rem;
          text-align: center; opacity: .75; cursor: pointer; }
  #drop.over { opacity: 1; background: rgba(127,127,127,.15); }
  input[type=file] { display: none; }
  ul { list-style: none; padding: 0; margin: 1rem 0 0; }
  li { border-top: 1px solid rgba(127,127,127,.35); padding: .6rem 0; }
  .row { display: flex; gap: .5rem; align-items: baseline; }
  .name { flex: 1; word-break: break-all; }
  .state { font-variant-numeric: tabular-nums; white-space: nowrap; }
  .ok { color: #17803d; } .err { color: #b3261e; }
  progress { width: 100%; height: .4rem; margin-top: .35rem; }
  button { font: inherit; padding: .2rem .6rem; }
  #free { opacity: .7; font-size: .85rem; margin-top: .75rem; }
</style>
</head>
<body>
<h1>Enviar para o Book32</h1>

<div id="drop" tabindex="0">
  Larga aqui os ficheiros<br><small>ou toca para escolher (.epub, .ttf)</small>
</div>
<input id="picker" type="file" multiple accept=".epub,.ttf">

<ul id="list"></ul>
<div id="free"></div>

<script>
const drop = document.getElementById('drop');
const picker = document.getElementById('picker');
const list = document.getElementById('list');
const free = document.getElementById('free');
const queue = [];
let busy = false;

drop.addEventListener('click', () => picker.click());
drop.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') picker.click(); });
picker.addEventListener('change', () => { add(picker.files); picker.value = ''; });

['dragenter', 'dragover'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.add('over');
}));
['dragleave', 'drop'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.remove('over');
}));
drop.addEventListener('drop', e => add(e.dataTransfer.files));

function add(files) {
  for (const file of files) {
    const li = document.createElement('li');
    li.innerHTML = '<div class="row"><span class="name"></span>' +
                   '<span class="state">na fila</span></div>' +
                   '<progress value="0" max="100"></progress>';
    li.querySelector('.name').textContent = file.name;
    list.appendChild(li);
    queue.push({ file, li });
  }
  pump();
}

// Envio em série: o LittleFS do device é single-writer e o servidor assíncrono
// não lida bem com uploads concorrentes.
function pump() {
  if (busy) return;
  const job = queue.shift();
  if (!job) { refreshFree(); return; }
  busy = true;
  send(job).finally(() => { busy = false; pump(); });
}

function send(job) {
  const { file, li } = job;
  const state = li.querySelector('.state');
  const bar = li.querySelector('progress');
  state.className = 'state';
  state.textContent = '0%';
  bar.value = 0;

  return new Promise(resolve => {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/books/upload');
    xhr.withCredentials = true;

    xhr.upload.onprogress = e => {
      if (!e.lengthComputable) return;
      const pct = Math.round(e.loaded / e.total * 100);
      bar.value = pct;
      state.textContent = pct + '%';
    };

    xhr.onload = () => {
      bar.value = 100;
      let body = {};
      try { body = JSON.parse(xhr.responseText); } catch (_) {}
      if (xhr.status === 200 && body.ok) {
        state.className = 'state ok';
        // O device pode truncar o nome ou juntar um sufixo anti-colisão.
        state.textContent = '✓';
        if (body.name && body.name !== file.name) {
          li.querySelector('.name').textContent = file.name + ' → ' + body.name;
        }
      } else if (xhr.status === 401) {
        fail(li, 'credenciais recusadas', job);
      } else {
        fail(li, body.error || ('erro ' + xhr.status), job);
      }
      resolve();
    };

    xhr.onerror = () => {
      fail(li, 'device inacessivel - acorda o Book32 e tenta de novo', job);
      resolve();
    };
    xhr.ontimeout = () => { fail(li, 'timeout', job); resolve(); };

    const form = new FormData();
    form.append('file', file, file.name);
    xhr.send(form);
  });
}

function fail(li, msg, job) {
  const state = li.querySelector('.state');
  state.className = 'state err';
  state.textContent = '✗';
  const row = li.querySelector('.row');
  if (!row.querySelector('.msg')) {
    const span = document.createElement('span');
    span.className = 'msg err';
    span.style.flexBasis = '100%';
    row.appendChild(span);
  }
  row.querySelector('.msg').textContent = msg;
  if (!li.querySelector('button')) {
    const btn = document.createElement('button');
    btn.textContent = 'repetir';
    btn.onclick = () => {
      btn.remove();
      row.querySelector('.msg').textContent = '';
      queue.push(job);
      pump();
    };
    row.appendChild(btn);
  }
}

function refreshFree() {
  fetch('/api/status').then(r => r.json()).then(s => {
    free.textContent = 'Livre: ' + (s.freeSpace / 1048576).toFixed(1) + ' MB de ' +
                       (s.totalSpace / 1048576).toFixed(1) + ' MB';
  }).catch(() => { free.textContent = ''; });
}
refreshFree();
</script>
</body>
</html>
```

- [ ] **Step 2: Verificar que cabe na partição**

```bash
python -m platformio run --target buildfs
ls -l .pio/build/seeed_xiao_esp32s3/littlefs.bin
```

Esperado: build `SUCCESS` e imagem de 1 MB (o tamanho da partição; o que
importa é o build não falhar por falta de espaço).

- [ ] **Step 3: Commit**

```bash
git add data/send.html
git commit -m "feat(web): pagina /send com drag-drop, fila e progresso"
```

---

### Task 7: mDNS (`book32.local`)

**Files:**
- Modify: `lib/Book32_Web/WebMgr.cpp` (includes e `WebMgr::init`, linhas ~196-206)

`WebMgr::init()` é chamado depois do WiFi ligar, que é exatamente onde o mDNS
tem de arrancar. `ESPmDNS.h` vem com o core ESP32 do Arduino — não é preciso
mexer em `lib_deps`.

- [ ] **Step 1: Adicionar o include**

Junto aos outros includes de sistema em `lib/Book32_Web/WebMgr.cpp`, a seguir a
`#include <WiFi.h>`:

```cpp
#include <ESPmDNS.h>
```

- [ ] **Step 2: Arrancar o mDNS**

Em `WebMgr::init()`, substituir:

```cpp
    server->begin();
    _initialized = true;
    Serial.println("Web Server Started");
```

por:

```cpp
    server->begin();
    _initialized = true;
    Serial.println("Web Server Started");

    // mDNS: http://book32.local/send funciona sem saber o IP. Falha
    // silenciosamente em redes que bloqueiam multicast — o IP continua a
    // funcionar, por isso isto nunca é fatal.
    if (MDNS.begin("book32")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://book32.local/");
    } else {
        Serial.println("mDNS: arranque falhou (o IP continua a funcionar)");
    }
```

- [ ] **Step 3: Parar o mDNS quando o WiFi desliga**

Em `WebMgr::stop()`, antes de o servidor ser parado, inserir:

```cpp
    MDNS.end();
```

Isto evita que o serviço fique anunciado depois de o rádio desligar, quando o
leitor faz `WiFi.mode(WIFI_OFF)`.

- [ ] **Step 4: Compilar**

```bash
python -m platformio run
```

Esperado: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add lib/Book32_Web/WebMgr.cpp
git commit -m "feat(web): mDNS book32.local"
```

---

### Task 8: Verificação em hardware

**Files:** nenhum (verificação manual)

Nada nesta task se pode testar no host. Executar por ordem e registar o
resultado de cada passo.

- [ ] **Step 1: Flash**

```bash
python -m platformio run --target upload
python -m platformio run --target uploadfs
python -m platformio device monitor
```

Esperado no log: `Web Server Started` seguido de `mDNS: http://book32.local/`.

- [ ] **Step 2: Resolução de nome e página**

Abrir `http://book32.local/send` no telemóvel, na mesma WiFi.
Esperado: prompt de credenciais na primeira vez (utilizador
`BOOK32_AUTH_USER`, password derivada do MAC — a mesma da web UI), depois a
página com a drop-zone e a linha "Livre: X MB de Y MB".

- [ ] **Step 3: Envio múltiplo**

Enviar 3 EPUBs de uma vez.
Esperado: 3 ✓ em série (nunca dois em progresso ao mesmo tempo), e os 3
aparecem na biblioteca do device e em `http://book32.local/`.

- [ ] **Step 4: Tipo rejeitado**

Enviar um `.pdf`.
Esperado: ✗ "tipo de ficheiro nao suportado", resposta imediata, e o espaço
livre reportado não muda.

- [ ] **Step 5: Partição cheia**

Encher a partição de ebooks e tentar enviar mais um EPUB.
Esperado: ✗ "sem espaco na particao de ebooks" (HTTP 507) e espaço livre
inalterado.

- [ ] **Step 6: Ligação cortada**

Começar o envio de um EPUB grande e desligar o WiFi do telemóvel a meio.
Esperado: ✗ "device inacessivel..." com botão `repetir`. Reiniciar o device e
confirmar no log a linha `A remover upload incompleto: <nome>.epub.part`, e que
nenhum ficheiro truncado aparece na biblioteca.

- [ ] **Step 7: Não-regressão da web UI**

Abrir `http://book32.local/` e enviar um livro pelo formulário antigo.
Esperado: continua a funcionar (o `script.js` só olha para o status code, e o
200 mantém-se em caso de sucesso).

- [ ] **Step 8: Fechar**

Se algum passo falhar, parar e reportar antes de dar a funcionalidade como
concluída. Se todos passarem, não há nada a commitar nesta task — o código já
foi commitado nas tasks 1 a 7. Reportar o resultado dos 7 passos.
