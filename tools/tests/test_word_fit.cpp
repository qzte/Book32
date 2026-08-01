// Host test for the line-buffer clamp used by the reader's word wrap.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_word_fit test_word_fit.cpp && ./test_word_fit
#include "WordFitLogic.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using std::string;

// Larguras de teste: 10 px por caractere imprimível, 0 para tudo o que esteja
// fora do intervalo (é o que o TextRenderer faz com glifos que a fonte não tem).
static unsigned char widths[256];

static int widthOf(const string& s) {
    int w = 0;
    for (unsigned char c : s) w += widths[c];
    return w;
}

int main() {
    memset(widths, 0, sizeof(widths));
    for (int c = 0x20; c < 0x100; c++) widths[c] = 10;
    widths[(unsigned char)'\x01'] = 0;  // glifo em falta

    // 1. Palavra que cabe: copiada inteira, largura preservada.
    {
        string w = "livro";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 100, 200, widths);
        assert(f.take == 5 && f.width == 50);
    }
    // 2. Palavra maior que o buffer: truncada no limite do buffer, nunca acima.
    //    (Este é o caso que corrompia a pilha: 300 caracteres num buffer de 256.)
    {
        string w(300, 'a');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 255, 100000, widths);
        assert(f.take == 255);
        assert(f.take <= 255);
    }
    // 3. Palavra mais larga que a linha: partida no limite de pixels.
    {
        string w = "abcdefghij";               // 100 px
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 100, 45, widths);
        assert(f.take == 4 && f.width == 40);  // o 5.º caractere passaria de 45
    }
    // 4. Buffer e pixels apertados ao mesmo tempo: vence o mais restritivo.
    {
        string w(50, 'a');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 3, 1000, widths);
        assert(f.take == 3);
    }
    // 5. Sem espaço no buffer: nada é copiado (o chamador fecha a linha).
    {
        string w = "abc";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 0, 1000, widths);
        assert(f.take == 0);
    }
    // 6. Orçamento negativo (a linha já passou da margem): avança na mesma um
    //    caractere, senão o ciclo de composição da linha nunca terminava.
    {
        string w = "abc";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 10, -5, widths);
        assert(f.take == 1 && f.width == 10);
    }
    // 7. Glifos de largura zero: também progridem, e cabem todos.
    {
        string w(20, '\x01');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), 0, 10, 0, widths);
        assert(f.take == 10 && f.width == 0);
    }
    // 8. Entradas degeneradas.
    {
        assert(fitWordIntoLine(nullptr, 5, 0, 10, 10, widths).take == 0);
        assert(fitWordIntoLine("abc", 0, 0, 10, 10, widths).take == 0);
    }
    // 9. Invariante geral: para qualquer combinação, take nunca excede o
    //    espaço do buffer nem o comprimento da palavra, e progride sempre que
    //    há espaço.
    {
        for (int len = 1; len <= 40; len++) {
            string w(len, 'a');
            for (int buf = 0; buf <= 40; buf += 7) {
                for (int budget = -20; budget <= 200; budget += 13) {
                    WordFit f = fitWordIntoLine(w.c_str(), len, widthOf(w), buf, budget, widths);
                    assert(f.take <= buf);
                    assert(f.take <= len);
                    assert(f.take >= 0);
                    if (buf >= 1) assert(f.take >= 1);
                }
            }
        }
    }
    printf("test_word_fit: all tests passed.\n");
    return 0;
}
