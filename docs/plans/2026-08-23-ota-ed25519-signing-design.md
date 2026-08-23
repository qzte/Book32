# v1.11.0 — Assinatura Ed25519 do OTA

Data: 2026-08-23
Estado: implementado
Fecha: o C4 identificado em `docs/plans/2026-07-21-ota-integrity-design.md` — a
opção 1 recomendada nesse documento (assinatura, em vez de pinning de CA).

## Problema

O SHA-256 introduzido na v1.6.0 (`lib/Book32_Core/OtaDigest.h`) protege contra
corrupção, não contra adulteração: o digest esperado vem da mesma resposta da
API do GitHub que fornece o URL do binário. Um atacante capaz de forjar essa
resposta (MITM, dado que `checkUpdate()` não valida o certificado TLS)
controla o binário e o hash esperado ao mesmo tempo.

## Solução

Uma segunda linha, independente do digest, no corpo da release:

```
SHA256 (firmware.bin) = <64 hex>
ED25519 (firmware.bin) = <128 hex>
```

`ED25519 (...)` é uma assinatura Ed25519 sobre os **32 bytes crus** do digest
SHA-256 do asset (não sobre o texto hex, nem sobre o ficheiro inteiro), feita
por `.github/workflows/release.yml` com uma chave privada que só existe no
secret `OTA_ED25519_PRIVATE_KEY` do repositório. A chave pública correspondente
está embutida no firmware (`lib/Book32_Core/OtaEd25519PublicKey.h`) e nunca
muda por OTA.

Assinar o digest em vez do ficheiro completo é seguro porque o SHA-256 é
resistente a pré-imagem: reutilizar um par (digest, assinatura) assinado sob
um nome de asset diferente exigiria encontrar um ficheiro — malicioso ou não —
com esse mesmo digest, exactamente o que o SHA-256 torna inviável. Não é
preciso nenhuma separação de domínio adicional por causa disto.

### Porque é que isto fecha o C4

Um atacante que force a resposta da API do GitHub continua a controlar o
digest SHA-256 e o URL — mas não a chave privada. Sem ela não consegue
produzir uma linha `ED25519 (...)` que o dispositivo aceite, porque a
verificação usa a chave pública embutida no firmware a bordo, que a resposta
forjada não consegue alterar.

### Falha fechada

Tal como o SHA-256, uma assinatura em falta ou malformada recusa a instalação
**antes** do download começar
(`GitHubMgr::downloadAndFlash`). Um atacante que simplesmente omita a linha
`ED25519 (...)` da resposta forjada — para cair de volta no SHA-256 que
também controla — não ganha nada: a ausência da assinatura já é, por si só,
motivo de recusa.

## Implementação

- **Parsing**: `OtaDigest.h` generalizou `extractSha256()` num
  `extractHexField()` partilhado (mesma forma de linha
  `LABEL (asset) = hex`, comprimento hex parametrizável), com
  `extractEd25519Signature()` como segundo utilizador. Acrescentou também
  `hexDecode()`, para converter os 128 caracteres hex em 64 bytes crus antes
  de chamar `Ed25519::verify()`.
- **Verificação on-device**: `lib/Book32_Update/GitHubMgr.cpp` chama
  `Ed25519::verify(sigBytes, BOOK32_OTA_ED25519_PUBLIC_KEY, digest, 32)`
  logo a seguir à comparação SHA-256 (antes de `Update.end()`), usando o
  mesmo `digest[32]` já calculado incrementalmente durante o download — sem
  segunda leitura do ficheiro.
- **Biblioteca**: `rweather/Crypto` (`platformio.ini`), que expõe
  `Ed25519::verify()`/`sign()` para Arduino/ESP32. É uma implementação C++
  praticamente livre de dependências do Arduino (cai para acesso directo à
  memória fora de `ARDUINO`), o que permitiu validar o algoritmo em host
  antes de qualquer flash — ver "Validação" abaixo.
- **Workflow**: `release.yml` ganhou um passo "Sign checksums (Ed25519)" que
  lê o secret, assina o digest SHA-256 binário de cada asset com
  `openssl pkeyutl -sign -rawin`, e falha explicitamente
  (`::error::`) se o secret não estiver configurado — uma release sem ele
  seria publicada e o dispositivo recusá-la-ia silenciosamente mais tarde.

## Validação (sem hardware disponível nesta sessão)

Não houve acesso a um dispositivo real. A confiança nesta alteração assenta
em três verificações feitas em host, com a chave de produção real:

1. `tools/tests/test_ota_digest.cpp` (corre no CI, `-Wall -Werror`) cobre o
   parsing/`hexDecode()` — lógica pura, sem dependência de Arduino.
2. O próprio `Ed25519.cpp` da `rweather/Crypto` foi compilado e corrido em
   host (mesmo código-fonte que o dispositivo vai compilar, sem qualquer
   caminho específico de MCU para `verify()`) contra a chave de produção
   real: uma assinatura válida verifica, uma assinatura alterada, uma
   mensagem alterada e uma chave pública errada são todas rejeitadas.
3. O script de assinatura do `release.yml` foi corrido localmente
   byte-a-byte (chave descartável, nunca a de produção) e o resultado
   verificado pelo mesmo binário de teste do ponto 2 — o pipeline completo
   "workflow assina → dispositivo verifica" foi exercitado de ponta a ponta.

O que isto **não** substitui: compilar de facto para o `seeed_xiao_esp32s3`
via PlatformIO (fica a cargo do `pio run` do CI) e um OTA real num
dispositivo, incluindo o caso de recusa (assinatura errada/ausente deve
mostrar "Update Blocked" no ecrã e manter o firmware corrente intacto).
Recomendo os dois antes de promover isto a uma release marcada.

## Gestão da chave

A chave privada foi gerada com `openssl genpkey -algorithm ed25519` fora
desta sessão de agente (o classificador de modo automático bloqueou
deliberadamente a tentativa inicial de gerar e imprimir a chave num único
comando — decisão correcta) e vive apenas no secret `OTA_ED25519_PRIVATE_KEY`
do repositório. Não há cópia no código, no histórico do git, ou em qualquer
ficheiro deste repositório.

Rodar a chave no futuro exige reflash por USB de todos os dispositivos já no
terreno — a chave pública embutida no firmware é precisamente o que impede
uma troca silenciosa por OTA. Tratar como um evento raro e planeado, não como
manutenção de rotina.
