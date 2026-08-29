#pragma once
// Book32 v1.17.0 — do dc:title do EPUB às linhas que a biblioteca desenha.
//
// Porquê: o handler de upload corta o nome do ficheiro aos 28 caracteres
// (WebMgr.cpp) porque o nome tem de caber no LittleFS, e a biblioteca derivava
// o título desse nome cortado. O resultado no e-ink era sempre uma linha só,
// truncada a meio da palavra e sem reticências: "Tempestade de Ónix Reb".
// O título a sério está dentro do EPUB, no <dc:title> do OPF; estas funções
// limpam-no e partem-no pelas linhas disponíveis na linha da lista.
//
// Puro, sem dependências do Arduino nem do GFX: a medição entra por callback,
// por isso a mesma função serve a fonte normal e a negrito do item
// seleccionado. Templates sobre S para servir Arduino String e std::string
// (mesma convenção do SafeName.h/BookOrderLogic.h).
// Host-testable: tools/tests/test_book_title.cpp.

#include <cstddef>
#include <vector>

// Tecto do título guardado em /book_titles.json. Um <dc:title> é texto de um
// ficheiro do utilizador: sem tecto, um EPUB com um "título" de milhares de
// caracteres inchava o JSON de metadados para nada — na lista nunca cabem
// mais do que duas linhas.
#ifndef BOOK32_MAX_TITLE_LEN
#define BOOK32_MAX_TITLE_LEN 96
#endif

