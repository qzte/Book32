#pragma once
// Book32 — decisão pura de "vale a pena hifenizar aqui antes de mudar de
// linha", extraída de TextRenderer::renderRichPageDynamic() (M2 da avaliação
// 2026-09-02: essa função tinha ramos de hifenização a 6+ níveis de
// aninhamento dentro do desenho; a decisão em si não precisa do ecrã).
//
// Sem Arduino.h, sem Book32Display: só bytes e std::vector, por isso é
// testável em host (tools/tests/test_line_breaker.cpp), como WordFitLogic.h
// e HyphenationLogic.h.

#include <vector>
#include "WordFitLogic.h"
#include "HyphenationLogic.h"

// D4: antes de desistir de uma palavra que não cabe no resto da linha,
// tenta um corte silábico que caiba no espaço que sobra — o caso comum na
// prosa portuguesa corrida, ao contrário do corte por carácter desesperado
// de fitWordIntoLineHyphenated() (só atingido quando a palavra é mais larga
// que uma linha inteira). Exige >=3 letras de cada lado do corte (mais
// apertado que o >=2 que hyphenationPoints() já garante), para um corte
// aqui nunca ficar pior do que uma quebra normal, e é ignorado logo a
// seguir a uma linha que já acabou em hífen (`justHyphenated`) — dois finais
// de linha hifenizados seguidos lê-se pior do que um espaço em branco maior
// ocasional.
//
// Pura: não desenha nem mexe no buffer da linha do chamador. Devolve
// hyphen=false quando não há corte viável (largura da palavra, orçamento
// insuficiente, ou a regra >=3/>=3 chumba todos os pontos) — o chamador
// segue então para a quebra normal.
inline WordFit tryHyphenateAtWrap(const char* word, int wordLen, int wordWidth, int bufLeft, int pixelBudget,
                                  const unsigned char* widths, bool justHyphenated) {
    if (justHyphenated || bufLeft <= 0 || pixelBudget <= 0) return {0, 0, false};

    std::vector<int> hpoints = hyphenationPoints(word, wordLen);
    std::vector<int> hpoints3;
    for (int p : hpoints) {
        if (p >= 3 && (wordLen - p) >= 3) hpoints3.push_back(p);
    }
    if (hpoints3.empty()) return {0, 0, false};

    return fitWordIntoLineHyphenated(word, wordLen, wordWidth, bufLeft, pixelBudget, widths, hpoints3);
}
