// Host test for the EPUB title pipeline used by the e-ink library list.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_book_title test_book_title.cpp && ./test_book_title
#include "BookTitleLogic.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using book32::sanitizeBookTitleT;
using book32::wrapBookTitleT;
using std::string;

// Medição de teste: 10 px por caractere, como no test_word_fit.
static int measure10(const string& s) {
    return (int)s.size() * 10;
}

static string joined(const std::vector<string>& lines) {
    string out;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) out += "|";
        out += lines[i];
    }
    return out;
}

int main() {
    // --- sanitizeBookTitleT ---

    // 1. Título simples passa intacto.
    assert(sanitizeBookTitleT<string>("Tempestade de Ónix") == "Tempestade de Ónix");

    // 2. Indentação do OPF: mudanças de linha e espaços colapsam num só espaço.
    assert(sanitizeBookTitleT<string>("\n    The Butchers\n    Masquerade\n  ") == "The Butchers Masquerade");

    // 3. Marcação dentro do <dc:title> não chega ao ecrã.
    assert(sanitizeBookTitleT<string>("<span>The Gate</span> of the Feral Gods") ==
           "The Gate of the Feral Gods");

    // 4. Entidades XML nomeadas e numéricas (decimal e hexadecimal).
    assert(sanitizeBookTitleT<string>("Pai &amp; Filho") == "Pai & Filho");
    assert(sanitizeBookTitleT<string>("&#211;nix") == "Ónix"); // U+00D3 em UTF-8
    assert(sanitizeBookTitleT<string>("&#xD3;nix") == "Ónix");
    assert(sanitizeBookTitleT<string>("A&nbsp;B") == "A B");

    // 5. Entidade que não se reconhece fica como texto, não desaparece.
    assert(sanitizeBookTitleT<string>("Tom &foo; Jerry") == "Tom &foo; Jerry");

    // 6. Sem texto útil devolve "": o chamador é que decide o fallback.
    assert(sanitizeBookTitleT<string>("   \n  ").empty());
    assert(sanitizeBookTitleT<string>("<meta/>").empty());

    // 7. Tecto de comprimento sem cortar a meio de uma sequência UTF-8.
    {
        string longTitle(200, 'a');
        assert((int)sanitizeBookTitleT<string>(longTitle).size() == BOOK32_MAX_TITLE_LEN);

        // "Ó" (2 bytes) mesmo em cima do corte: o byte de continuação não pode
        // ficar sozinho no fim.
        string edge(BOOK32_MAX_TITLE_LEN - 1, 'a');
        edge += "Ó";
        string cut = sanitizeBookTitleT<string>(edge);
        assert((int)cut.size() == BOOK32_MAX_TITLE_LEN - 1);
        assert(cut.back() == 'a');
    }

    // --- wrapBookTitleT ---

    // 8. O caso do dispositivo: o título completo passa a ocupar duas linhas
    //    em vez de ser cortado a meio da palavra.
    {
        auto lines = wrapBookTitleT<string>("Tempestade de Onix Rebelde", 2, 200, measure10);
        assert(joined(lines) == "Tempestade de Onix|Rebelde");
    }

    // 9. Título que cabe todo: uma linha só, sem reticências.
    {
        auto lines = wrapBookTitleT<string>("Duna", 2, 200, measure10);
        assert(lines.size() == 1 && lines[0] == "Duna");
    }

    // 10. O que não cabe nas linhas disponíveis leva reticências, e a linha
    //     com as reticências continua dentro do limite.
    {
        auto lines = wrapBookTitleT<string>("um dois tres quatro cinco seis sete oito", 2, 100, measure10);
        assert(lines.size() == 2);
        assert(measure10(lines[0]) <= 100);
        assert(measure10(lines[1]) <= 100);
        assert(lines[1].size() >= 3 && lines[1].compare(lines[1].size() - 3, 3, "...") == 0);
    }

    // 11. Palavra sozinha maior que a linha: corte por caracteres, sem ciclo
    //     infinito e sem transbordar.
    {
        auto lines = wrapBookTitleT<string>("Donaudampfschifffahrtsgesellschaft", 2, 100, measure10);
        assert(lines.size() == 2);
        assert(measure10(lines[0]) <= 100);
        assert(measure10(lines[1]) <= 100);
        assert(lines[0] == "Donaudampf");
    }

    // 12. Uma linha só (é o que a lista usa quando não há altura para duas).
    {
        auto lines = wrapBookTitleT<string>("um dois tres quatro", 1, 100, measure10);
        assert(lines.size() == 1);
        assert(measure10(lines[0]) <= 100);
    }

    // 13. Casos degenerados: nada de linhas vazias, nada de ciclos infinitos.
    {
        assert(wrapBookTitleT<string>("", 2, 100, measure10).empty());
        assert(wrapBookTitleT<string>("abc", 0, 100, measure10).empty());
        auto tight = wrapBookTitleT<string>("abcdefgh", 3, 1, measure10);
        assert(tight.size() == 3);
        for (const auto& l : tight)
            assert(!l.empty());
    }

    // 14. Espaços à volta do título não viram linhas vazias nem entram na
    //     medição (o texto chega aqui já normalizado pelo sanitize acima).
    {
        auto lines = wrapBookTitleT<string>("  um dois  ", 2, 200, measure10);
        assert(joined(lines) == "um dois");
    }

    printf("test_book_title: OK\n");
    return 0;
}