namespace book32 {

template <typename S> inline S sliceT(const S& src, int from, int to) {
    S out;
    for (int i = from; i < to; i++)
        out += src[i];
    return out;
}

// Acrescenta `cp` a `out` em UTF-8. O ficheiro de títulos guarda UTF-8 (é o
// que a web UI lê); a conversão para Latin-1 acontece só à entrada do desenho,
// no AppReader, como já acontecia com os nomes de ficheiro.
template <typename S> inline void appendUtf8T(S& out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Compara src[from..from+len) com um literal ASCII, sem alocar.
template <typename S> inline bool matchesAtT(const S& src, int from, const char* lit, int len) {
    if (from + len > (int)src.length()) return false;
    for (int i = 0; i < len; i++) {
        if (src[from + i] != lit[i]) return false;
    }
    return true;
}

// Limpa o que vem do <dc:title>: um valor de OPF pode trazer marcação
// (<dc:title><span>...</span></dc:title>), entidades XML, e mudanças de linha
// da indentação do próprio ficheiro. Nada disto pode chegar ao ecrã.
//
// Devolve "" quando não sobra texto — o chamador decide o fallback (o nome do
// ficheiro), esta função não o inventa.
template <typename S> inline S sanitizeBookTitleT(const S& raw, int maxLen = BOOK32_MAX_TITLE_LEN) {
    S out;
    int n = (int)raw.length();
    bool inTag = false;
    bool pendingSpace = false;

    for (int i = 0; i < n; i++) {
        char c = raw[i];

        if (c == '<') {
            inTag = true;
            continue;
        }
        if (inTag) {
            if (c == '>') inTag = false;
            continue;
        }

        if (c == '&') {
            int semi = -1;
            for (int j = i + 1; j < n && j <= i + 10; j++) {
                if (raw[j] == ';') {
                    semi = j;
                    break;
                }
                if (raw[j] == ' ' || raw[j] == '&') break;
            }
            if (semi > i + 1) {
                int len = semi - i - 1;
                unsigned long cp = 0;
                bool decoded = false;
                if (raw[i + 1] == '#') {
                    bool hex = (len > 1 && (raw[i + 2] == 'x' || raw[i + 2] == 'X'));
                    int start = i + 2 + (hex ? 1 : 0);
                    if (start < semi) {
                        decoded = true;
                        for (int j = start; j < semi && decoded; j++) {
                            char d = raw[j];
                            int v;
                            if (d >= '0' && d <= '9')
                                v = d - '0';
                            else if (hex && d >= 'a' && d <= 'f')
                                v = d - 'a' + 10;
                            else if (hex && d >= 'A' && d <= 'F')
                                v = d - 'A' + 10;
                            else {
                                decoded = false;
                                break;
                            }
                            cp = cp * (hex ? 16 : 10) + (unsigned long)v;
                            if (cp > 0x10FFFF) decoded = false;
                        }
                    }
                } else if (matchesAtT(raw, i + 1, "amp", 3) && len == 3) {
                    cp = '&';
                    decoded = true;
                } else if (matchesAtT(raw, i + 1, "lt", 2) && len == 2) {
                    cp = '<';
                    decoded = true;
                } else if (matchesAtT(raw, i + 1, "gt", 2) && len == 2) {
                    cp = '>';
                    decoded = true;
                } else if (matchesAtT(raw, i + 1, "quot", 4) && len == 4) {
                    cp = '"';
                    decoded = true;
                } else if (matchesAtT(raw, i + 1, "apos", 4) && len == 4) {
                    cp = '\'';
                    decoded = true;
                } else if (matchesAtT(raw, i + 1, "nbsp", 4) && len == 4) {
                    cp = ' ';
                    decoded = true;
                }

                if (decoded) {
                    i = semi;
                    if (cp == ' ') {
                        if (out.length() > 0) pendingSpace = true;
                        continue;
                    }
                    if (pendingSpace) {
                        out += ' ';
                        pendingSpace = false;
                    }
                    appendUtf8T(out, cp);
                    continue;
                }
            }
            // Entidade que não se reconhece fica como o texto que é.
        }

        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (out.length() > 0) pendingSpace = true;
            continue;
        }

        if (pendingSpace) {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }

    if ((int)out.length() > maxLen) {
        int cut = maxLen;
        // Nunca cortar a meio de uma sequência UTF-8: o byte de continuação
        // sozinho vira lixo no ecrã e na web UI.
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80)
            cut--;
        out = sliceT(out, 0, cut);
    }
    return out;
}

// Parte `title` em no máximo `maxLines` linhas que caibam em `maxWidth`
// píxeis. `measure(S) -> int` devolve a largura do texto na fonte activa.
//
// Regras:
//   - quebra pelos espaços; uma palavra que sozinha não cabe é cortada por
//     caracteres (um título pode ter um "palavrão" que nenhuma linha aguenta);
//   - o que sobrar depois da última linha vira reticências no fim dela, com o
//     texto encolhido até as reticências caberem mesmo (o código antigo tirava
//     3 caracteres à sorte e voltava a transbordar);
//   - nunca devolve uma linha vazia, e progride sempre pelo menos um caractere
//     por linha, mesmo com maxWidth absurdamente pequeno.
template <typename S, typename Measure>
inline std::vector<S> wrapBookTitleT(const S& title, int maxLines, int maxWidth, Measure measure,
                                     const char* ellipsis = "...") {
    std::vector<S> lines;
    int n = (int)title.length();
    if (maxLines <= 0 || n <= 0) return lines;

    int pos = 0;
    while (pos < n && title[pos] == ' ')
        pos++;

    while (pos < n && (int)lines.size() < maxLines) {
        int lineStart = pos;
        int end = pos;  // fim (exclusivo) do que já foi aceite nesta linha
        int next = pos; // por onde continua a linha seguinte

        while (next < n) {
            int wordEnd = next;
            while (wordEnd < n && title[wordEnd] != ' ')
                wordEnd++;

            if (measure(sliceT(title, lineStart, wordEnd)) <= maxWidth) {
                end = wordEnd;
                next = wordEnd;
                while (next < n && title[next] == ' ')
                    next++;
                continue;
            }

            if (end == lineStart) {
                // Primeira palavra da linha e já não cabe: cortar por
                // caracteres, sempre pelo menos um para o ciclo progredir.
                int k = lineStart + 1;
                while (k < wordEnd && measure(sliceT(title, lineStart, k + 1)) <= maxWidth)
                    k++;
                end = k;
                next = end; // continua a meio da palavra, sem saltar espaços
            }
            break;
        }

        if (end == lineStart) end = lineStart + 1; // rede de segurança
        while (end > lineStart + 1 && title[end - 1] == ' ')
            end--;

        S line = sliceT(title, lineStart, end);

        bool isLastLine = ((int)lines.size() == maxLines - 1);
        if (isLastLine && next < n) {
            int cut = end;
            S candidate = line;
            candidate += ellipsis;
            while (cut > lineStart + 1 && measure(candidate) > maxWidth) {
                cut--;
                candidate = sliceT(title, lineStart, cut);
                candidate += ellipsis;
            }
            while (cut > lineStart + 1 && title[cut - 1] == ' ')
                cut--;
            line = sliceT(title, lineStart, cut);
            line += ellipsis;
        }

        lines.push_back(line);
        pos = next;
        while (pos < n && title[pos] == ' ')
            pos++;
    }

    return lines;
}

} // namespace book32
