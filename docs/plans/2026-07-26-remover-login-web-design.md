# Remover o pedido de login em todo o sistema (v1.9.0)

**Data:** 2026-07-26
**Versão:** 1.8.1 → 1.9.0 (minor: alteração de comportamento sem quebra de dados nem de formatos persistidos)

## Objetivo

Eliminar o HTTP Basic Auth introduzido na v1.5.0. Nenhum pedido ao servidor
web volta a responder `401` nem faz o browser abrir a caixa de credenciais.

## O que foi removido

| Camada | Alteração |
| --- | --- |
| `lib/Book32_Web/WebMgr.cpp` | Removido o helper `requireAuth()` e as 13 chamadas espalhadas pelos endpoints |
| `lib/Book32_Web/WebMgr.cpp` | Removidas as 2 guardas `request->authenticate(...)` dos *body handlers* de `/api/books/upload` e `/api/library/import` |
| `lib/Book32_Core/DeviceCred.h` | Removido o macro `BOOK32_AUTH_USER` (ficou sem utilizadores); modelo de ameaça reescrito |
| `data/script.js` | Removido o wrapper de `window.fetch` que forçava `credentials: 'same-origin'` |
| `data/send.html` | Removido `xhr.withCredentials` e o ramo de erro `401` ("credenciais recusadas") |
| `include/Config.h` | `SYSTEM_VERSION` → `1.9.0` |

Endpoints agora abertos (antes exigiam credenciais):
`POST /api/books/order`, `POST /api/books/upload`, `DELETE /api/books/delete`,
`POST /api/update/all`, `POST /api/settings/reader`, `POST /api/settings/display`,
`POST /api/settings/sleep`, `DELETE /api/reader/progress`,
`POST /api/library/import`, `GET /api/app/switch`, `GET /api/wifi/status`,
`GET /api/wifi/scan`, `POST /api/wifi/connect`.

## O que foi deliberadamente mantido

- **`WebMgr::devicePassword()` e `deriveDevicePassword()`** continuam a existir:
  são a *passphrase* WPA2 do SoftAP (`AppMainMenu::startHotspot()`) e o valor
  mostrado no rodapé do e-ink. `tools/tests/test_device_cred.cpp` continua
  válido e a passar.
- **O hotspot continua fechado (WPA2).** Passar a AP aberto é uma decisão
  separada; bastaria trocar `WiFi.softAP(AP_SSID, WebMgr::devicePassword())`
  por `WiFi.softAP(AP_SSID)` em `lib/Book32_Apps/AppMainMenu.cpp`.

## Consequências de segurança (registo honesto)

Qualquer cliente que alcance a porta 80 do dispositivo — na rede de casa ou
ligado ao hotspot — pode agora, sem qualquer credencial: apagar livros, apagar
todo o progresso de leitura, alterar as configurações, ler o SSID e o IP da
rede doméstica, listar as redes vizinhas, fazer o dispositivo juntar-se a outra
rede WiFi e disparar um OTA. Na rede de casa a única barreira passa a ser a
password do router.

Se um dia houver necessidade de reintroduzir o login, o comentário no topo de
`WebMgr.cpp` documenta a armadilha principal: os *body handlers* de upload
correm **antes** do *response handler*, por isso têm de validar por si próprios
— caso contrário os bytes chegam à flash antes de o `401` ser enviado.

## Validação

- Os 9 harnesses de host compilam e passam (`g++ -std=c++17 -I lib/Book32_Core`).
- `grep -rn "requireAuth\|BOOK32_AUTH_USER" lib src` → sem resultados.
- A verificar no dispositivo: `pio run --target clean`, `upload`, `uploadfs`;
  abrir `http://<IP>/` e `http://<IP>/send` e confirmar que não aparece caixa de
  credenciais em nenhuma ação (upload, apagar, guardar configurações, OTA).
