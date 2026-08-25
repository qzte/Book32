# Avaliação do repositório original rolohaun/Book32 para possível adoção

Data: 2026-08-25
Estado: avaliação — sem alterações de código associadas

## Contexto

O `qzte/Book32` nasceu como fork de `rolohaun/Book32`. As duas árvores
partilham história até à tag `v1.1.3` (`86875b4`), commit que corresponde ao
mesmo hash nos dois repositórios. A partir daí divergiram por completo, sem
nenhum merge ou rebase entre elas desde então:

- **`qzte/Book32`** (este fork): 73 commits próprios desde `v1.1.3`, agora em
  `v1.12.1`. Desenvolvimento extenso e independente: OTA com assinatura
  Ed25519, `SettingsStore`/`ProgressStore` próprios, `StandbyGuard`,
  `UploadGuard`, página web `/send` com fila e progresso, mDNS, UI responsiva
  para telemóvel, testes de host em CI, `.clang-format`, etc.
- **`rolohaun/Book32`** (original): 59 commits desde `v1.1.3`. A meio do
  caminho o autor decidiu reiniciar a numeração de versões — o commit
  `37cd4a2 "Prepare Book32 1.0 release"` marca o fim de uma série `v1.3.x`
  (que incluía uma app de monitorização de impressoras 3D via Klipper) e o
  início de uma nova série `1.0`/`1.1`. O `HEAD` atual do original
  (`34a79cf`) corresponde à versão `1.1` desse novo esquema.

Não existe commit em comum recente nem antepassado próximo — `git merge-base`
entre os dois `main` falha porque as árvores não convergem depois de
`v1.1.3`. Um merge ou rebase direto não é viável nem desejável; a avaliação
abaixo é feature a feature.

## O que o fork já fez de forma independente (equivalente ou superior)

Verificado diretamente no código deste fork — nenhuma destas é uma lacuna:

- **Integridade do OTA**: o original **não tem nenhuma verificação de
  integridade** no canal OTA (sem digest, sem assinatura, sem
  `setCACert()`). Este fork já implementou SHA-256 incremental e assinatura
  Ed25519 (`lib/Book32_Core/OtaDigest.h`, `OtaEd25519PublicKey.h`) — o fork
  está à frente do original neste ponto crítico de segurança.
- **Barra de progresso OTA no ecrã** (`lib/Book32_Update/GitHubMgr.cpp:18`,
  `drawOTAProgress`) — equivalente à `v1.1.6`/`v1.1.8` do original.
- **Números de página globais e retoma de leitura** (`_globalPageNumber` em
  `AppReader.cpp`, `resumeOnBoot` em `ProgressStore.cpp`) — equivalente à
  série `v1.2.6`–`v1.2.8` e a "Add reader progress resume" do original.
- **Heurísticas de deteção de capítulo e limpeza de conteúdo EPUB**
  (deteção por classe CSS `chapter-title`, heurística de números de capítulo,
  filtro de `figure`/`svg`/`img`, filtro de texto "Unknown") em
  `EpubLoader.cpp` — equivalente à série `v1.1.7`–`v1.2.2` do original.
- **Gestão de bateria e idle sleep**: o fork tem trabalho próprio e recente
  (correção de overflow unsigned no idle sleep, desativação de longpress)
  que cobre o mesmo espaço da série `v1.3.15`–`v1.3.30` do original
  (deteção de carregamento, sleep configurável).

Ou seja: a maior parte do trabalho de engenharia feito no original depois da
divergência já foi reinventada — e em alguns pontos ultrapassada — de forma
independente neste fork.

## O que é genuinamente novo no original e vale a pena avaliar

### Alta prioridade — instalador via browser (`docs/index.html` + ESP Web Tools)

