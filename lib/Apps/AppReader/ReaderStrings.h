#pragma once

// Strings da UI do leitor (biblioteca + rodapé de leitura), reunidas aqui em
// vez de espalhadas por AppReader.cpp — o resto da UI (web, AppMainMenu,
// AppSettings) já está em português; isto fechava a última bolsa de inglês
// visível no dispositivo. PT por omissão (ver M4 na avaliação de código).
namespace ReaderStrings {
constexpr const char* LIBRARY_TITLE = "Biblioteca";
constexpr const char* BACK_TO_MENU = "<  Voltar ao Menu";
constexpr const char* NO_BOOKS_FOUND = "Nenhum livro encontrado.";
constexpr const char* UPLOAD_HINT = "Envie EPUBs pela web.";
constexpr const char* FOOTER_HINT = "Seguinte: Mover  |  Manter: Abrir";
constexpr const char* MENU_LABEL = "Menu";
constexpr const char* BOOKS_COUNT_FMT = "%d livros";
constexpr const char* PAGE_OF_FMT = "Pag. %d de %d";
constexpr const char* PAGE_FMT = "Pag. %d";
} // namespace ReaderStrings
