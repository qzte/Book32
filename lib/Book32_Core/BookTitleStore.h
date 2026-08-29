#pragma once
// Book32 v1.17.0 — cache local do título a sério de cada livro.
//
// Porquê: o upload corta o nome do ficheiro aos 28 caracteres (o LittleFS tem
// de o aguentar, ver WebMgr) e a biblioteca derivava daí o título, o que dava
// "Tempestade de Ónix Reb" no ecrã. O título verdadeiro está no <dc:title> do
// OPF, dentro do EPUB — mas abrir o ZIP de cada livro a cada scanBooks() era
// exactamente o tipo de trabalho que o leitor evita fazer no caminho de abrir
// a biblioteca. Por isso lê-se uma vez por livro, fora do desenho, e guarda-se
// aqui.
//
// Chaveado pelo nome original (o mesmo que o ProgressStore usa), não pelo nome
// cortado em disco: assim sobrevive a um livro reenviado com um sufixo de
// desambiguação diferente, e o reconcile() abaixo limpa entradas de livros que
// já não estão no dispositivo.
//
// Cache local, como o PageCountStore e ao contrário do ProgressStore: não
// entra no export/import de estado entre dispositivos, porque qualquer
// dispositivo a reconstrói sozinho a partir dos próprios ficheiros.

#include <Arduino.h>
#include <map>
#include <vector>
#include "Lock.h"

class BookTitleStore {
  public:
    static BookTitleStore& getInstance();

    // Devolve false quando o livro ainda não foi lido. Uma entrada vazia nunca
    // é gravada (ver set), por isso `out` vem sempre com texto quando é true.
    bool get(const String& originalName, String& out);

    // Guarda o título (UTF-8, já limpo — ver sanitizeBookTitleT). Um título
    // vazio é ignorado: gravá-lo faria a biblioteca mostrar uma linha em
    // branco em vez do nome do ficheiro.
    void set(const String& originalName, const String& title);

    // Tudo de uma vez, para o scanBooks() não abrir o ficheiro por livro.
    void loadAll(std::map<String, String>& out);

    // Larga as entradas de livros que já não existem, como o ProgressStore e
    // o BookmarkStore fazem no mesmo ponto do scanBooks().
    void reconcile(const std::vector<String>& presentOriginalNames);

  private:
    BookTitleStore() {}

    // O leitor lê isto no loop principal e a web UI pode escrever a partir da
    // tarefa do servidor (apagar um livro passa pelo reconcile do scan
    // seguinte, mas o import de estado mexe nos mesmos ficheiros). Dois
    // acessos ao mesmo std::map de tarefas diferentes é corrupção de memória,
    // não só uma leitura desactualizada. Ver Lock.h.
    Book32Mutex _mutex;
    void load();
    bool save();

    bool _loaded = false;
    std::map<String, String> _titles;
};
