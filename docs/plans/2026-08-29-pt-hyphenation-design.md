# Hifenização em português — design notes (v1.15.0)

Implementa a lacuna #3 de
[2026-08-29-avaliacao-crosspoint-reader.md](2026-08-29-avaliacao-crosspoint-reader.md):
o Book32 hoje corta uma palavra portuguesa demasiado larga para a sua própria
linha num ponto arbitrário (o que couber em pixels), sem qualquer hífen
visível — no ecrã estreito de 480px do TRMNL, com palavras compostas
portuguesas relativamente longas, isto acontece com alguma frequência,
sobretudo no tamanho de letra "Large".

## Âmbito: regras à mão, não um dicionário de padrões

O CrossPoint Reader (e a generalidade dos leitores maduros) usa hifenização
por padrões ao estilo Liang/TeX — um ficheiro de padrões que, para cobrir bem
uma língua, ronda dezenas de KB (o próprio roadmap do CrossPoint refere o
alemão como ~200KB só de padrões). O Book32 só lê uma língua, por isso optei
por um conjunto pequeno de regras escritas à mão — exactamente as regras de
"separação silábica" ensinadas no ensino básico em Portugal:

1. Uma consoante isolada entre duas vogais junta-se à vogal seguinte:
   "ca-sa".
2. "ch", "lh", "nh" (cada um um único som) e os grupos consonânticos
   consoante+líquida (bl, br, cl, cr, dr, fl, fr, gl, gr, pl, pr, tr, vr...)
   nunca se separam: "ma-lha", "li-vro".
3. Qualquer outra sequência de 2+ consoantes separa-se depois de todas menos
   a última: "car-ro", "com-pu-ta-dor".
4. Vogais adjacentes ficam na mesma sílaba (ditongo), excepto quando a
   segunda leva acento agudo em í/ú — a própria ortografia portuguesa já
   marca o hiato forçado assim: "sa-í-da", "sa-ú-de".
5. "u" é sempre classificado como vogal, nunca como consoante. É isto,
   sozinho, que mantém "qu"/"gu" ligados à vogal seguinte sem precisar de um
   caso especial: em "quan-do" e "lin-gua-gem" o "u" funde-se com o ditongo
   como qualquer outro.

Isto não pretende encontrar todas as sílabas que um linguista encontraria —
só o suficiente, correctamente, para dar ao quebra-linhas um ponto de corte
válido. Uma palavra com qualquer coisa que não seja letra pura (números, um
URL, um hífen já existente) fica de fora por completo em vez de arriscar um
corte errado — ver `lib/Book32_Core/HyphenationLogic.h` para a implementação
e o raciocínio linha a linha.

## Âmbito: só a palavra que não cabe na própria linha

O ciclo de quebra de linha em `TextRenderer::renderRichPageDynamic` tem dois
casos distintos quando uma palavra não cabe:

- **Já há conteúdo na linha, a palavra não cabe no resto dela**: o código
  existente muda logo para uma linha nova (branco à direita, sem cortar a
  palavra). Isto não foi tocado.
- **A linha está vazia (acabou de mudar) e mesmo assim a palavra não cabe
  nela inteira** — ou seja, a palavra é mais larga do que a coluna toda: é
  aqui que `fitWordIntoLine` já fazia corte por carácter, e é aqui que a
  hifenização entra (`fitWordIntoLineHyphenated`, em
  `lib/Book32_Core/WordFitLogic.h`).

Melhorar o primeiro caso também — hifenizar o fim de uma linha para reduzir o
espaço em branco à direita, o que teria mais impacto na justificação do texto
em geral — exigiria alterar o fluxo de controlo desse ciclo (que já tem
vários pontos de retorno antecipados para páginas cheias, cuidadosamente
afinados) para tratar uma palavra partida a meio como uma continuação sem
espaço na linha seguinte. Dado que este ciclo decide directamente o
`(nodeIndex, charOffset)` usado em todo o lado — progresso de leitura,
marcadores, contagem de páginas, "ir para %" — e esta sessão não tem
hardware físico para validar uma mudança de maior risco no fluxo de
paginação, fiquei pelo caso mais contido e mais seguro: só a palavra que não
cabe em linha nenhuma. Continua a ser exactamente o caso que motivou a
lacuna (palavras compostas longas cortadas de forma feia, sem hífen).

## Sem impacto no rastreio de posição

`fitWordIntoLineHyphenated` devolve quantos caracteres da palavra ORIGINAL
consumir (`take`); o hífen "-" é acrescentado só ao buffer da linha desenhada
(`lineBuf`), nunca ao texto de origem. `result.nextCharOffset` continua a
apontar exactamente para onde ficou a continuação da palavra no texto real —
o mesmo mecanismo que `fitWordIntoLine` já usava, só que agora `take` pode
coincidir com um ponto de sílaba válido em vez de "o máximo que cabe em
pixels".

## Custo em tempo de execução

`hyphenationPoints()` aloca um `std::vector<int>` e percorre a palavra —
nada de grátis. Por isso só é chamada quando a palavra *não* cabe inteira na
linha (o mesmo teste que `fitWordIntoLine` já fazia internamente): uma
palavra normal, que cabe, nunca a alcança.

## Verificação

Sem hardware físico nesta sessão. Validado com o teste de host puro
(`tools/tests/test_hyphenation.cpp`, casos manuscritos e verificados à mão
para "ca-sa", "car-ros", "ma-lhado", "li-vraria", "com-pu-ta-dor",
"quan-do", "lin-gua-gem", "sa-í-da"/"sa-ú-de", entre outros) e os novos casos
em `tools/tests/test_word_fit.cpp` para `fitWordIntoLineHyphenated`. `pio
run` não corre neste ambiente (ver
`docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md` para o porquê);
`TODO.txt` lista o que falta confirmar visualmente no dispositivo.
