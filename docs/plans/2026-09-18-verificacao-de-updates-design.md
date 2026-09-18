# "Não consegui verificar" não é "estás actualizado" — design notes

Responde a D5 de
[2026-09-18-avaliacao-codigo-stores-web.md](2026-09-18-avaliacao-codigo-stores-web.md).
Saiu na v1.26.0 (PR #95).

Continua a linha de
[2026-07-21-ota-integrity-design.md](2026-07-21-ota-integrity-design.md) e
[2026-08-23-ota-ed25519-signing-design.md](2026-08-23-ota-ed25519-signing-design.md),
mas fecha uma parte do problema que nenhum dos dois cobria — e que, ao
contrário do que ambos discutem, não está na cadeia de confiança: está no
nosso código.

## O problema

`GitHubMgr::checkUpdate()` colapsava todos os resultados possíveis num único
`bool available`. Sem WiFi, ligação recusada, 403 do limite de pedidos, 404 sem
releases publicadas, resposta grande de mais para analisar — tudo voltava igual
a uma release que mesmo não é mais recente. E os três sítios que falam com o
utilizador diziam a mesma coisa:

- a UI web: *"Estás actualizado."*
- o menu do aparelho: *"Ja tem a versao mais recente."*
- o log série: `No update available`

Era a única resposta que o dispositivo não tinha base para dar. Não tinha
falado com o GitHub de todo.

### Porque é que isto importa mais aqui do que noutro sítio

O desenho do OTA aceita, deliberadamente, que a ligação TLS não valida
certificado: as assinaturas Ed25519 protegem o que é gravado, escolhidas em
vez de fixar um CA que podia caducar e deixar o aparelho sem saída.

O que as assinaturas não podem proteger é **uma resposta que nunca chega**.
Quem consiga interferir com a ligação mantém o aparelho numa versão antiga
simplesmente por partir a verificação — e não precisa de forjar nada. Dizer
"estás actualizado" nesse caso é exactamente o contrário do útil. Dizer "não
consegui verificar" é honesto, e é accionável.

Isto não substitui o *pinning* do CA nem o torna desnecessário — fecha a parte
do risco que se pode fechar sem tocar numa cadeia de confiança que não se pode
reparar por OTA.

## O desenho

`UpdateCheckLogic.h`, puro, sem Arduino, com teste de host — a convenção do
`SemVer.h`. Classifica uma tentativa em sete resultados, dos quais só dois
significam que se ouviu o GitHub:

| Estado | Significado |
| --- | --- |
| `update_available` | ouviu-se: há novidade |
| `up_to_date` | ouviu-se: não há |
| `offline` | sem rede, o pedido nunca foi feito |
| `no_release` | 404 |
| `rate_limited` | 403 |
| `http_error` | outro estado, ou falha de transporte (código ≤ 0) |
| `bad_response` | um 200 que não se leu, ou sem *tag* |

`updateCheckCompleted(status)` devolve true só para os dois primeiros. É a
pergunta que todos os chamadores passam a fazer antes de afirmar seja o que
for.

O `checkUpdate()` passa a recolher os factos — há rede? que código veio? o
corpo leu-se? tem *tag*? é mais recente? — e a deixar o veredito para a função
pura. É o que permite testar "um 200 com o corpo ilegível" sem rede nenhuma.

### Um caso que estava mesmo errado

Uma resposta 200 **sem `tag_name`** fazia o `semverIsNewer("")` devolver false,
e o aparelho reportava estar actualizado sem nunca ter tido uma versão para
comparar. Não era uma imprecisão de mensagem, era uma conclusão errada. Agora
é `bad_response`.

## A API

`GET /api/check_update` ganha três campos:

```json
{ "checked": false, "status": "rate_limited", "httpCode": 403 }
```

O `hasUpdate` **fica exactamente como estava**. Os campos são aditivos, para
não partir quem já lê a resposta.

As chaves de `status` são parte da interface: a UI web decide por elas.
Acrescentar um estado é seguro, mudar o nome de um não é — e o teste de host
verifica que as sete existem e não colidem, porque uma colisão juntaria dois
resultados numa só mensagem, em silêncio.

## As três camadas de apresentação

O texto fica em cada camada, não na lógica — a UI web é em português com
acentos, o menu do aparelho é sem acentos (limitação da fonte), o log é em
inglês.

- **UI web** — mensagem por motivo, e passa a distinguir uma falha
  *dispositivo → GitHub* de uma falha *browser → dispositivo*, que antes
  partilhavam o mesmo texto ("Erro ao procurar actualizações"). Um firmware
  anterior a este campo (`checked === undefined`) mantém o comportamento
  antigo, para o caso de alguém actualizar só a UI.
- **Menu do aparelho** (`AppSettings`) — o mesmo tratamento, texto curto.
- **`AppMainMenu`** — não precisou. Só age quando há novidade, portanto nunca
  afirma nada de falso.

## Dois arranjos adjacentes

Provocados por esta alteração, não recolhidos por iniciativa própria:

- O `UpdateInfo` era inicializado por agregado com uma lista posicional de
  onze valores. Os campos novos partiam-na — e um campo acrescentado no meio
  partia-a em silêncio, com sorte num erro de compilação. Passou a ter valores
  por omissão.
- O documento JSON da resposta era um `1024` fixo com o corpo inteiro da
  release lá dentro (que já traz as linhas de SHA256 e ED25519). Os três
  campos novos aproximavam-no do limite, e um documento que transborda sai
  como JSON cortado: o browser falha o `res.json()` e mostra um erro de ligação
  que não aconteceu. Passou a ser dimensionado pelo conteúdo.

## Fora de âmbito

- **Fixar o CA** — ver a secção "O que não se fez" da avaliação.
- **Repetir automaticamente** uma verificação falhada: sem valor claro, e um
  `rate_limited` a repetir-se sozinho piora o próprio problema.
- **Distinguir "não consegui ligar" de "o servidor respondeu 500"** na
  mensagem: ambos são `http_error`, com o `httpCode` disponível para quem
  quiser detalhe.

## Estado de verificação

A lógica tem teste de host; a integração não foi vista em hardware. O que
interessa confirmar são as falhas, não o caminho feliz: desligar o WiFi e
carregar em "Verificar", na UI web e no menu do aparelho, e ver que dizem que
não há rede em vez de "estás actualizado". Ver o `TODO.txt`, secção `v1.26.0`.
