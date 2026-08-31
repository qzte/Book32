#include "CoverImage.h"
#include <JPEGDEC.h>
#include <cstring>

// Estado partilhado com o callback de desenho da JPEGDEC (JPEG_DRAW_CALLBACK
// é um ponteiro de função simples, sem contexto capturável — mesma limitação
// e mesma solução dos callbacks de zip em EpubLoader.cpp, ver `zipFd`
// estático ali). Só corre a partir de uma descodificação de cada vez
// (AppReader::resolveNextBookCover, uma capa por passagem do update()),
// nunca em paralelo, por isso estáticos de ficheiro chegam.
static uint8_t* g_canvas = nullptr;
static size_t g_canvasPitch = 0; // bytes por linha do canvas (não do resultado final)
static int g_canvasWAlloc = 0;   // largura alocada do canvas, em pixeis (>= largura lógica)
static int g_canvasHAlloc = 0;   // altura alocada do canvas, em linhas (>= altura lógica)

// Chamado pela JPEGDEC com blocos de pixels já reduzidos e já convertidos
// para 1bpp (pixel type ONE_BIT_DITHERED): 0 = preto, 1 = branco. Copia cada
// bloco para a posição correspondente no canvas, com verificação de limites
// própria — o canvas é dimensionado por nós (ver decodeJpegCoverToBitmap),
// não pela JPEGDEC, e um ficheiro do utilizador não pode escrever fora dele.
static int coverJpegDrawCallback(JPEGDRAW* pDraw) {
    if (!g_canvas || !pDraw || !pDraw->pPixels) return 0; // aborta a descodificação

    const uint8_t* src = (const uint8_t*)pDraw->pPixels;
    int rowBytes = pDraw->iWidth / 8;
    if (rowBytes <= 0) return 1; // bloco sem largura útil (borda); continua

    for (int row = 0; row < pDraw->iHeight; row++) {
        int dy = pDraw->y + row;
        if (dy < 0 || dy >= g_canvasHAlloc) continue;

        int byteOff = pDraw->x / 8;
        if (byteOff < 0 || (size_t)byteOff >= g_canvasPitch) continue;

        int copyBytes = rowBytes;
        if ((size_t)(byteOff + copyBytes) > g_canvasPitch) copyBytes = (int)(g_canvasPitch - byteOff);
        if (copyBytes <= 0) continue;

        memcpy(g_canvas + (size_t)dy * g_canvasPitch + byteOff, src + (size_t)row * rowBytes, copyBytes);
    }
    return 1; // continua a descodificação
}

// Amostragem pelo vizinho mais próximo do canvas (canvasW x canvasH lógicos,
// dentro de um canvas de canvasPitch bytes/linha) para o tamanho final
// outWidth x outHeight, com a troca de convenção de bit: a JPEGDEC devolve
// 0=preto/1=branco (ONE_BIT_DITHERED); o Adafruit_GFX::drawBitmap() espera
// 1=pintar/0=deixar o fundo — exactamente o oposto, por isso o bit é
// invertido ao copiar, não só reamostrado.
static void downsampleToOutput(const uint8_t* canvas, size_t canvasPitch, int canvasW, int canvasH,
                               int outWidth, int outHeight, uint8_t* outBuffer) {
    size_t outPitch = (size_t)(outWidth + 7) / 8;
    memset(outBuffer, 0, outPitch * (size_t)outHeight); // 0 = fundo (branco) em todo o lado, por defeito

    for (int oy = 0; oy < outHeight; oy++) {
        int sy = (int)(((long)oy * canvasH) / outHeight);
        if (sy >= canvasH) sy = canvasH - 1;
        if (sy < 0) sy = 0;

        for (int ox = 0; ox < outWidth; ox++) {
            int sx = (int)(((long)ox * canvasW) / outWidth);
            if (sx >= canvasW) sx = canvasW - 1;
            if (sx < 0) sx = 0;

            size_t srcByte = (size_t)sy * canvasPitch + (size_t)(sx / 8);
            uint8_t srcBit = (canvas[srcByte] >> (7 - (sx % 8))) & 0x01;
            if (srcBit == 0) { // preto na JPEGDEC -> pintar no Adafruit_GFX
                outBuffer[(size_t)oy * outPitch + (size_t)(ox / 8)] |= (uint8_t)(0x80 >> (ox % 8));
            }
        }
    }
}

