#include "CoverImage.h"
#include "ImageDither.h"
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <cstring>
#include <new>

using book32::FitRect;
using book32::GrayBoxScaler;

namespace {

// Contraste esticado ignorando 1% dos pixeis mais escuros e 1% dos mais claros
// (um logótipo preto ou um brilho branco não decidem sozinhos a escala), e só
// quando o intervalo original tem pelo menos 24 níveis — abaixo disso a capa é
// mesmo lisa e esticar transformaria ruído de compressão em manchas.
const int kAutoLevelLowPermille = 10;
const int kAutoLevelHighPermille = 10;
const int kAutoLevelMinSpan = 24;

// Tecto de pixeis para uma capa PNG (~2000x3000). Ao contrário do JPEG, que a
// JPEGDEC sabe descodificar já reduzido a 1/2, 1/4 ou 1/8, um PNG tem de ser
// inflado por inteiro antes de se poder reduzir seja o que for: uma capa
// gigante prendia o loop() do leitor durante segundos. Acima deste tecto o
// livro fica com o desenho genérico, que é melhor do que um bloqueio longo por
// uma miniatura de 60x80. A conversão de uma capa só acontece uma vez por
// livro (fica em cache), por isso o custo abaixo do tecto é pago uma vez.
const long kMaxPngPixels = 6000000;

// Contexto passado aos callbacks dos descodificadores pelo ponteiro de
// utilizador que ambas as bibliotecas oferecem (JPEGDEC::setUserPointer,
// PNG::decode(pUser, ...)). A v1.19 usava estáticos de ficheiro para o mesmo
// efeito; com dois descodificadores e um scaler com estado, um contexto
// explícito é mais seguro e não depende de nunca haver duas descodificações ao
// mesmo tempo.
struct DecodeContext {
    GrayBoxScaler* scaler;
    uint8_t* lineBuf;  // só para PNG: uma linha convertida para luminância
    int lineBufPixels; // capacidade de lineBuf, em pixeis
};

// --- JPEG ------------------------------------------------------------------

// Blocos de MCU já em luminância de 8 bits (setPixelType(EIGHT_BIT_GRAYSCALE)).
// iWidth é o passo da linha dentro do bloco e iWidthUsed a parte que ainda cai
// dentro da imagem: nas bordas direita/inferior a JPEGDEC entrega pixeis de
// enchimento para além da imagem, que não podem entrar na média.
int jpegDrawCallback(JPEGDRAW* pDraw) {
    if (!pDraw || !pDraw->pPixels) return 0; // aborta a descodificação
    DecodeContext* ctx = (DecodeContext*)pDraw->pUser;
    if (!ctx || !ctx->scaler) return 0;

    const uint8_t* src = (const uint8_t*)pDraw->pPixels;
    int stride = pDraw->iWidth;
    int usable = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
    if (usable > stride) usable = stride;
    if (stride <= 0 || usable <= 0) return 1; // bloco sem largura útil; continua

    for (int row = 0; row < pDraw->iHeight; row++) {
        ctx->scaler->addSpan(pDraw->y + row, pDraw->x, src + (size_t)row * stride, usable);
    }
    return 1; // continua a descodificação
}

// Abre, calcula o rectângulo final (proporção preservada dentro da caixa) e
// descodifica já para o scaler. O rectângulo só se sabe depois de abrir o
// ficheiro, por isso é aqui que é calculado e devolvido em `outFit`, em vez de
// abrir a capa uma vez para as dimensões e outra para a descodificar.
bool decodeJpegToScaler(const uint8_t* data, size_t size, int boxW, int boxH, GrayBoxScaler& scaler,
                        FitRect* outFit) {
    // A JPEGDEC embute vários KB de tabelas Huffman/quantização na própria
    // struct (não são ponteiros para heap): como objecto local rebentava a
    // stack do loopTask no dispositivo (v1.19.1). Fica no heap.
    JPEGDEC* jpg = new (std::nothrow) JPEGDEC();
    if (!jpg) return false;
    if (!jpg->openRAM(const_cast<uint8_t*>(data), (int)size, jpegDrawCallback)) {
        delete jpg;
        return false;
    }

    int nativeW = jpg->getWidth();
    int nativeH = jpg->getHeight();
    if (nativeW <= 0 || nativeH <= 0) {
        jpg->close();
        delete jpg;
        return false;
    }

    FitRect fit = book32::fitInsideBox(nativeW, nativeH, boxW, boxH);
    if (fit.w <= 0 || fit.h <= 0) {
        jpg->close();
        delete jpg;
        return false;
    }

    // Maior factor de redução (2/4/8) que ainda deixa a imagem descodificada
    // maior ou igual ao tamanho final: descodificar menos é mais rápido e a
    // média de área a seguir continua a ter pelo menos um pixel por célula.
    int scaleFactor = 1;
    int scaleOption = 0;
    struct ScaleStep {
        int factor, option;
    };
    static const ScaleStep kScales[] = {
        {8, JPEG_SCALE_EIGHTH}, {4, JPEG_SCALE_QUARTER}, {2, JPEG_SCALE_HALF}};
    for (const ScaleStep& s : kScales) {
        if (nativeW / s.factor >= fit.w && nativeH / s.factor >= fit.h) {
            scaleFactor = s.factor;
            scaleOption = s.option;
            break;
        }
    }

    int srcW = nativeW / scaleFactor;
    int srcH = nativeH / scaleFactor;
    if (srcW <= 0 || srcH <= 0 || !scaler.begin(srcW, srcH, fit.w, fit.h)) {
        jpg->close();
        delete jpg;
        return false;
    }

    DecodeContext ctx = {&scaler, nullptr, 0};
    jpg->setUserPointer(&ctx);
    jpg->setPixelType(EIGHT_BIT_GRAYSCALE);
    // JPEG_LUMA_ONLY salta a descodificação das componentes de cor, que não
    // são usadas: uma capa a cores custa quase o mesmo que uma já cinzenta.
    int ok = jpg->decode(0, 0, scaleOption | JPEG_LUMA_ONLY);
    jpg->close();
    delete jpg;
    if (!ok) return false;
    *outFit = fit;
    return true;
}

// --- PNG -------------------------------------------------------------------

inline uint8_t lumaFromRgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((77 * (int)r + 150 * (int)g + 29 * (int)b) >> 8);
}

