#pragma once
// Book32 — quantos caracteres de uma palavra cabem na linha em construção.
//
// Rationale: o TextRenderer montava cada linha num `char lineBuf[256]` e
// acrescentava a palavra com strncat() sem verificar o espaço restante. A
// quebra de linha só dispara quando a linha já tem conteúdo, por isso a
// primeira palavra de uma linha era sempre copiada por inteiro: um token com
// mais de 255 caracteres (um URL, ou texto que o parser de HTML não conseguiu
// separar) escrevia para lá do fim do buffer, na pilha.
//
// A decisão é aritmética pura, por isso vive aqui, separada do desenho, e é
// testável sem hardware. Host-testable: tools/tests/test_word_fit.cpp.

#include <cstddef>
#include <vector>

struct WordFit {
    int take;            // caracteres a copiar (0 = não cabe nada, fechar a linha)
    int width;           // largura em pixels desses caracteres
    bool hyphen = false; // true: o chamador deve acrescentar um hífen a seguir a `take`
};

// `widths` é a tabela de larguras indexada por byte (0-255) da fonte activa.
// `bufLeft` é o espaço livre no buffer da linha, já descontado o terminador e
// um eventual separador. `pixelBudget` é o que resta da largura útil da linha
// e pode ser negativo.
//
// Garantias:
//   - take <= bufLeft e take <= wordLen (nunca transborda o buffer);
//   - take >= 1 sempre que bufLeft >= 1 e wordLen >= 1, mesmo com orçamento
//     negativo ou glifos de largura zero (caracteres fora do intervalo da
//     fonte), o que impede o ciclo de chamadas de ficar parado.
inline WordFit fitWordIntoLine(const char* word, int wordLen, int wordWidth,
                               int bufLeft, int pixelBudget,
                               const unsigned char* widths) {
    if (!word || wordLen <= 0 || bufLeft <= 0) return {0, 0};

    // Caso normal: a palavra cabe inteira, sem custo extra de medição.
    if (wordLen <= bufLeft && wordWidth <= pixelBudget) return {wordLen, wordWidth};

    // Palavra que não cabe numa linha só para ela: partir por caracteres.
    int take = 0;
    int fitted = 0;
    while (take < wordLen && take < bufLeft) {
        int charWidth = (int)widths[(unsigned char)word[take]];
        if (take > 0 && fitted + charWidth > pixelBudget) break;
        fitted += charWidth;
        take++;
    }

    if (take <= 0) {
        // Só acontece quando nem o primeiro caractere cabe no orçamento;
        // avançar um caractere garante progresso.
        take = 1;
        fitted = (int)widths[(unsigned char)word[0]];
    }
    return {take, fitted};
}

// Como fitWordIntoLine, mas primeiro tenta um corte numa sílaba válida
// (`points`, tipicamente vindo de hyphenationPoints() em HyphenationLogic.h),
// acrescentando um hífen visível em vez de cortar num carácter arbitrário.
// Cai para fitWordIntoLine sem alterações quando a palavra cabe inteira, ou
// quando nenhum ponto de `points` cabe no orçamento (palavra curta, sem
// vogais reconhecidas, ou `points` vazio).
//
// `bufLeft` já é o espaço livre no buffer da linha (ver fitWordIntoLine); o
// hífen ocupa mais um byte, por isso um corte silábico só é aceite quando o
// prefixo mais o hífen cabem os dois em bufLeft.
inline WordFit fitWordIntoLineHyphenated(const char* word, int wordLen, int wordWidth, int bufLeft,
                                         int pixelBudget, const unsigned char* widths,
                                         const std::vector<int>& points) {
    if (!word || wordLen <= 0 || bufLeft <= 0) return {0, 0, false};
    if (wordLen <= bufLeft && wordWidth <= pixelBudget) return {wordLen, wordWidth, false};

    int hyphenWidth = (int)widths[(unsigned char)'-'];
    int best = -1;
    int bestWidth = 0;
    for (int p : points) {
        if (p <= 0 || p >= wordLen) continue;
        if (p + 1 > bufLeft) continue; // +1: reserva o byte do hífen
        int w = 0;
        for (int k = 0; k < p; k++)
            w += (int)widths[(unsigned char)word[k]];
        if (w + hyphenWidth > pixelBudget) continue;
        if (p > best) {
            best = p;
            bestWidth = w + hyphenWidth;
        }
    }
    if (best > 0) return {best, bestWidth, true};

    return fitWordIntoLine(word, wordLen, wordWidth, bufLeft, pixelBudget, widths);
}
