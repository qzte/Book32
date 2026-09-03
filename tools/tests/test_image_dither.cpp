// Host test para a conversão de capas de EPUB para o 1bpp do e-ink.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_image_dither test_image_dither.cpp
//        && ./test_image_dither
#include "ImageDither.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace book32;

static bool bitSet(const std::vector<uint8_t>& bits, int w, int x, int y) {
    size_t pitch = (size_t)((w + 7) / 8);
    return (bits[(size_t)y * pitch + (size_t)(x / 8)] & (0x80 >> (x % 8))) != 0;
}

// Média de cinzento do bloco reticulado: com Floyd-Steinberg, a fracção de
// pixeis pretos numa área tem de acompanhar o tom original — é essa a
// propriedade que a v1.19 perdia ao reduzir depois de reticular.
static double blackRatio(const std::vector<uint8_t>& bits, int w, int h) {
    int black = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (bitSet(bits, w, x, y)) black++;
        }
    }
    return (double)black / (double)(w * h);
}

int main() {
    // 1. fitInsideBox: preserva a proporção e centra.
    {
        FitRect r = fitInsideBox(600, 900, 60, 80); // capa 2:3 numa caixa 3:4
        assert(r.h == 80);
        assert(r.w == 53); // 600*80/900 = 53.3 -> 53
        assert(r.x == 3 && r.y == 0);
        assert(r.w <= 60 && r.h <= 80);

        FitRect wide = fitInsideBox(1000, 500, 60, 80); // panorâmica: limita a largura
        assert(wide.w == 60 && wide.h == 30);
        assert(wide.x == 0 && wide.y == 25);

        FitRect square = fitInsideBox(400, 400, 80, 80);
        assert(square.w == 80 && square.h == 80 && square.x == 0 && square.y == 0);

        FitRect bad = fitInsideBox(0, 900, 60, 80); // dimensões inválidas: rectângulo vazio
        assert(bad.w == 0 && bad.h == 0);
    }

    // 2. GrayBoxScaler: a média de área não deita fora linhas nem colunas.
    //    Origem 4x4 com dois tons; reduzida a 2x2, cada célula é a média do
    //    seu quadrante — com vizinho mais próximo daria 0 ou 255, nunca 128.
    {
        GrayBoxScaler s;
        assert(s.begin(4, 4, 2, 2));
        for (int y = 0; y < 4; y++) {
            uint8_t row[4];
            for (int x = 0; x < 4; x++)
                row[x] = ((x + y) % 2 == 0) ? 0 : 255;
            s.addSpan(y, 0, row, 4);
        }
        uint8_t out[4];
        assert(s.resolve(out));
        for (int i = 0; i < 4; i++)
            assert(out[i] >= 127 && out[i] <= 128);
    }

    // 3. Blocos entregues fora de ordem e com pixeis para além da largura útil
    //    (é o que a JPEGDEC faz nas bordas): o resultado é o mesmo e nada
    //    escreve fora do destino.
    {
        GrayBoxScaler ordered, shuffled;
        assert(ordered.begin(8, 8, 4, 4));
        assert(shuffled.begin(8, 8, 4, 4));
        std::vector<uint8_t> img(64);
        for (int i = 0; i < 64; i++)
            img[i] = (uint8_t)((i * 7) % 256);
        for (int y = 0; y < 8; y++)
            ordered.addSpan(y, 0, &img[(size_t)y * 8], 8);
        for (int y = 7; y >= 0; y--) {
            uint8_t padded[12];
            for (int x = 0; x < 8; x++)
                padded[x] = img[(size_t)y * 8 + x];
            for (int x = 8; x < 12; x++)
                padded[x] = 0xAA;                  // borda de MCU, fora da imagem
            shuffled.addSpan(y, 4, &padded[4], 8); // metade direita + borda
            shuffled.addSpan(y, 0, &padded[0], 4); // metade esquerda, depois
        }
        uint8_t a[16], b[16];
        assert(ordered.resolve(a));
        assert(shuffled.resolve(b));
        for (int i = 0; i < 16; i++)
            assert(a[i] == b[i]);
    }

    // 4. Origem mais pequena do que o destino: as células sem amostras são
    //    preenchidas a partir do vizinho, nunca ficam a preto.
    {
        GrayBoxScaler s;
        assert(s.begin(2, 2, 6, 6));
        uint8_t row0[2] = {200, 200};
        uint8_t row1[2] = {200, 200};
        s.addSpan(0, 0, row0, 2);
        s.addSpan(1, 0, row1, 2);
        uint8_t out[36];
        assert(s.resolve(out));
        for (int i = 0; i < 36; i++)
            assert(out[i] == 200);
    }

    // 5. Sem amostras nenhumas (descodificação abortada logo no início):
    //    resolve() falha em vez de devolver uma imagem inventada.
    {
        GrayBoxScaler s;
        assert(s.begin(10, 10, 4, 4));
        uint8_t out[16];
        for (int i = 0; i < 16; i++)
            out[i] = 0x5A;
        assert(!s.resolve(out));
        for (int i = 0; i < 16; i++)
            assert(out[i] == 0x5A);    // buffer por tocar
        assert(!s.begin(0, 10, 4, 4)); // dimensões inválidas
    }

    // 6. autoLevelGray: uma capa "lavada" (tons todos entre 100 e 150) passa a
    //    usar a escala toda — sem isto sairia um bloco de uma cor só no e-ink.
    {
        std::vector<uint8_t> g(1000);
        for (size_t i = 0; i < g.size(); i++)
            g[i] = (uint8_t)(100 + (i % 51));
        autoLevelGray(g.data(), g.size(), 10, 10, 8);
        uint8_t lo = 255, hi = 0;
        for (uint8_t v : g) {
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        assert(lo == 0 && hi == 255);
    }

    // 7. autoLevelGray com uma imagem quase lisa: minSpan impede que ruído de
    //    compressão de 3 níveis seja amplificado até virar manchas.
    {
        std::vector<uint8_t> g(600);
        for (size_t i = 0; i < g.size(); i++)
            g[i] = (uint8_t)(180 + (i % 3));
        std::vector<uint8_t> before = g;
        autoLevelGray(g.data(), g.size(), 10, 10, 16);
        assert(g == before);
    }

    // 8. Dithering: a fracção de preto acompanha o tom de origem, e os bits
    //    fora do rectângulo pedido ficam por tocar.
    {
        const int W = 40, H = 40;
        size_t pitch = (size_t)((W + 7) / 8);
        for (int tone : {32, 96, 160, 224}) {
            std::vector<uint8_t> gray((size_t)W * H, (uint8_t)tone);
            std::vector<uint8_t> bits(pitch * H, 0);
            ditherToBitmap1bpp(gray.data(), W, H, bits.data(), W, H, 0, 0);
            double expected = 1.0 - (double)tone / 255.0;
            double got = blackRatio(bits, W, H);
            assert(got > expected - 0.12 && got < expected + 0.12);
        }

        // Preto e branco puros não devem ficar reticulados de todo.
        {
            std::vector<uint8_t> gray((size_t)W * H, 0);
            std::vector<uint8_t> bits(pitch * H, 0);
            ditherToBitmap1bpp(gray.data(), W, H, bits.data(), W, H, 0, 0);
            assert(blackRatio(bits, W, H) == 1.0);
        }
        {
            std::vector<uint8_t> gray((size_t)W * H, 255);
            std::vector<uint8_t> bits(pitch * H, 0);
            ditherToBitmap1bpp(gray.data(), W, H, bits.data(), W, H, 0, 0);
            assert(blackRatio(bits, W, H) == 0.0);
        }
    }

    // 9. Desenho deslocado dentro de uma caixa maior (capa 2:3 centrada numa
    //    caixa 3:4): nada é escrito fora do rectângulo de destino.
    {
        const int BOX_W = 60, BOX_H = 80, IMG_W = 20, IMG_H = 20;
        const int DX = 20, DY = 30;
        size_t pitch = (size_t)((BOX_W + 7) / 8);
        std::vector<uint8_t> gray((size_t)IMG_W * IMG_H, 0); // tudo preto: máximo de bits
        std::vector<uint8_t> bits(pitch * BOX_H, 0);
        ditherToBitmap1bpp(gray.data(), IMG_W, IMG_H, bits.data(), BOX_W, BOX_H, DX, DY);
        for (int y = 0; y < BOX_H; y++) {
            for (int x = 0; x < BOX_W; x++) {
                bool inside = (x >= DX && x < DX + IMG_W && y >= DY && y < DY + IMG_H);
                assert(bitSet(bits, BOX_W, x, y) == inside);
            }
        }
    }

    // 10. Recorte: uma imagem maior do que a caixa não escreve fora do buffer
    //     (protege contra um w/h que não bata certo com o cache da miniatura).
    {
        const int BOX_W = 16, BOX_H = 16;
        size_t pitch = (size_t)((BOX_W + 7) / 8);
        std::vector<uint8_t> gray(64 * 64, 0);
        std::vector<uint8_t> bits(pitch * BOX_H + 8, 0xEE); // sentinela no fim
        ditherToBitmap1bpp(gray.data(), 64, 64, bits.data(), BOX_W, BOX_H, -8, -8);
        for (size_t i = pitch * BOX_H; i < bits.size(); i++)
            assert(bits[i] == 0xEE);
    }

    printf("test_image_dither: OK\n");
    return 0;
}