// Mistura com fundo branco: as capas com transparência (logótipos, cantos
// arredondados) ficariam com o transparente a preto se o alfa fosse ignorado.
inline uint8_t overWhite(uint8_t value, uint8_t alpha) {
    return (uint8_t)(((int)value * alpha + 255 * (255 - alpha)) / 255);
}

// Amplia amostras de 1/2/4 bits para 0..255 (0b01 em 2 bits vale 85, não 1).
inline uint8_t expandBits(uint8_t sample, int bits) {
    switch (bits) {
        case 1:
            return sample ? 255 : 0;
        case 2:
            return (uint8_t)(sample * 85);
        case 4:
            return (uint8_t)(sample * 17);
        default:
            return sample;
    }
}

// Lê a amostra `index` de uma linha empacotada com `bits` bits por amostra.
inline uint8_t sampleAt(const uint8_t* row, int index, int bits) {
    if (bits == 8) return row[index];
    int perByte = 8 / bits;
    uint8_t byte = row[index / perByte];
    int shift = 8 - bits * ((index % perByte) + 1);
    return (uint8_t)((byte >> shift) & ((1 << bits) - 1));
}

// Uma linha de PNG (qualquer tipo de pixel dos que a PNGdec suporta: cinzento,
// paleta, RGB, com ou sem alfa; 1/2/4/8 bits por amostra) convertida para
// luminância de 8 bits em `out`.
void pngLineToLuma(const PNGDRAW* pDraw, uint8_t* out, int count) {
    const uint8_t* s = pDraw->pPixels;
    const int bits = pDraw->iBpp;

    switch (pDraw->iPixelType) {
        case PNG_PIXEL_GRAYSCALE:
            for (int x = 0; x < count; x++)
                out[x] = expandBits(sampleAt(s, x, bits), bits);
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            for (int x = 0; x < count; x++)
                out[x] = overWhite(s[x * 2], s[x * 2 + 1]);
            break;
        case PNG_PIXEL_TRUECOLOR:
            for (int x = 0; x < count; x++) {
                out[x] = lumaFromRgb(s[x * 3], s[x * 3 + 1], s[x * 3 + 2]);
            }
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            for (int x = 0; x < count; x++) {
                const uint8_t* p = &s[x * 4];
                out[x] = overWhite(lumaFromRgb(p[0], p[1], p[2]), p[3]);
            }
            break;
        case PNG_PIXEL_INDEXED:
            for (int x = 0; x < count; x++) {
                uint8_t idx = sampleAt(s, x, bits);
                const uint8_t* pal = &pDraw->pPalette[(size_t)idx * 3];
                uint8_t luma = lumaFromRgb(pal[0], pal[1], pal[2]);
                // A paleta alfa (tRNS) vem a seguir às 256 entradas RGB, ver
                // PNGRGB565() na própria PNGdec.
                if (pDraw->iHasAlpha) luma = overWhite(luma, pDraw->pPalette[768 + idx]);
                out[x] = luma;
            }
            break;
        default:
            memset(out, 0xFF, (size_t)count); // tipo desconhecido: branco, nunca lixo
            break;
    }
}

