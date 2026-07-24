#pragma once
// Book32 — admission control for book/font uploads.
//
// Rationale: /api/books/upload respondia 200 mesmo quando o ficheiro não era
// aberto ou a partição estava cheia, por isso a UI não conseguia distinguir
// sucesso de falha. A decisão de aceitar um upload é lógica pura e vive aqui,
// separada do handler assíncrono, para ser testável sem hardware.
//
// Pure template — funciona com Arduino String e std::string.
// Host-testable: tools/tests/test_upload_guard.cpp.

#include <cstddef>
#include "FileExt.h"
#include "SafeName.h"

// Folga para metadados do LittleFS (blocos, entradas de diretório). Um EPUB
// que caiba "à justa" ainda assim falharia a escrever sem esta margem.
#ifndef BOOK32_UPLOAD_SLACK
#define BOOK32_UPLOAD_SLACK 8192
#endif

enum class UploadVerdict { Ok, BadExtension, UnsafeName, NoSpace };

// `filename` deve já ser um basename (sem componentes de diretório) — o
// handler extrai-o antes de chamar. `contentLength` é o tamanho do corpo
// multipart, sempre maior que o ficheiro, logo uma estimativa conservadora;
// 0 significa desconhecido e não bloqueia por si só.
template <typename S>
UploadVerdict checkUpload(const S& filename, size_t contentLength, size_t freeBytes) {
    if (!hasExtensionCI(filename, ".epub") && !hasExtensionCI(filename, ".ttf")) {
        return UploadVerdict::BadExtension;
    }
    if (!isSafeBookName(filename)) {
        return UploadVerdict::UnsafeName;
    }
    if (freeBytes < contentLength + BOOK32_UPLOAD_SLACK) {
        return UploadVerdict::NoSpace;
    }
    return UploadVerdict::Ok;
}
