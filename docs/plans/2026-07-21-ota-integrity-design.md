# v1.6.0 — Verificação de integridade do OTA (SHA-256)

Data: 2026-07-21
Estado: implementado
Fecha: parte do C4. **Não fecha o C4 por completo** — ver "Limites" abaixo.

## Problema

`performFirmwareUpdate()` e `performFilesystemUpdate()` transmitiam bytes
diretamente para `Update.write()` sem qualquer verificação. Combinado com os
bugs de truncagem corrigidos na v1.4.1 — respostas *chunked* a reportarem
comprimento −1, e ligações que estagnavam a meio — um download parcial ou
corrompido podia ser escrito na partição de aplicação e deixar o dispositivo
inutilizável.

## Solução

O workflow de release publica no corpo da release uma linha por *asset*:

```
SHA256 (firmware.bin) = <64 hex>
SHA256 (littlefs.bin) = <64 hex>
```

É o formato de `shasum -a 256 --tag`, portanto continua legível para quem edite
as notas à mão. O parser está em `lib/Book32_Core/OtaDigest.h` e ignora a prosa
à volta, incluindo os *code fences* de markdown.

O dispositivo calcula o SHA-256 **incrementalmente**, à medida que escreve
(`mbedtls_sha256_update` a cada bloco), portanto a verificação não custa uma
segunda passagem pela flash nem um segundo download. A comparação acontece
**antes** de `Update.end()`: em caso de divergência chama-se `Update.abort()`,
e o firmware em execução fica intacto.

### Falha fechada

Por decisão explícita, o dispositivo **recusa** instalar um asset sem digest
válido. O guard corre antes de `http.begin()`, portanto nem chega a descarregar.
O ecrã mostra `Update Blocked / No checksum in release` ou
`Checksum mismatch`, para que o motivo não fique só no log série.

Consequência a ter presente: uma release publicada à mão, sem passar pelo
workflow, **não instala por OTA**. O caminho de recuperação é USB
(`pio run --target upload`).

## Limites — o que isto não resolve

Vale a pena ser direto, porque é fácil sobrestimar o que um checksum garante.

O digest vem da **mesma resposta da API do GitHub** que fornece o URL de
download. Quem consiga forjar essa resposta controla o binário *e* o hash
esperado, e a verificação passa. Isto não é, portanto, defesa contra um
adversário ativo (MITM).

Agrava-o uma fragilidade que permanece: `checkUpdate()` chama `http.begin(url)`
sem `setCACert()`. No ESP32 Arduino isso estabelece TLS **sem validar o
certificado** — a ligação é cifrada, mas o dispositivo aceita qualquer
certificado, que é precisamente o que um MITM apresenta.

O que esta versão fecha de facto:

- downloads truncados, corrompidos em trânsito, ou interrompidos;
- assets trocados por engano entre releases;
- ficheiros mal construídos no processo de build.

O que continua em aberto:

- substituição maliciosa do binário por um adversário na rede;
- ausência de autenticação da origem.

### Caminho para fechar o C4

Duas opções, por ordem de força:

1. **Assinatura Ed25519** com a chave pública embutida no firmware. O atacante
   passaria a precisar da chave privada; comprometer o GitHub não bastaria.
   Independente de TLS e de CAs a expirar. Exige gerir a chave privada num
   *secret* do repositório e um passo de assinatura no workflow.
2. **Pinning do CA da GitHub** via `setCACert()`. Mais simples, mas os
   certificados raiz rodam periodicamente: se o CA embutido caducar, o
   dispositivo deixa de conseguir verificar updates e só se recupera por USB.

A opção 1 é a recomendada para uma futura v1.7.0.

## Workflow de release

`.github/workflows/release.yml` dispara em tags `v[0-9]+.[0-9]+.[0-9]+` e:

1. corre todos os testes de host (`tools/tests/test_*.cpp`);
2. **valida que a tag coincide com `SYSTEM_VERSION`** em `include/Config.h` —
   um desencontro faria o dispositivo reoferecer o mesmo update indefinidamente;
3. compila firmware e imagem de filesystem;
4. calcula os SHA-256 e escreve-os no corpo da release;
5. publica os dois `.bin` como assets.

O passo (2) merece nota: é a proteção contra o modo de falha mais provável do
dia a dia, que é esquecer o *bump* da versão antes de criar a tag.

## Formato das tags

Semantic Versioning estrito com `v` inicial: `v1.6.0`. As tags antigas do
repositório (`v.1.4`, `v.1.3.1`) não são aceites pelo filtro do workflow nem
pelo comparador introduzido na v1.4.1 — o `v.` com ponto não faz *parse*, e o
comparador falha fechado, ou seja, não atualiza.

## Testes

`tools/tests/test_ota_digest.cpp` cobre: leitura por asset, rejeição de nome
parcial (`old_firmware.bin` não satisfaz `firmware.bin`), rejeição de digests
truncados e não-hexadecimais, insensibilidade a maiúsculas na etiqueta e no
hexadecimal, notas vazias, e comparação de digests.

A libertação do contexto mbedTLS foi auditada em todos os caminhos de saída
posteriores à inicialização, incluindo os de aborto por estagnação e por erro
de escrita.
