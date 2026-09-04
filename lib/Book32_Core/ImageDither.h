#ifndef IMAGE_DITHER_H
#define IMAGE_DITHER_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#define BOOK32_IMAGE_DITHER_HAS_PSRAM 1
#endif

// Book32 — conversão de imagem (capas de EPUB) para o 1bpp do e-ink.
//
// Lógica pura, sem Arduino nem descodificadores: recebe luminância de 8 bits
// vinda de quem descodificou o ficheiro (JPEGDEC/PNGdec, ver
// lib/Apps/AppReader/CoverImage.cpp) e devolve o bitmap final. Fica aqui, em
// Book32_Core, para correr nos testes de host (tools/tests/test_image_dither.cpp)
// tal como o resto da lógica testável do projecto.
//
// A ordem das operações é o que decide a qualidade do resultado, e é o oposto
// da que a v1.19 usava:
//
//   1. redução por média de área (GrayBoxScaler) já em tons de cinzento;
//   2. esticar o contraste (autoLevelGray), porque um 1bpp não tem tons para
//      salvar uma capa lavada ou escura de mais;
//   3. só no fim, dithering Floyd-Steinberg no tamanho final.
//
// A v1.19 fazia o dithering primeiro (ONE_BIT_DITHERED da JPEGDEC, no tamanho
// descodificado) e só depois reduzia por vizinho mais próximo: reduzir uma
// imagem já reticulada é amostrar pontos soltos do retículo, o que destrói o
// padrão que dava a ilusão de tons e deixa a miniatura ruidosa/manchada. Fazer
// a média primeiro e reticular só no tamanho final é a ordem correcta.

