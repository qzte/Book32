# UI web responsiva no telemóvel

Data: 2026-07-25

## Problema

A interface servida pelo Book32 em `192.168.4.1` fica desformatada no telemóvel.
`data/style.css` não tem nenhuma `@media` query: o layout assume sempre duas
colunas, com uma sidebar de 250px ao lado do conteúdo, dentro de um `body` com
`height: 100vh; overflow: hidden`. Num ecrã de ~390px isso deixa a página sem
scroll e com o conteúdo cortado.

O `<meta viewport>` já está presente em ambas as páginas, portanto o problema é
exclusivamente de CSS.

## Objetivo

O site fica bem formatado e usável no telemóvel, com a mesma qualidade de
utilização que já tem no PC e no tablet.

Fora de âmbito: PWA. `192.168.4.1` é HTTP simples, logo não é um *secure
context*, e o browser recusa registar um Service Worker — sem o qual não há
instalação nem funcionamento offline. Foi discutido e descartado.

`data/send.html` já é responsiva (estilos próprios, `max-width: 34rem`, layout
fluido) e não é alterada.

## Estratégia

Mobile-first por adição: o CSS existente continua a ser o caminho do desktop, e
só se acrescentam regras. Um único breakpoint, `@media (max-width: 768px)`.
Acima de 768px nada muda.

O breakpoint existe por uma razão estrutural específica: a passagem de duas
colunas para uma coluna não é exprimível com unidades fluidas. Tudo o resto usa
CSS intrinsecamente fluido, no espírito do `send.html` — `flex-wrap`, `gap`,
`max-width`, `word-break` — em vez de mais media queries. Menos CSS a servir de
um ESP32 por Wi-Fi lento.

Sem bibliotecas, sem imagens, sem pedidos de rede novos.

## Navegação: menu hambúrguer

A sidebar mantém-se o mesmo elemento `<nav class="sidebar">`. Em ≤768px torna-se
um painel `position: fixed` fora do ecrã (`transform: translateX(-100%)`), que
desliza para dentro quando `<body>` ganha a classe `.nav-open`. Por cima do
conteúdo entra um overlay escuro semi-transparente.

Alterações:

- **`data/index.html`** — um `<button class="nav-toggle" aria-label="Menu"
  aria-expanded="false">☰</button>` como primeiro filho de `.top-bar`, mais um
  `<div class="nav-overlay">`. O botão está escondido em desktop.
- **`data/script.js`** — `toggleNav()` e `closeNav()`, que alternam
  `body.classList` e sincronizam `aria-expanded`. `showTab()` chama `closeNav()`,
  para o painel fechar ao escolher um separador. Fecha também ao tocar no
  overlay e com a tecla `Escape`.
- **`.top-bar`** — de `justify-content: flex-end` para `space-between`: ☰ à
  esquerda, bateria à direita.

## Layout

| Regra atual | Correção em ≤768px |
|---|---|
| `body { height: 100vh; overflow: hidden }` | `min-height: 100dvh; overflow: visible` (`dvh` resolve a barra de endereço do Safari) |
| `.container { display: flex }` | `display: block` |
| `.sidebar { width: 250px }` | painel fixo fora do ecrã (ver acima) |
| `.content { padding: 3rem }` | `padding: 1rem` |

`.stats-grid` e `.apps-grid` já são fluidas
(`repeat(auto-fit, minmax(200px, 1fr))`) e não são alteradas.

## Estilos inline

Nove elementos do `index.html` têm atributos `style` — `width: 60px`,
`flex: 1; margin-left: 10px`, `margin-left: 10px`, `width: 100%`. Estilos inline
vencem qualquer media query, por muito específica que seja.

São movidos para classes (`.input-narrow`, `.setting-row > select`, etc.). A
alternativa seria `!important` em cada regra correspondente, o que produz CSS
impossível de manter. Refactor contido e exigido pelo trabalho; não se estende a
mais nada.

## Conteúdo e controlos

- **`.setting-row`** — `flex-wrap: wrap` e `gap: 0.75rem`. Labels longos como
  "Full Refresh Interval (Pages)" deixam de esmagar o campo ao lado; passam para
  a linha de cima quando não cabem.
- **Zoom automático no iOS** — o Safari amplia a página ao tocar num campo com
  `font-size` abaixo de 16px. `input`, `select` e `textarea` passam a
  `font-size: 16px` no breakpoint móvel.
- **Alvos de toque** — `min-height: 44px` no móvel para `.nav-links li`, `.btn`,
  `.btn-delete` e `.btn-order`. Este último tem hoje `padding: .2rem .4rem`
  (cerca de 14px de altura) e, empilhado aos pares, é o pior caso atual.
- **`.book-item`** — `flex-wrap: wrap`, e `word-break: break-word` no
  `.book-title`, para nomes de EPUB compridos não empurrarem o botão apagar para
  fora do ecrã.
- **Tipografia móvel** — `h1` de `2rem` para `1.5rem`; `.stat p` de `1.8rem`
  para `1.5rem`.
- **`#update-buttons`** já tem `flex-wrap: wrap` e fica como está.

## Ficheiros

- `data/style.css` — media query, regras fluidas, classes que substituem os
  estilos inline.
- `data/index.html` — botão do menu, overlay, remoção dos atributos `style`.
- `data/script.js` — `toggleNav()`, `closeNav()`, chamada em `showTab()`.

Sem alterações em `src/`: o servidor limita-se a servir os ficheiros do LittleFS.

## Desvios ao "desktop intocado" (verificados na implementação)

A comparação pixel a pixel dos três separadores a 1280px deu Dashboard e
Ereader idênticos. Em Settings ficaram duas diferenças deliberadas:

1. A linha "Sleep Message" passou a `.setting-row.stacked` — o label fica acima
   do campo, em vez de partir em duas linhas com o campo espremido ao lado.
2. Deslocamentos verticais de até 3px em três linhas, cujas margens de topo
   ad-hoc (15px, 12px, 10px, todas inline) foram unificadas em 12px pela classe
   `.setting-row.spaced`.

## Verificação

Não há testes automatizados no repo e não se criam para CSS. A verificação é
visual.

Local: `python -m http.server` dentro de `data/`, testado a três larguras —
375px (iPhone SE, pior caso), 768px (fronteira do breakpoint) e desktop, este
último para confirmar que **nada mudou** acima do breakpoint. Em local os
pedidos a `/api/*` falham, pelo que os valores aparecem como `--` e a lista de
livros vazia; chega para validar o layout.

No hardware, após `pio run -t uploadfs`, num telemóvel real ligado a
`192.168.4.1`:

1. A página faz scroll até ao fim, nos três separadores.
2. O ☰ abre e fecha o painel; escolher um separador fecha-o.
3. Tocar num campo de texto não faz a página saltar (iOS).
4. Os botões de ordenar livros são premíveis à primeira.
5. Nenhum scroll horizontal em nenhum separador.
