#include "JsonFileStore.h"

// "/chapter_toc.json" -> "/chapter_toc.tmp". Trocar a extensão em vez de
// acrescentar ".tmp" mantém exactamente os caminhos temporários que o
// ProgressStore e o BookmarkStore já usavam, para um aparelho actualizado não
// ficar com restos de um .tmp com outro nome.
static String tempPathFor(const char* path) {
    String tmp(path);
    int dot = tmp.lastIndexOf('.');
    int slash = tmp.lastIndexOf('/');
    if (dot > slash) tmp.remove(dot);
    tmp += ".tmp";
    return tmp;
}

bool writeJsonAtomic(fs::FS& fs, const char* path, const JsonDocument& doc, const char* tag) {
    JsonWriteAttempt attempt;
    attempt.overflowed = doc.overflowed();
    attempt.expected = measureJson(doc);

    String tmpPath;
    if (!attempt.overflowed) {
        tmpPath = tempPathFor(path);
        File out = fs.open(tmpPath.c_str(), FILE_WRITE);
        attempt.opened = (bool)out;
        if (attempt.opened) {
            attempt.written = serializeJson(doc, out);
            out.flush();
            out.close();
        }

        if (attempt.opened && attempt.written == attempt.expected) {
            // O rename do littlefs substitui o destino atomicamente; o
            // remove + segunda tentativa é só para portes que recusam um
            // destino existente.
            attempt.renamed = fs.rename(tmpPath.c_str(), path);
            if (!attempt.renamed) {
                fs.remove(path);
                attempt.renamed = fs.rename(tmpPath.c_str(), path);
            }
        }
    }

    JsonWriteOutcome outcome = jsonWriteOutcome(attempt);
    if (outcome == JsonWriteOutcome::Ok) return true;

    // Recusada: o ficheiro anterior fica como estava e o temporário não fica
    // para trás a ocupar espaço (nem a ser lido como se fosse bom).
    if (tmpPath.length() > 0) fs.remove(tmpPath.c_str());
    Serial.printf("%s: escrita de %s recusada — %s\n", tag, path, jsonWriteOutcomeMessage(outcome));
    return false;
}