namespace book32 {

struct FitRect {
    int x, y, w, h;
};

// Rectângulo de srcW x srcH ampliado/reduzido para caber inteiro dentro de
// boxW x boxH sem deformar (contain), centrado na caixa. Uma capa 2:3 numa
// caixa 3:4 fica mais estreita do que a caixa, com margem branca dos dois
// lados, em vez de esticada à largura toda como acontecia antes.
inline FitRect fitInsideBox(int srcW, int srcH, int boxW, int boxH) {
    FitRect r = {0, 0, 0, 0};
    if (srcW <= 0 || srcH <= 0 || boxW <= 0 || boxH <= 0) return r;

    // Comparar srcW/srcH com boxW/boxH sem vírgula flutuante nem divisões.
    if ((long)srcW * boxH >= (long)srcH * boxW) {
        r.w = boxW; // limitado pela largura
        r.h = (int)(((long)srcH * boxW + srcW / 2) / srcW);
    } else {
        r.h = boxH; // limitado pela altura
        r.w = (int)(((long)srcW * boxH + srcH / 2) / srcH);
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    if (r.w > boxW) r.w = boxW;
    if (r.h > boxH) r.h = boxH;
    r.x = (boxW - r.w) / 2;
    r.y = (boxH - r.h) / 2;
    return r;
}

// Redução por média de área ("box filter"), alimentada por pedaços de imagem
// em qualquer ordem — é o que os descodificadores dão: a JPEGDEC entrega
// blocos de MCU da esquerda para a direita em bandas, a PNGdec entrega uma
// linha de cada vez. Cada pixel de origem é somado à célula de destino que lhe
// corresponde e no fim faz-se a média; nenhuma linha ou coluna da origem é
// deitada fora, ao contrário da amostragem por vizinho mais próximo.
class GrayBoxScaler {
  public:
    GrayBoxScaler() : _sum(nullptr), _count(nullptr), _srcW(0), _srcH(0), _dstW(0), _dstH(0) {}
    ~GrayBoxScaler() {
        release();
    }
    GrayBoxScaler(const GrayBoxScaler&) = delete;
    GrayBoxScaler& operator=(const GrayBoxScaler&) = delete;

    // Devolve false (e deixa o objecto inutilizável) para dimensões inválidas
    // ou se a alocação falhar — um EPUB do utilizador não pode travar aqui.
    //
    // Os dois vectores são alocados à mão em vez de com std::vector porque
    // uma capa de ecrã inteiro (320x480 células) pede perto de 1 MB: no
    // dispositivo isso não cabe na RAM interna, e um std::vector que falha
    // aborta o programa (as excepções estão desligadas no Arduino-ESP32) em
    // vez de devolver false. Com ps_malloc a memória vem da PSRAM, que é onde
    // o resto da conversão já trabalha, e uma falha é só um livro sem capa.
    bool begin(int srcW, int srcH, int dstW, int dstH) {
        release();
        if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return false;
        size_t cells = (size_t)dstW * (size_t)dstH;
        if (cells > SIZE_MAX / sizeof(uint32_t)) return false;

        _sum = (uint32_t*)allocLarge(cells * sizeof(uint32_t));
        _count = (uint16_t*)allocLarge(cells * sizeof(uint16_t));
        if (!_sum || !_count) {
            release();
            return false;
        }
        memset(_sum, 0, cells * sizeof(uint32_t));
        memset(_count, 0, cells * sizeof(uint16_t));

        _srcW = srcW;
        _srcH = srcH;
        _dstW = dstW;
        _dstH = dstH;
        return true;
    }

    bool ready() const {
        return _dstW > 0;
    }
    int sourceWidth() const {
        return _srcW;
    }
    int sourceHeight() const {
        return _srcH;
    }

    // `luma` são `count` pixeis consecutivos da linha `sy` a partir da coluna
    // `sx`. Pixeis fora da imagem (bordas de MCU que a JPEGDEC entrega para
    // além da largura útil) são ignorados em silêncio.
    void addSpan(int sy, int sx, const uint8_t* luma, int count) {
        if (!ready() || !luma || count <= 0) return;
        if (sy < 0 || sy >= _srcH) return;
        int dy = (int)(((long)sy * _dstH) / _srcH);
        if (dy < 0) dy = 0;
        if (dy >= _dstH) dy = _dstH - 1;
        uint32_t* rowSum = &_sum[(size_t)dy * _dstW];
        uint16_t* rowCount = &_count[(size_t)dy * _dstW];

        for (int i = 0; i < count; i++) {
            int x = sx + i;
            if (x < 0) continue;
            if (x >= _srcW) break;
            int dx = (int)(((long)x * _dstW) / _srcW);
            if (dx < 0) dx = 0;
            if (dx >= _dstW) dx = _dstW - 1;
            // Saturar em vez de dar a volta: uma célula com mais de 65535
            // pixeis de origem não acontece com as capas reais (seria uma
            // origem 256x mais larga do que a miniatura), mas se acontecesse
            // um contador a dar a volta trocava a média por lixo.
            if (rowCount[dx] == UINT16_MAX) continue;
            rowSum[dx] += luma[i];
            rowCount[dx]++;
        }
    }

    void addPixel(int sx, int sy, uint8_t luma) {
        addSpan(sy, sx, &luma, 1);
    }

    // Escreve _dstW * _dstH bytes de cinzento em `outGray`. Devolve false se
    // nenhum pixel chegou a ser somado (descodificação abortada a meio, ou
    // imagem vazia) — nesse caso `outGray` fica por tocar e quem chama trata
    // a capa como falhada em vez de mostrar uma caixa cinzenta inventada.
    bool resolve(uint8_t* outGray) const {
        if (!ready() || !outGray) return false;

        size_t cells = (size_t)_dstW * (size_t)_dstH;
        bool any = false;
        for (size_t i = 0; i < cells; i++) {
            if (_count[i] > 0) {
                outGray[i] = (uint8_t)((_sum[i] + _count[i] / 2) / _count[i]);
                any = true;
            }
        }
        if (!any) return false;

        // Células sem amostras: só acontece quando a origem é mais pequena do
        // que o destino nalgum eixo (capa minúscula). Preenche-se a partir do
        // vizinho já resolvido mais próximo — primeiro na horizontal, depois
        // copiando linhas inteiras em falta — para não deixar buracos pretos.
        for (int y = 0; y < _dstH; y++) {
            const uint16_t* rowCount = &_count[(size_t)y * _dstW];
            uint8_t* rowOut = &outGray[(size_t)y * _dstW];
            int last = -1;
            for (int x = 0; x < _dstW; x++) {
                if (rowCount[x] > 0) {
                    if (last >= 0) {
                        for (int f = last + 1; f < x; f++) {
                            rowOut[f] = (f - last <= x - f) ? rowOut[last] : rowOut[x];
                        }
                    } else {
                        for (int f = 0; f < x; f++)
                            rowOut[f] = rowOut[x];
                    }
                    last = x;
                }
            }
            if (last < 0) continue; // linha toda vazia: tratada abaixo
            for (int f = last + 1; f < _dstW; f++)
                rowOut[f] = rowOut[last];
        }

        for (int y = 0; y < _dstH; y++) {
            if (rowHasSamples(y)) continue;
            int src = -1;
            for (int d = 1; d < _dstH; d++) {
                if (y - d >= 0 && rowHasSamples(y - d)) {
                    src = y - d;
                    break;
                }
                if (y + d < _dstH && rowHasSamples(y + d)) {
                    src = y + d;
                    break;
                }
            }
            if (src < 0) continue; // impossível: `any` garante uma linha com amostras
            memcpy(&outGray[(size_t)y * _dstW], &outGray[(size_t)src * _dstW], (size_t)_dstW);
        }
        return true;
    }

  private:
    static void* allocLarge(size_t bytes) {
#ifdef BOOK32_IMAGE_DITHER_HAS_PSRAM
        void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (p) return p;
#endif
        return malloc(bytes);
    }

    void release() {
        if (_sum) free(_sum);
        if (_count) free(_count);
        _sum = nullptr;
        _count = nullptr;
        _srcW = _srcH = _dstW = _dstH = 0;
    }

    bool rowHasSamples(int y) const {
        const uint16_t* rowCount = &_count[(size_t)y * _dstW];
        for (int x = 0; x < _dstW; x++) {
            if (rowCount[x] > 0) return true;
        }
        return false;
    }

    uint32_t* _sum;
    uint16_t* _count;
    int _srcW, _srcH, _dstW, _dstH;
};

// Estica o contraste para todo o intervalo 0..255, ignorando `lowPermille` /
// `highPermille` (por mil) dos pixeis mais escuros/claros para que um logótipo
// preto ou um brilho branco não decidam sozinhos a escala.
//
// Num ecrã de 1 bit isto não é cosmética: sem esticar, uma capa cujos tons
// vivam todos entre 90 e 160 sai quase toda do mesmo lado do limiar do
// dithering — ou seja, um bloco preto ou um bloco branco. `minSpan` evita o
// contrário: numa capa genuinamente lisa (fundo de uma cor só), esticar um
// intervalo de 3 níveis para 255 transformava ruído de compressão em manchas.
inline void autoLevelGray(uint8_t* gray, size_t n, int lowPermille, int highPermille, int minSpan) {
    if (!gray || n == 0) return;
    if (lowPermille < 0) lowPermille = 0;
    if (highPermille < 0) highPermille = 0;
    if (lowPermille + highPermille >= 1000) return;

    uint32_t hist[256];
    memset(hist, 0, sizeof(hist));
    for (size_t i = 0; i < n; i++)
        hist[gray[i]]++;

    size_t lowCut = ((size_t)lowPermille * n) / 1000;
    size_t highCut = ((size_t)highPermille * n) / 1000;

    int lo = 0, hi = 255;
    size_t acc = 0;
    for (int v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc > lowCut) {
            lo = v;
            break;
        }
    }
    acc = 0;
    for (int v = 255; v >= 0; v--) {
        acc += hist[v];
        if (acc > highCut) {
            hi = v;
            break;
        }
    }
    if (hi <= lo) return;
    if (minSpan > 0 && hi - lo < minSpan) return;

    uint8_t lut[256];
    int span = hi - lo;
    for (int v = 0; v < 256; v++) {
        int scaled = ((v - lo) * 255 + span / 2) / span;
        if (scaled < 0) scaled = 0;
        if (scaled > 255) scaled = 255;
        lut[v] = (uint8_t)scaled;
    }
    for (size_t i = 0; i < n; i++)
        gray[i] = lut[gray[i]];
}

// Floyd-Steinberg em varrimento serpentino (linhas ímpares da direita para a
// esquerda), do cinzento `w x h` para dentro de `outBits`, um bitmap 1bpp
// empacotado de `outW x outH` na convenção do Adafruit_GFX::drawBitmap():
// bit=1 pinta a preto, bit=0 deixa o fundo. Os bits fora da imagem não são
// tocados — quem chama limpa o buffer primeiro (o fundo da miniatura é branco).
//
// O serpentino é o que evita as "riscas" diagonais típicas do Floyd-Steinberg
// sempre varrido no mesmo sentido, que numa miniatura de 60 pixeis de largura
// se veriam bem.
inline void ditherToBitmap1bpp(const uint8_t* gray, int w, int h, uint8_t* outBits, int outW, int outH,
                               int dstX, int dstY) {
    if (!gray || !outBits || w <= 0 || h <= 0 || outW <= 0 || outH <= 0) return;

    const size_t outPitch = (size_t)((outW + 7) / 8);
    std::vector<int32_t> curr((size_t)w + 2, 0);
    std::vector<int32_t> next((size_t)w + 2, 0);
    if (curr.size() != (size_t)w + 2 || next.size() != (size_t)w + 2) return;

    for (int y = 0; y < h; y++) {
        const bool leftToRight = ((y & 1) == 0);
        for (int i = 0; i < w + 2; i++)
            next[i] = 0;

        for (int step = 0; step < w; step++) {
            int x = leftToRight ? step : (w - 1 - step);
            int wanted = (int)gray[(size_t)y * w + x] + (int)(curr[x + 1] / 16);
            if (wanted < 0) wanted = 0;
            if (wanted > 255) wanted = 255;
            int chosen = (wanted < 128) ? 0 : 255;
            int err = wanted - chosen;

            if (chosen == 0) { // preto: bit=1 no Adafruit_GFX
                int px = dstX + x;
                int py = dstY + y;
                if (px >= 0 && px < outW && py >= 0 && py < outH) {
                    outBits[(size_t)py * outPitch + (size_t)(px / 8)] |= (uint8_t)(0x80 >> (px % 8));
                }
            }

            // Pesos clássicos 7/5/3/1 sobre 16, espelhados no sentido inverso.
            // O erro fica guardado multiplicado por 16 (dividido só na leitura
            // acima) para manter tudo em inteiros sem perder resto a cada passo.
            const int dir = leftToRight ? 1 : -1;
            curr[x + 1 + dir] += err * 7;
            next[x + 1 - dir] += err * 3;
            next[x + 1] += err * 5;
            next[x + 1 + dir] += err * 1;
        }
        curr.swap(next);
    }
}

} // namespace book32

#endif
