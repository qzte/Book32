// Book32 — host test for upload admission control.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_upload_guard.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "UploadGuard.h"

int main() {
    using std::string;
    const size_t BIG = 10u * 1024u * 1024u;

    // Extensões aceites, incluindo maiúsculas.
    assert(checkUpload(string("book.epub"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("Book.EPUB"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("font.TTF"), 1000, BIG) == UploadVerdict::Ok);

    // Extensões rejeitadas.
    assert(checkUpload(string("book.pdf"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string("book"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string(""), 1000, BIG) == UploadVerdict::BadExtension);

    // Nome com extensão válida mas inseguro.
    assert(checkUpload(string("../a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir/a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir\\a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);

    // A extensão é verificada antes do nome: um nome inseguro com extensão
    // inválida reporta BadExtension.
    assert(checkUpload(string("../a.pdf"), 1000, BIG) == UploadVerdict::BadExtension);

    // Espaço: cabe exatamente com a folga incluída.
    assert(checkUpload(string("a.epub"), 1000, 1000 + BOOK32_UPLOAD_SLACK) == UploadVerdict::Ok);
    // Falta 1 byte.
    assert(checkUpload(string("a.epub"), 1000, 1000 + BOOK32_UPLOAD_SLACK - 1) == UploadVerdict::NoSpace);
    // Partição cheia.
    assert(checkUpload(string("a.epub"), 1000, 0) == UploadVerdict::NoSpace);

    // contentLength desconhecido (0) não é motivo para rejeitar: o handler
    // valida depois, byte a byte, pelo retorno de write().
    assert(checkUpload(string("a.epub"), 0, BIG) == UploadVerdict::Ok);
    // ...mas com a partição cheia continua a ser NoSpace.
    assert(checkUpload(string("a.epub"), 0, 0) == UploadVerdict::NoSpace);

    printf("All 16 tests passed.\n");
    return 0;
}
