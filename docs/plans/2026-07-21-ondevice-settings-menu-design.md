# Menu de configuração no dispositivo + standby manual (KEY2)

Data: 2026-07-21
Versão alvo: 1.4.0 (minor — duas funcionalidades novas, sem quebras)

## Motivação

Todas as definições do Book32 vivem hoje no Web UI. Isso obriga a ter um
telemóvel ou computador à mão e uma ligação Wi-Fi activa só para mudar o
tamanho da letra. Um e-reader deve ser configurável sem depender de outro
aparelho.

Ao mesmo tempo, o KEY2 do kit TRMNL estava por usar. Passa a ser o botão de
standby manual.

## Âmbito

Exposto no dispositivo: leitura (fonte, tamanho, frequência de refresh),
ecrã (orientação), suspensão (timeout), Wi-Fi (ligar/desligar + estado), e
sistema (versão, espaço, actualização OTA, reiniciar).

Fora de âmbito (fica exclusivo do Web UI): escrever passwords de Wi-Fi,
gestão de livros, ordenação da biblioteca, mensagem de suspensão. Escrever
texto livre com dois botões não vale o custo.

## Arquitectura

### SettingsStore (novo, Book32_Core)

O `WebMgr` lê e escreve os JSON de configuração directamente dentro dos
handlers HTTP, com os defaults e os clamps embutidos em cada lambda. Se o
menu no dispositivo fizesse o mesmo, existiriam duas cópias da mesma lógica
que divergiriam à primeira alteração.

O `SettingsStore` extrai essa lógica para um único sítio. Ambos os
consumidores — Web UI e menu no dispositivo — passam por ele.

| Ficheiro (EbookFS)     | Chaves                                          |
| ---------------------- | ----------------------------------------------- |
| `reader_config.json`   | `refreshFrequency`, `fontSize`, `fontFamily`     |
| `display_config.json`  | `rotation`                                       |
| `sleep_config.json`    | `sleepTimeout`, `sleepMessage`                   |

Regras preservadas do comportamento actual:

- `fontSize` limitado a 9, 12 ou 18 pt
- `fontFamily` limitado a 0–4
- `rotation` limitado a 1 ou 3 (só as duas orientações verticais)
- `saveReader()` faz merge com o ficheiro existente, para que gravar uma
  chave não apague as outras
- `reader_config.json` mantém a leitura de recurso em `SystemFS`
- JSON inválido ou ficheiro em falta cai nos defaults; nunca aborta

Alternativas descartadas: o menu escrever JSON à mão (duplica os clamps); o
menu falar com a API HTTP local (exigiria Wi-Fi ligado para mudar a letra).

### AppSettings (novo, Book32_Apps)

Uma `App` normal, registada no `main.cpp` a seguir ao `AppReader`. O
`AppMainMenu` desenha um ícone em grelha por cada app registada, portanto o
menu aparece no ecrã inicial sem código de navegação novo.

Edita um rascunho em RAM. Nada vai para o disco até `Guardar`.

## Navegação

Lista vertical, um item por linha, valor actual à direita. Refresh total do
e-ink a cada mudança — mais lento que o refresh parcial do `AppMainMenu`, mas
bastante mais simples, e um menu de definições não se percorre em contínuo.

Botões, coerentes com o resto do sistema:

- KEY3 curto → item seguinte (com wrap)
- KEY3 longo → seleccionar / ciclar valor
- KEY1 curto → item anterior
- KEY1 longo → sair do sub-ecrã, ou voltar ao menu principal
- KEY2 longo → standby (global)

Ciclagem no próprio item para escolhas curtas: tamanho da letra, orientação,
frequência de refresh, timeout de suspensão, Wi-Fi. Sub-ecrã para listas
longas: tipo de letra (5 famílias), rede (só leitura), sistema.

Itens finais: `Guardar` e `Descartar`. Os itens alterados ficam marcados com
asterisco. Sair com alterações pendentes abre confirmação — guardar,
descartar, cancelar.

### Excepção: Wi-Fi

O toggle de Wi-Fi age de imediato, não no `Guardar`. É um comando e não uma
preferência persistida, e é preciso vê-lo ligar para depois ler o IP.

## Aplicação ao guardar

O `AppSettings` corre no loop principal, e não numa task assíncrona, logo
pode aplicar directamente. Não precisa do mecanismo `_pendingRotation` /
`_pendingReaderFontSize` de que o `WebMgr` depende para sair do contexto do
async_tcp.

1. `SettingsStore::saveReader/saveDisplay/saveSleep()`
2. Se a rotação mudou: `DisplayMgr::setRotation()` e `forceRedraw()` em todas
   as apps
3. `applyFontSize()` / `applyFontFamily()` em todas as apps — os hooks já
   existem no `BaseApp` e só o leitor reage
4. `BatteryMgr::loadSleepSettings()`
5. Confirmação breve e regresso ao menu principal

Falha de escrita mostra erro e mantém o rascunho intacto — as alterações não
se perdem. Procurar actualização sem rede avisa em vez de bloquear.
Reiniciar pede confirmação.

## KEY2: standby manual

O KEY2 não existia no código: o `Config.h` definia apenas `PIN_BUTTON`
(GPIO5, KEY3) e `PIN_BUTTON_BACK` (GPIO2, KEY1). É um botão novo em GPIO3.

Accionado por pressão longa (400 ms), para evitar standby acidental.

Tratado no `InputMgr`, antes do despacho para a app activa. Assim funciona em
todo o lado — incluindo ecrãs modais como a confirmação de gravação — sem
replicar o mesmo `case` em cada app.

Ao premir: a app activa grava o estado (o leitor já persiste progresso; o
`AppSettings` grava o rascunho se estiver sujo), e depois
`BatteryMgr::enterIdleSleep()`, que já trata da mensagem no e-ink, do
`esp_sleep_enable_ext0_wakeup` e do deep sleep.

O KEY2 segue o padrão de *polling* manual do KEY1, e não os callbacks do
OneButton, para que os dois botões se comportem da mesma maneira.

### Armadilha: ext0 só aceita um pino

Acordar continua a ser pelo KEY3, como já era. Suportar também o KEY2 exigiria
trocar para `esp_sleep_enable_ext1_wakeup` com máscara de pinos. Não vale o
risco agora: o botão de acordar mantém-se o de sempre.

## Testes

A lógica do `SettingsStore` — defaults, clamps, merge, JSON corrompido — é
pura e vai num harness compilado no host, como já se fez para a codificação
Latin-1. A máquina de estados da navegação testa-se da mesma forma, sem
hardware.

## Armadilhas registadas

- **Não duplicar os clamps.** Se um valor novo for adicionado, muda-se no
  `SettingsStore` e ambos os consumidores acompanham.
- **Nunca desenhar a partir da task async.** O `WebMgr` adia via
  `_pending*` por esta razão. O `AppSettings` não tem o problema porque corre
  no loop, mas a distinção deve manter-se clara.
- **O rascunho perde-se no deep sleep** se não for gravado. Daí o passo de
  gravação antes do standby.