// A PNGdec guarda a linha actual e a anterior no mesmo buffer interno
// (PNG_MAX_BUFFERED_PIXELS, ver platformio.ini), mas a guarda que traz só
// compara esse tamanho com UMA linha: um PNG com uma linha entre metade e a
// totalidade do buffer passa a guarda e escreve para lá do fim da struct.
// Aqui recusa-se essa capa antes de descodificar, com a conta certa (duas
// linhas, um byte de filtro cada, mais a folga do alinhamento a 16).
bool pngLineFits(int pixelType, int bpp, int width) {
    if (width <= 0 || bpp <= 0) return false;
    int samplesPerPixel;
    switch (pixelType) {
        case PNG_PIXEL_GRAYSCALE:
        case PNG_PIXEL_INDEXED:
            samplesPerPixel = 1;
            break;
        case PNG_PIXEL_GRAY_ALPHA:
            samplesPerPixel = 2;
            break;
        case PNG_PIXEL_TRUECOLOR:
            samplesPerPixel = 3;
            break;
        case PNG_PIXEL_TRUECOLOR_ALPHA:
            samplesPerPixel = 4;
            break;
        default:
            return false;
    }
    long pitch = ((long)samplesPerPixel * bpp * width + 7) / 8;
    return (2 * (pitch + 1) + 64) <= PNG_MAX_BUFFERED_PIXELS;
}

int pngDrawCallback(PNGDRAW* pDraw) {
    if (!pDraw || !pDraw->pPixels) return 0; // aborta a descodificação
    DecodeContext* ctx = (DecodeContext*)pDraw->pUser;
    if (!ctx || !ctx->scaler || !ctx->lineBuf) return 0;

    int count = pDraw->iWidth;
    if (count > ctx->lineBufPixels) count = ctx->lineBufPixels;
    if (count <= 0) return 1;

    pngLineToLuma(pDraw, ctx->lineBuf, count);
    ctx->scaler->addSpan(pDraw->y, 0, ctx->lineBuf, count);
    return 1;
}

