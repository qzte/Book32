# Avaliação do repositório original rolohaun/Book32 — atualização (hardware Sticky)

Data: 2026-08-29
Estado: avaliação — sem alterações de código associadas

## Contexto

Seguimento de `docs/plans/2026-08-25-avaliacao-upstream-rolohaun.md`. Este
fork está agora em `v1.15.0` (era `v1.12.1` na avaliação anterior).
Comparei de novo com o `HEAD` atual de `rolohaun/Book32` (`8f21bd7`).

## O que mudou no original desde a última avaliação

Apenas dois commits novos desde `af480a4` ("Release Book32 1.2"), o ponto já
coberto em 25/08:

- `77e14f3` "Add Book32 Sticky hardware support" — **única novidade real**:
  suporte para um dispositivo físico diferente, o Seeed reTerminal E1002 /
  "Book32 Sticky" (ecrã tátil 3.97", touch GT911, armazenamento em MicroSD
  com fallback interno de 23 MB). Traz `StickyDisplay.cpp/h`,
  `StickyTouch.cpp/h`, `partitions_32MB_sticky.csv`, um novo ambiente
  PlatformIO (`seeed_reterminal_sticky`) e alterações a `InputMgr`,
  `DisplayMgr`, `BatteryMgr`, `Book32FS`, `Config.h`, `main.cpp`
  (941 inserções, 134 remoções no total).
- `8f21bd7` "Refresh versioned installer assets" — apenas republicação dos
  binários do instalador via browser para as versões existentes; sem
  funcionalidade nova.

Confirmado por inspeção direta que nada relacionado com "sticky",
"reterminal" ou "GT911" existe neste fork, e que `AppKlipper`/`AppTodo`
continuam ausentes, tal como já apontado em 25/08.

## Recomendação

O Book32 Sticky é um produto físico diferente do kit TRMNL usado por este
fork — pinout, ecrã tátil e cartão SD próprios. Portar às cegas o driver de
touch e o layout de partições sem hardware real para validar repete
exatamente o tipo de risco já documentado no "Incidente (2026-08-25):
instalador apagou um dispositivo real" na avaliação anterior.

**Decisão (2026-08-29)**: não implementar — não há hardware Sticky
disponível para testar. Fica registado aqui para retomar caso essa
disponibilidade mude no futuro; a implementação do original em `77e14f3`
serve de referência direta caso isso aconteça.

## Resumo

| # | Item | Novo desde 25/08? | Recomendação |
|---|---|---|---|
| 1 | Suporte de hardware Book32 Sticky (touch + MicroSD) | Sim | Não implementar — sem hardware para validar |
| 2 | Refresh de assets do instalador | Sim, mas sem funcionalidade nova | Nenhuma ação |
| 3 | Restantes itens da avaliação de 25/08 (Open Sans, AppKlipper, AppTodo, ícones) | Sem alteração desde então | Ver `2026-08-25-avaliacao-upstream-rolohaun.md` |
