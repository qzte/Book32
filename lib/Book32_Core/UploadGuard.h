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

// Folga fixa para metadados do LittleFS (entradas de diretório, blocos
// parcialmente usados). Um EPUB que caiba "à justa" ainda assim falharia a
// escrever sem esta margem. Além disto há uma componente que escala com o
// tamanho — ver checkUpload.
#ifndef BOOK32_UPLOAD_SLACK
#define BOOK32_UPLOAD_SLACK 8192
#endif

enum class UploadVerdict { Ok, BadExtension, UnsafeName, NoSpace };

// `filename` deve já ser um basename (sem componentes de diretório) — o
// handler extrai-o antes de chamar. `contentLength` é o tamanho do corpo
// multipart, sempre maior que o ficheiro, logo uma estimativa conservadora.
// `contentLength == 0` significa desconhecido: não impede a aceitação, mas a
// verificação de espaço continua a correr, logo uma partição com menos de
// BOOK32_UPLOAD_SLACK livres devolve NoSpace na mesma.
//
// O espaço exigido escala com o tamanho: o littlefs guarda os ponteiros da
// CTZ skip-list dentro dos blocos de dados, por isso um ficheiro ocupa cerca
// de size * 4096/4088 em blocos — ~18 KB de overhead num ficheiro de 9 MB,
// bem acima da folga fixa de 8 KB. Sem isto um upload passava aqui e falhava
// depois no write(), devolvendo 500 onde 507 é a resposta correta.
// (size/256 majora size/511 = size*4096/4088 - size; sem risco de overflow
// para tamanhos na ordem dos 10 MB com size_t de 32 bits.)
template <typename S>
UploadVerdict checkUpload(const S& filename, size_t contentLength, size_t freeBytes) {
    if (!hasExtensionCI(filename, ".epub") && !hasExtensionCI(filename, ".ttf")) {
        return UploadVerdict::BadExtension;
    }
    if (!isSafeBookName(filename)) {
        return UploadVerdict::UnsafeName;
    }
    if (freeBytes < contentLength + contentLength / 256 + BOOK32_UPLOAD_SLACK) {
        return UploadVerdict::NoSpace;
    }
    return UploadVerdict::Ok;
}
