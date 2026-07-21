# v1.5.0 — Autenticação do dispositivo (SoftAP + HTTP Basic)

Data: 2026-07-21
Estado: implementado

## Problema

Até à v1.4.1, o Book32 expunha duas superfícies sem qualquer controlo de acesso:

1. **SoftAP aberto.** `AppMainMenu::startHotspot()` chamava `WiFi.softAP(AP_SSID)`
   sem passphrase. Qualquer pessoa ao alcance do rádio entrava na rede.
2. **API sem autenticação.** Nenhum endpoint validava credenciais. Combinado
   com (1), isto permitia a um estranho apagar livros, ler o SSID da rede
   doméstica e disparar uma atualização OTA.

A v1.4.1 fechou o path traversal em `/api/books/delete`, mas isso protege
contra escapar da raiz — não contra quem simplesmente não devia estar lá.

## Decisões

### Credencial derivada do MAC (não configurável)

A password é `book<XXYYZZ>`, os últimos três bytes do MAC WiFi em hexadecimal
maiúsculo (ex.: `book4F2A91`). Ver `lib/Book32_Core/DeviceCred.h`.

Alternativas consideradas:

- **Aleatória por sessão de AP.** Mais forte, mas obriga a reler o ecrã e
  reintroduzir a cada arranque do hotspot. Copiar uma string aleatória de um
  E-Ink para o telemóvel é suficientemente irritante para o utilizador acabar
  por desligar a proteção.
- **Configurável, com default derivado do MAC.** É o destino certo a prazo,
  mas exige persistência em `SettingsStore`, um endpoint novo e UI no
  `script.js` — superfície a mais para uma correção de segurança. Fica como
  candidato a 1.6.0.

Escolheu-se a derivação fixa porque é estável entre reboots (o valor impresso
no ecrã continua válido), não precisa de estado persistido nem de UI, e não
corre o risco de trancar o utilizador fora do próprio dispositivo.

### Âmbito da autenticação: por efeito, não por verbo

Protegidos: todos os POST/DELETE, mais três GETs que não são read-only na
prática —

- `/api/app/switch` está declarado `HTTP_GET` mas **muda o estado** do
  dispositivo;
- `/api/wifi/status` devolve o SSID da rede doméstica e o IP local;
- `/api/wifi/scan` devolve a lista de redes vizinhas.

Abertos, para que o UI arranque e renderize antes de pedir credenciais:
`/api/status`, `/api/books`, os GETs de `/api/settings/*` e o GET de
`/api/reader/progress`.

O handler de upload merece nota: em ESPAsyncWebServer o *body handler* corre
**antes** do handler de resposta, portanto tem de rejeitar ele próprio os
pedidos não autenticados. Sem isso, os dados seriam escritos para a flash antes
de o 401 chegar ao cliente.

### A password aparece no ecrã, mas só com o hotspot ativo

`getWifiFooterText()` mostra `Wi-Fi: Book32 / book4F2A91 -> 192.168.4.1`
apenas quando `_hotspotActive`. Numa ligação normal por estação o rodapé
mostra só o IP, como antes.

A razão: o menu principal está permanentemente visível. Se a credencial
estivesse sempre no ecrã, qualquer pessoa que pegasse no dispositivo obteria a
password que também dá acesso à API na rede doméstica. Limitando-a ao estado em
que é de facto necessária, reduz-se a exposição sem custo de usabilidade — quem
precisa da password é precisamente quem está a olhar para o ecrã à procura do
hotspot.

## Modelo de ameaça — o que isto não resolve

Vale a pena ser explícito, para que ninguém confie nisto mais do que deve:

- **O MAC é observável por rádio.** Quem conheça este esquema de derivação
  consegue calcular a passphrase. Isto protege contra o vizinho casual, não
  contra um atacante determinado.
- **Basic Auth é base64, não cifra**, e o servidor é HTTP simples. Um sniffer
  já dentro da LAN doméstica lê a credencial em claro. HTTPS no ESP32 com
  certificado auto-assinado traria avisos no browser e custo de RAM
  significativo — não se justificava nesta versão.
- **Não há rate limiting.** Um atacante na rede pode tentar credenciais à
  vontade. Com 24 bits de entropia derivada, força bruta é viável para quem
  se dê ao trabalho.

Em resumo: isto eleva a fasquia de "qualquer um" para "alguém a fazer esforço".
É a melhoria certa para o problema real (AP aberto num dispositivo doméstico),
não uma solução de segurança forte.

## Cliente

`data/script.js` passa a envolver `window.fetch` para acrescentar
`credentials: 'same-origin'` por omissão. O browser trata o desafio 401 com o
seu prompt nativo e reutiliza a credencial nos pedidos seguintes, portanto
nenhuma password fica guardada no script.

## Testes

`tools/tests/test_device_cred.cpp` (host, sem hardware) cobre a derivação:
vetor conhecido, determinismo entre chamadas, MACs distintos a produzirem
passwords distintas, zero-padding dos bytes baixos, e o mínimo de 8 caracteres
exigido por WPA2.

A cobertura de autenticação foi verificada por inspeção estática de todos os
`server->on(...)` e `AsyncCallbackJsonWebHandler` do `WebMgr.cpp`.

## Nota de compatibilidade

Esta versão **quebra o acesso existente**: quem já usasse o hotspot tem de
ligar-se de novo com passphrase, e o browser vai pedir credenciais na primeira
ação mutável. É uma alteração de comportamento visível, daí o incremento de
*minor* (1.4.1 → 1.5.0) e não de *patch*.