O original adicionou uma página estática (hospedada em GitHub Pages) que usa
[esp-web-tools](https://esphome.github.io/esp-web-tools/) para flashar o
Book32 diretamente do Chrome/Edge via Web Serial, sem precisar de
PlatformIO, CLI ou drivers. Tem dois modos — atualizar um dispositivo
existente ou preparar hardware novo de raiz — com manifests JSON simples:

```json
{
  "chipFamily": "ESP32-S3",
  "parts": [
    { "path": "firmware/firmware.bin", "offset": 65536 },
    { "path": "firmware/littlefs.bin", "offset": 5308416 }
  ]
}
```

Confirmei que `partitions_16MB.csv` é **byte-a-byte idêntico** entre os dois
repositórios — mesmo layout de flash, mesmos offsets. Isto significa que o
instalador é diretamente reutilizável sem qualquer adaptação de baixo nível;
só é preciso apontar os manifests para os binários que o `release.yml` deste
fork já produz. Dado que este fork já tem pipeline de release funcional, o
esforço de adoção é sobretudo copiar `docs/index.html`, `installer.css`,
`installer.js` e os manifests, e ligar isso ao workflow de release
existente (publicar `firmware.bin`/`littlefs.bin`/`book32-factory.bin` em
`docs/firmware/` ou apontar os manifests para os assets do release do
GitHub). Impacto alto para utilizadores não técnicos, risco baixo — é uma
página estática independente do firmware em si.

### Média prioridade — apps novas (domínio de funcionalidade, não correção)

- **AppKlipper**: monitorização de impressoras 3D via API Moonraker/Klipper
  (descoberta na rede, estado de impressão, temperaturas). É um domínio
  completamente novo, sem sobreposição com o leitor de EPUB. Só faz sentido
  adotar se o utilizador tiver uma impressora 3D com Klipper que queira
  monitorizar a partir do Book32 — não é uma "melhoria" no sentido estrito,
  é uma funcionalidade adicional opcional.
- **AppTodo**: lista de tarefas simples com interface web. Pequena, sem
  dependências problemáticas. Nice-to-have de baixo esforço se houver
  interesse, mas não resolve nenhuma lacuna atual do fork.

### Baixa prioridade

- **Fonte Open Sans para o leitor**: mais uma opção de fonte. O fork já tem
  cinco fontes próprias (`FreeSans`, `Gelasio`, `Literata`, `Merriweather`,
  `SourceSerif4`) com trabalho dedicado a suporte de acentuação
  portuguesa/Latin-1 (`docs/plans/2026-07-21-latin1-portuguese-fonts-design.md`).
  Antes de importar `OpenSansGFXFonts.h` seria preciso confirmar que a fonte
  gerada cobre os mesmos glifos acentuados — caso contrário introduz uma
  regressão de leitura em português. Valor marginal.
- **Ícones em alta resolução (160x160)**: puramente cosmético.
- **"Format blank ebook partition on first boot"**: verificar se o fork já
  trata este caso (partição de ebooks vazia num arranque novo) — parece
  sobreposto pelo trabalho de `PageCountStore`/`BookOrderLogic`, mas não
  confirmei linha a linha.

## O que não faz sentido adotar

- **Reinício da numeração de versões para 1.0/1.1**: o fork já está em
  `1.12.1` com o seu próprio histórico de tags; adotar o esquema de versões
  do original só criaria confusão.
- **Merge ou rebase direto das 59 commits**: dado que praticamente todo o
  conteúdo relevante (não-Klipper, não-instalador) já foi reimplementado de
  forma independente e por vezes divergente (estruturas de dados,
  convenções de nomes como `lib/Apps` vs `lib/Book32_Apps`), um merge
  traria uma quantidade grande de conflitos para recuperar funcionalidade
  que já existe. Cherry-pick seletivo das features genuinamente novas é a
  única abordagem que compensa o esforço.

## Resumo

| # | Item | Novo no original? | Impacto | Esforço | Recomendação |
|---|---|---|---|---|---|
| 1 | Instalador via browser (ESP Web Tools) | Sim | Alto (UX de instalação) | Baixo | Adotar |
| 2 | AppKlipper (monitor Klipper/Moonraker) | Sim | Médio (opcional) | Médio | Só se houver interesse em impressão 3D |
| 3 | AppTodo | Sim | Baixo | Baixo | Opcional |
| 4 | Fonte Open Sans | Sim | Baixo | Baixo | Só após validar glifos PT |
| 5 | Ícones 160x160 | Sim | Baixo (cosmético) | Baixo | Opcional |
| 6 | Progresso de leitura, números de página globais, heurísticas de capítulo, barra de progresso OTA | Já implementado no fork | — | — | Nenhuma ação |
| 7 | Integridade/assinatura do OTA | Fork já está à frente | — | — | Nenhuma ação |
| 8 | Merge/rebase direto do histórico | Não recomendado | — | Alto | Não fazer |

O item de maior valor por esforço é claramente o instalador via browser: é
uma página estática, sem impacto no firmware, compatível byte-a-byte com o
layout de flash já usado neste fork, e resolve um problema real (instalar o
Book32 exige hoje PlatformIO + VS Code). Os restantes itens são extensões de
funcionalidade opcionais, não correções de lacunas.
