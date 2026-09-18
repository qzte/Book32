#pragma once
// Book32 — a única escrita de ficheiro JSON partilhada por todos os stores.
//
// Porquê: até aqui só o ProgressStore e o BookmarkStore gravavam de forma
// segura (ficheiro .tmp + rename). Os outros nove faziam
//
//     File f = FS.open(PATH, FILE_WRITE);   // trunca o ficheiro bom já aqui
//     if (!f) return false;
//     serializeJson(doc, f);                // valor de retorno ignorado
//     f.close();
//     return true;                          // "gravado" mesmo com o FS cheio
//
// FILE_WRITE trunca o ficheiro antes de escrever fosse o que fosse. Num
// aparelho a bateria — que faz esp_deep_sleep_start() directamente, ver
// BatteryMgr::enterIdleSleep — uma falha de energia a meio deixa JSON
// cortado, e JSON cortado não falha a meias: deserializeJson recusa o
// ficheiro inteiro e leva consigo as entradas de todos os outros livros. No
// books_meta.json isso é o mapa nome-truncado -> nome-original a
// desaparecer, ou seja, o progresso de leitura de toda a biblioteca a ficar
// órfão.
//
// A sequência abaixo é a do ProgressStore::save(), com uma verificação a
// mais que faltava lá também: comparar os bytes escritos com o tamanho do
// documento em vez de os comparar com zero. Com o filesystem cheio, o
// serializeJson escreve uma parte e devolve um número maior que zero.
//
// A decisão do que cada passo significa está em JsonStoreLogic.h, sem
// filesystem à mistura e com teste de host (tools/tests/test_json_store.cpp).

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include "JsonStoreLogic.h"

// Grava `doc` em `path` de forma atómica: escreve para um ficheiro temporário
// ao lado (o mesmo caminho com a extensão trocada por .tmp, a convenção que o
// ProgressStore já usava) e só depois o renomeia por cima do destino. O
// rename do littlefs substitui o destino atomicamente, por isso o ficheiro
// anterior mantém-se intacto até ao documento novo estar inteiro em disco.
//
// Devolve false sem tocar no ficheiro existente em qualquer recusa; `tag`
// só serve para a mensagem no monitor série.
bool writeJsonAtomic(fs::FS& fs, const char* path, const JsonDocument& doc, const char* tag);
