#pragma once
// Book32 — exclusão mútua para o estado partilhado entre tarefas.
//
// Rationale: os singletons de estado (ProgressStore, SettingsStore, BatteryMgr,
// e as funções de BookMeta) são tocados por duas tarefas ao mesmo tempo — o
// loop principal e a tarefa do ESPAsyncWebServer, que corre os handlers HTTP
// no seu próprio contexto. Exemplos concretos que existiam:
//
//   * GET /api/status mede a bateria (liga o interruptor de medição, faz 30
//     leituras do ADC, desliga-o) enquanto o loop principal pode estar a fazer
//     exactamente o mesmo — uma das tarefas desliga o interruptor a meio da
//     leitura da outra.
//   * DELETE /api/reader/progress limpa o mapa em memória do ProgressStore
//     enquanto o leitor grava a posição da página. Dois escritores no mesmo
//     std::map é corrupção de memória, não apenas uma escrita perdida.
//   * POST /api/settings/reader faz ler-modificar-gravar sobre o mesmo
//     ficheiro que o menu de definições no dispositivo. O LittleFS serializa
//     cada operação de ficheiro, mas não a sequência inteira: a última gravação
//     apaga a alteração da outra.
//
// O mutex é recursivo de propósito: vários métodos públicos destas classes
// chamam-se entre si (ProgressStore::get() -> begin() -> load()), e um mutex
// simples entraria em deadlock ao ser tomado segunda vez pela mesma tarefa.
//
// Ordem de aquisição: ProgressStore -> BookMeta é o único encadeamento que
// existe (applyImportedJson lê os metadados). Nenhum caminho faz o inverso,
// por isso não há ciclo. Manter assim ao acrescentar bloqueios.

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Book32Mutex {
public:
    Book32Mutex() : _handle(xSemaphoreCreateRecursiveMutex()) {}

    // Um handle nulo (sem memória para o semáforo, no arranque) degrada para
    // "sem bloqueio" em vez de bloquear o sistema para sempre.
    void lock() {
        if (_handle) xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
    }
    void unlock() {
        if (_handle) xSemaphoreGiveRecursive(_handle);
    }

private:
    SemaphoreHandle_t _handle;
};

class Book32Guard {
public:
    explicit Book32Guard(Book32Mutex& mutex) : _mutex(mutex) { _mutex.lock(); }
    ~Book32Guard() { _mutex.unlock(); }

    Book32Guard(const Book32Guard&) = delete;
    Book32Guard& operator=(const Book32Guard&) = delete;

private:
    Book32Mutex& _mutex;
};
