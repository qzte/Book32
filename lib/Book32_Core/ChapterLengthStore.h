#pragma once
// Book32 — cache local do comprimento de texto (em caracteres) de cada
// capítulo de cada livro, usado para resolver um "ir para %" (ver
// GoToPercentLogic.h / AppReader::updatePercentSeek) sem ter de reabrir e
// reanalisar o EPUB inteiro outra vez.
//
// Porquê um store à parte em vez de reaproveitar o PageCountStore: o
// comprimento de texto de um capítulo não depende do tamanho/família de
// letra (ao contrário da contagem de páginas), por isso não faz sentido
// ficar preso à mesma chave (fontSize, fontFamily) — um valor calculado uma
// vez serve qualquer tamanho de letra seguinte. Mesma forma do
// ChapterNarrativeStore/ChapterGuideTypeStore (map<nome, vector<>>, sem
// assinatura global), não do PageCountStore.
//
// Preenchido pelo mesmo varrimento único de capítulos que conta páginas e
// constrói o índice de títulos (ver AppReader::updateIndexing, D6 da
// avaliação de código do eReader): cada capítulo já é lido e analisado uma
// vez para essas duas coisas, por isso medir o comprimento aqui não custa
// outra passagem pelo ZIP. Um "ir para %" pedido depois de esse varrimento
// já ter terminado lê os valores directamente daqui em vez de voltar a
// analisar o livro inteiro (ver AppReader::startPercentSeek); um pedido
// antes disso continua a fazer o próprio varrimento, como sempre fez.
//
// Chaveado pelo nome original, cache local (não entra no export/import de
// estado entre dispositivos) — mesmas convenções do ChapterTocStore.

#include <Arduino.h>
#include <map>
#include <vector>
#include "Lock.h"

class ChapterLengthStore {
  public:
    static ChapterLengthStore& getInstance();

    // `out` fica com um comprimento por capítulo, na mesma ordem da spine.
    // Devolve false quando o livro ainda não tem comprimentos guardados —
    // o chamador faz o próprio varrimento nesse caso (comportamento de
    // sempre, antes desta funcionalidade existir).
    bool get(const String& originalName, std::vector<long>& out);

    // Grava os comprimentos completos do livro de uma só vez — construídos
    // durante o mesmo varrimento que conta páginas e títulos, por isso não
    // há aqui uma versão parcial a fundir. Um vector vazio é ignorado.
    void set(const String& originalName, const std::vector<long>& lengths);

    // Larga os comprimentos de livros cujo .epub já não existe, como o
    // ChapterTocStore faz no mesmo ponto do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames);

  private:
    ChapterLengthStore() {}

    Book32Mutex _mutex;
    void load();
    bool save();

    bool _loaded = false;
    std::map<String, std::vector<long>> _lengths;
};