// Mesma forma do decodeJpegToScaler(): abre, calcula o rectângulo final e
// descodifica, tudo numa passagem.
bool decodePngToScaler(const uint8_t* data, size_t size, int boxW, int boxH, GrayBoxScaler& scaler,
                       FitRect* outFit) {
    // A PNGIMAGE embutida na classe PNG traz a janela de 32 KB do zlib mais os
    // buffers de linha: ~50 KB, muito acima do que a stack do loopTask aguenta
    // (mesma razão da JPEGDEC acima). Preferir PSRAM, que é onde já vive o
    // ficheiro da capa lido do EPUB.
    void* raw = ps_malloc(sizeof(PNG));
    if (!raw) raw = malloc(sizeof(PNG));
    if (!raw) return false;
    PNG* png = new (raw) PNG();

    // openRAM() devolve PNG_SUCCESS (0), ao contrário do openRAM() da JPEGDEC,
    // que devolve 1. Aqui falham também os PNG entrelaçados, os de 16 bits por
    // canal e os mais largos do que o buffer de linha (PNG_MAX_BUFFERED_PIXELS,
    // ver platformio.ini) — todos casos em que a capa fica simplesmente sem
    // miniatura, como qualquer outro ficheiro que não se saiba abrir.
    if (png->openRAM(const_cast<uint8_t*>(data), (int)size, pngDrawCallback) != PNG_SUCCESS) {
        png->~PNG();
        free(raw);
        return false;
    }

    int srcW = png->getWidth();
    int srcH = png->getHeight();
    FitRect fit = book32::fitInsideBox(srcW, srcH, boxW, boxH);
    if (srcW <= 0 || srcH <= 0 || fit.w <= 0 || fit.h <= 0 || (long)srcW * srcH > kMaxPngPixels ||
        !pngLineFits(png->getPixelType(), png->getBpp(), srcW) || !scaler.begin(srcW, srcH, fit.w, fit.h)) {
        png->close();
        png->~PNG();
        free(raw);
        return false;
    }

    uint8_t* lineBuf = (uint8_t*)ps_malloc((size_t)srcW);
    if (!lineBuf) lineBuf = (uint8_t*)malloc((size_t)srcW);
    if (!lineBuf) {
        png->close();
        png->~PNG();
        free(raw);
        return false;
    }

    DecodeContext ctx = {&scaler, lineBuf, srcW};
    int rc = png->decode(&ctx, 0);
    png->close();
    png->~PNG();
    free(raw);
    free(lineBuf);
    if (rc != PNG_SUCCESS) return false;
    *outFit = fit;
    return true;
}

bool looksLikeJpeg(const uint8_t* d, size_t n) {
    return n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

bool looksLikePng(const uint8_t* d, size_t n) {
    static const uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && memcmp(d, kSig, sizeof(kSig)) == 0;
}

} // namespace

bool decodeCoverToBitmap(const uint8_t* data, size_t size, int boxW, int boxH, uint8_t* outBuffer,
                         CoverFit* outFit) {
    if (!data || size == 0 || !outBuffer || boxW <= 0 || boxH <= 0) return false;

    const size_t outBytes = (size_t)((boxW + 7) / 8) * (size_t)boxH;
    memset(outBuffer, 0, outBytes); // 0 = fundo branco em toda a caixa

    // O formato é decidido pelos bytes, não pela extensão nem pelo media-type
    // do OPF: há EPUBs com "cover.jpg" que é afinal um PNG, e dar um ao
    // descodificador errado só produzia uma capa em falta sem explicação.
    bool isJpeg = looksLikeJpeg(data, size);
    bool isPng = !isJpeg && looksLikePng(data, size);
    if (!isJpeg && !isPng) return false;

    GrayBoxScaler scaler;
    FitRect fit = {0, 0, 0, 0};
    bool decoded = isJpeg ? decodeJpegToScaler(data, size, boxW, boxH, scaler, &fit)
                          : decodePngToScaler(data, size, boxW, boxH, scaler, &fit);
    if (!decoded) return false;

    size_t grayBytes = (size_t)fit.w * (size_t)fit.h;
    uint8_t* gray = (uint8_t*)ps_malloc(grayBytes);
    if (!gray) gray = (uint8_t*)malloc(grayBytes);
    if (!gray) return false;

    bool ok = scaler.resolve(gray);
    if (ok) {
        book32::autoLevelGray(gray, grayBytes, kAutoLevelLowPermille, kAutoLevelHighPermille,
                              kAutoLevelMinSpan);
        book32::ditherToBitmap1bpp(gray, fit.w, fit.h, outBuffer, boxW, boxH, fit.x, fit.y);
        if (outFit) {
            outFit->x = fit.x;
            outFit->y = fit.y;
            outFit->w = fit.w;
            outFit->h = fit.h;
        }
    } else {
        memset(outBuffer, 0, outBytes); // resolve() falhou a meio: caixa limpa
    }
    free(gray);
    return ok;
}
