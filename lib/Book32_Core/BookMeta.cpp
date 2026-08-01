#include "BookMeta.h"
#include "Book32FS.h"
#include "Lock.h"
#include <ArduinoJson.h>

static const char* BOOKS_META_PATH = "/books_meta.json";

// O ficheiro é lido pelo leitor (loop principal) e escrito pelo handler de
// upload e pelo de apagar (tarefa do servidor web). Cada função abaixo é um
// ler-modificar-gravar completo, por isso o bloqueio cobre a função inteira e
// não apenas as operações de ficheiro. Recursivo porque
// findFilenameForOriginal() chama loadBookMetadata(). Ver Lock.h.
//
// Ordem de aquisição: quem já detém o ProgressStore pode tomar este (é o que
// acontece no import); o inverso nunca acontece.
static Book32Mutex g_metaMutex;

// Capacity derived from the file, not a fixed 4096: a large library used to
// overflow the fixed document, and a save on top of a truncated document threw
// away everyone else's entries.
static size_t metaCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 24576) cap = 24576;
    return cap;
}

static bool openMetaForRead(File& file) {
    if (SystemFS.exists(BOOKS_META_PATH)) {
        file = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    } else if (EbookFS.exists(BOOKS_META_PATH)) {
        // Older firmware wrote the metadata to the ebook partition.
        file = EbookFS.open(BOOKS_META_PATH, FILE_READ);
    }
    return (bool)file;
}

void loadBookMetadata(std::map<String, String>& metadata) {
    Book32Guard guard(g_metaMutex);
    metadata.clear();

    File file;
    if (!openMetaForRead(file)) return;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair pair : obj) {
        metadata[String(pair.key().c_str())] = pair.value().as<String>();
    }
}

String getOriginalFilename(const String& truncatedName) {
    Book32Guard guard(g_metaMutex);
    File file;
    if (!openMetaForRead(file)) return truncatedName;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return truncatedName;

    if (doc.containsKey(truncatedName)) {
        return doc[truncatedName].as<String>();
    }
    return truncatedName;
}

String findFilenameForOriginal(const String& originalName) {
    Book32Guard guard(g_metaMutex);
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    for (const auto& kv : metadata) {
        if (kv.second == originalName) {
            if (EbookFS.exists("/" + kv.first)) return kv.first;
        }
    }

    // No metadata entry: books that were never truncated are stored under the
    // original name itself.
    if (EbookFS.exists("/" + originalName)) return originalName;
    return "";
}

// --- Leitura/escrita do ficheiro -------------------------------------------
// A escrita é sempre em SystemFS, mas a leitura passa por openMetaForRead(),
// que também conhece a localização antiga (EbookFS). Sem isso, num aparelho
// migrado de firmware antigo o primeiro upload lia um SystemFS vazio e gravava
// um ficheiro com uma única entrada; como openMetaForRead() prefere SystemFS,
// esse ficheiro passava a ocultar o do EbookFS e todos os nomes originais
// anteriores desapareciam — títulos truncados na biblioteca e progresso de
// leitura sem correspondência com os ficheiros.

// Tamanho do ficheiro de metadados existente, seja qual for a partição onde
// está; 0 quando ainda não há nenhum. Serve para dimensionar o documento antes
// de o ler — o documento tem de ser criado com a capacidade final, porque a
// atribuição de um DynamicJsonDocument encolhe o pool para o que já está em
// uso e não deixaria espaço para a entrada nova.
static size_t existingMetaSize() {
    File metaFile;
    if (!openMetaForRead(metaFile)) return 0;
    size_t size = metaFile.size();
    metaFile.close();
    return size;
}

// `doc` tem de vir já dimensionado por quem chama.
static bool loadMetaDoc(DynamicJsonDocument& doc) {
    File metaFile;
    if (!openMetaForRead(metaFile)) return false;

    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    if (error) {
        Serial.println("Failed to parse metadata, creating new");
        doc.clear();
        return false;
    }
    return true;
}

static bool writeMetaDoc(const DynamicJsonDocument& doc) {
    // Mesma lição do ProgressStore: mais vale recusar a escrita do que
    // substituir um ficheiro bom por um truncado, que apagaria as entradas de
    // todos os outros livros.
    if (doc.overflowed()) {
        Serial.println("BookMeta: documento excedeu a capacidade — escrita recusada");
        return false;
    }

    File metaFile = SystemFS.open(BOOKS_META_PATH, FILE_WRITE);
    if (!metaFile) {
        Serial.println("Failed to save metadata");
        return false;
    }
    serializeJson(doc, metaFile);
    metaFile.close();
    return true;
}

void saveBookMetadata(const String& truncatedName, const String& originalName) {
    Book32Guard guard(g_metaMutex);
    size_t extra = truncatedName.length() + originalName.length() + 64;
    DynamicJsonDocument doc(metaCapacityFor(existingMetaSize()) + extra);
    loadMetaDoc(doc);  // ficheiro ausente ou ilegível: começa vazio

    doc[truncatedName] = originalName;

    if (writeMetaDoc(doc)) {
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    }
}

void removeBookMetadata(const String& truncatedName) {
    Book32Guard guard(g_metaMutex);
    size_t existingSize = existingMetaSize();
    if (existingSize == 0) return;  // nada gravado ainda

    DynamicJsonDocument doc(metaCapacityFor(existingSize));
    if (!loadMetaDoc(doc)) return;

    doc.remove(truncatedName);
    writeMetaDoc(doc);
}
