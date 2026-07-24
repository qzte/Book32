# Enviar Livros Diretamente Para o Book32 — Design

Data: 2026-07-24
Estado: aprovado, por implementar

## Problema

Hoje, para pôr um livro no device, é preciso abrir a web UI completa
(`http://<IP>/`), navegar até à secção de livros e usar o formulário de
upload. São vários passos, exige saber o IP, e a página carrega toda a UI de
gestão só para enviar um ficheiro. Falta um caminho curto, sobretudo a partir
do telemóvel.

## Solução

Uma página de envio dedicada e mínima, `GET /send`, servida pelo próprio
device, com drag-and-drop no PC e file picker no telemóvel, mais mDNS para
não depender do IP. Não substitui nada: `index.html` mantém-se inalterado.

## Restrições assumidas

Estas restrições foram validadas antes do desenho e delimitam o que é
possível:

- **Sem share sheet nativo.** O Web Share Target exige uma PWA com service
  worker, e service workers só correm em contexto seguro (HTTPS). O device
  serve `http://` na LAN, e uma PWA alojada em HTTPS não pode fazer POST para
  `http://` na LAN (mixed content bloqueado). TLS no ESP32-S3 custaria RAM e
  flash e obrigaria a aceitar um certificado self-signed. Fica de fora.
- **Só com o device acordado e WiFi ligado.** O WiFi é desligado dentro do
  leitor (`AppReader.cpp:185`) e no standby. Mantê-lo sempre ligado custaria
  bateria. A página trata a inacessibilidade como um erro claro com retry.
- **Nada acontece no e-ink.** O livro aparece na biblioteca na próxima vez que
  esta for aberta. Sem refreshes, sem interromper o ecrã atual.
- **Autenticação: HTTP Basic**, a mesma do resto da web UI. O browser memoriza
  as credenciais após a primeira utilização.

## Arquitetura

| Unidade | Responsabilidade | Alteração |
| --- | --- | --- |
| `data/send.html` | UI de envio: drop-zone, file picker, fila, progresso, erros | novo |
| `lib/Book32_Core/UploadGuard.h` | decisão pura de aceitar/rejeitar um upload | novo |
| `WebMgr::setupEndpoints` | rota `GET /send` → serve `send.html` | ~5 linhas |
| `/api/books/upload` | validação, verificação de espaço, erros reais, JSON | corrigido |
| `WebMgr::begin` | `MDNS.begin("book32")` + `addService("http","tcp",80)` | ~4 linhas |

`data/send.html` é single-file: HTML, CSS e JS inline, sem dependências
externas. Vive na partição `spiffs` (1 MB), servida pelo `serveStatic`
existente (`WebMgr.cpp:985`). Como o `serveStatic` só mapeia `/send.html`, a
rota `/send` é registada explicitamente.

## Fluxo

1. O utilizador abre `http://book32.local/send`, tipicamente por um atalho no
   ecrã principal do telemóvel.
2. Basic auth. O browser memoriza depois da primeira vez.
3. Escolhe ou arrasta 1..N ficheiros.
4. O JS envia-os **em série**, um `XMLHttpRequest` por ficheiro, com
   `upload.onprogress` a alimentar uma barra por item. Série e não paralelo
   porque o LittleFS é single-writer e o `AsyncWebServer` no ESP32-S3 não lida
   bem com uploads concorrentes.
5. Cada item termina em ✓ com o nome final devolvido pelo device (pode diferir
   do original por truncagem a 28 chars ou sufixo `_1`), ou ✗ com a razão e um
   botão de retry.
6. Nada acontece no e-ink.

## Correções ao endpoint de upload

O handler atual (`WebMgr.cpp:549`) responde `200 Upload Complete` mesmo quando
`EbookFS.open()` falha ou o disco enche, e faz sanitização de nome inline sem
usar os helpers já existentes. Passa a:

- rejeitar extensões que não sejam `.epub` ou `.ttf` com `415`, usando
  `hasExtensionCI` de `FileExt.h`;
- rejeitar nomes inseguros com `400`, usando `isSafeBookName` de `SafeName.h`
  (a truncagem a 28 chars e o sufixo anti-colisão mantêm-se);
- verificar `EbookFS.totalBytes() - usedBytes()` contra o `Content-Length` em
  `index == 0` e responder `507` antes de escrever um byte;
- verificar o retorno de `write()`; em erro, apagar o ficheiro parcial e
  responder `500`, sem deixar entrada órfã em `books_meta.json`;
- gravar para `<nome>.part` e renomear apenas no `final`, para que uma ligação
  cortada não deixe um ficheiro aparentemente válido. Ficheiros `.part` são
  ignorados por `/api/books` e apagados no arranque;
- responder JSON `{"ok":true,"name":"<nome final>"}` em vez de texto.

O `script.js` atual só olha para o status code, por isso mantém-se compatível.
Isto será confirmado na implementação.

## Casos-limite

| Situação | Comportamento |
| --- | --- |
| Extensão não suportada | `415`, ficheiro nunca aberto |
| Nome inseguro (`../`, separadores, controlo) | `400` |
| Sem espaço | `507` decidido antes de escrever |
| Escrita falha a meio | parcial apagado, `500` |
| Ligação cai a meio | `.part` órfão, limpo no arranque |
| Device a dormir ou WiFi off | XHR falha; "device inacessível", com retry |
| Colisão de nomes | sufixo `_1`..`_99`; a página mostra o nome final |
| Credenciais erradas | `401`; o browser volta a pedir |

## Unidade testável

`lib/Book32_Core/UploadGuard.h`, função pura sem dependências Arduino:

```cpp
enum class UploadVerdict { Ok, BadExtension, UnsafeName, NoSpace };
UploadVerdict checkUpload(const std::string& filename,
                          size_t contentLength, size_t freeBytes);
```

O handler fica uma casca fina à volta desta função.

`tools/tests/test_upload_guard.cpp` (host, sem hardware) cobre: extensões
válidas e inválidas em maiúsculas e minúsculas, nomes inseguros, ficheiro que
cabe exatamente, ficheiro a que falta 1 byte, e `contentLength` igual a 0.

Build, seguindo a convenção dos testes existentes:

```
g++ -std=c++17 -I lib/Book32_Core tools/tests/test_upload_guard.cpp
```

## Verificação manual

O que não é testável no host:

1. `pio run -t upload && pio run -t uploadfs`; confirmar que `book32.local`
   resolve.
2. Enviar 3 EPUBs de uma vez do telemóvel: 3 ✓, os três aparecem na
   biblioteca.
3. Enviar um `.pdf`: ✗ imediato, nada escrito na partição.
4. Encher a partição e enviar: `507`, espaço livre inalterado.
5. Desligar o WiFi a meio de um envio: erro na página, e nenhum `.part`
   residual depois de reiniciar.

## Fora de âmbito

Share sheet nativo e PWA, envio com o device a dormir, conversão de formatos,
envio por email ou a partir de cloud, e qualquer feedback no e-ink.