bool decodeJpegCoverToBitmap(const uint8_t* jpegData, size_t jpegSize, int outWidth, int outHeight,
                             uint8_t* outBuffer) {
    if (!jpegData || jpegSize == 0 || !outBuffer || outWidth <= 0 || outHeight <= 0) return false;

    JPEGDEC jpg;
    if (!jpg.openRAM(const_cast<uint8_t*>(jpegData), (int)jpegSize, coverJpegDrawCallback)) return false;

    int nativeW = jpg.getWidth();
    int nativeH = jpg.getHeight();
    if (nativeW <= 0 || nativeH <= 0) {
        jpg.close();
        return false;
    }

    // O maior factor de redução (2/4/8) que ainda deixa o canvas descodificado
    // >= ao tamanho pedido — decodificar mais pequeno é mais rápido, e esta
    // descodificação corre uma vez por livro em segundo plano na biblioteca
    // (ver AppReader::resolveNextBookCover), mas ainda assim vale poupar
    // tempo num livro com uma capa de alta resolução.
    int scaleFactor = 1;
    int scaleOption = 0;
    struct ScaleStep {
        int factor, option;
    };
    static const ScaleStep kScales[] = {
        {8, JPEG_SCALE_EIGHTH}, {4, JPEG_SCALE_QUARTER}, {2, JPEG_SCALE_HALF}};
    for (const ScaleStep& s : kScales) {
        if (nativeW / s.factor >= outWidth && nativeH / s.factor >= outHeight) {
            scaleFactor = s.factor;
            scaleOption = s.option;
            break;
        }
    }

    int canvasW = nativeW / scaleFactor;
    int canvasH = nativeH / scaleFactor;
    if (canvasW <= 0 || canvasH <= 0) {
        jpg.close();
        return false;
    }

    // Alocado com folga (múltiplo de 16 pixeis de largura, +16 linhas de
    // altura) sobre o tamanho lógico: os blocos que a JPEGDEC entrega vêm
    // alinhados à grelha de MCU, que pode ultrapassar ligeiramente
    // canvasW/canvasH nos bordos direito/inferior. A amostragem final só lê
    // dentro do tamanho lógico (canvasW/canvasH), nunca da folga.
    int canvasWAlloc = ((canvasW + 15) / 16) * 16;
    int canvasHAlloc = canvasH + 16;
    size_t canvasPitch = (size_t)canvasWAlloc / 8;
    size_t canvasSize = canvasPitch * (size_t)canvasHAlloc;

    uint8_t* canvas = (uint8_t*)ps_malloc(canvasSize);
    if (!canvas) canvas = (uint8_t*)malloc(canvasSize);
    if (!canvas) {
        jpg.close();
        return false;
    }
    memset(canvas, 0xFF, canvasSize); // 0xFF = tudo branco (convenção ONE_BIT_DITHERED), cobre a folga

    size_t ditherSize = (size_t)canvasWAlloc * 16; // ver examples/dithering/ da JPEGDEC
    uint8_t* ditherBuf = (uint8_t*)ps_malloc(ditherSize);
    if (!ditherBuf) ditherBuf = (uint8_t*)malloc(ditherSize);
    if (!ditherBuf) {
        free(canvas);
        jpg.close();
        return false;
    }

    g_canvas = canvas;
    g_canvasPitch = canvasPitch;
    g_canvasWAlloc = canvasWAlloc;
    g_canvasHAlloc = canvasHAlloc;

    jpg.setPixelType(ONE_BIT_DITHERED);
    int decodeOk = jpg.decodeDither(0, 0, ditherBuf, scaleOption);
    jpg.close();
    free(ditherBuf);
    g_canvas = nullptr; // nunca deixar um ponteiro pendurado para a próxima chamada

    bool ok = false;
    if (decodeOk) {
        downsampleToOutput(canvas, canvasPitch, canvasW, canvasH, outWidth, outHeight, outBuffer);
        ok = true;
    }
    free(canvas);
    return ok;
}
