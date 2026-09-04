#ifndef COVER_IMAGE_H
#define COVER_IMAGE_H

#include <Arduino.h>

// Book32 — capa do EPUB (bytes JPEG ou PNG crus) para o bitmap monocromo de
// um item da biblioteca.
//
// A conversão em si (média de área, contraste, dithering) vive em
// lib/Book32_Core/ImageDither.h, que é lógica pura e corre nos testes de host;
// aqui fica só o que precisa dos descodificadores (JPEGDEC/PNGdec) e da PSRAM.
//
// Pipeline, por esta ordem — ver ImageDither.h para o porquê:
//
//   1. descodificar em tons de cinzento (JPEG: 8 bits luma, com o factor de
//      redução 1/2, 1/4 ou 1/8 da própria JPEGDEC quando a capa é grande;
//      PNG: linha a linha, convertida para luminância);
//   2. reduzir por média de área ao tamanho final, sem deformar a capa — uma
//      capa 2:3 fica centrada na caixa com margem branca, em vez de esticada;
//   3. esticar o contraste;
//   4. dithering Floyd-Steinberg já no tamanho final.
//
// Até à v1.20 fazia-se o dithering da JPEGDEC (ONE_BIT_DITHERED) no tamanho
// descodificado e reduzia-se depois por vizinho mais próximo, o que amostrava
// pontos soltos do retículo e deixava as miniaturas ruidosas; e a capa era
// esticada para a caixa toda, ignorando a proporção. PNG não era suportado de
// todo — um EPUB com capa PNG ficava com o desenho genérico.

// Rectângulo que a capa ocupa dentro da caixa pedida, em coordenadas da caixa.
// Guardado no cache da miniatura (ver AppReader.cpp) para que a moldura seja
// desenhada à volta da capa e não à volta da caixa inteira.
struct CoverFit {
    int x, y, w, h;
};

// Descodifica `data` (size bytes, JPEG ou PNG) para um bitmap 1bpp empacotado,
// exactamente boxW x boxH, na convenção do Adafruit_GFX::drawBitmap(): bit=1
// pinta na cor pedida, bit=0 deixa o fundo como estava. `outBuffer` tem de ter
// pelo menos ((boxW+7)/8)*boxH bytes, já alocados pelo chamador, e é limpo
// (fundo branco) por esta função antes de desenhar.
//
// `outFit`, quando não é nulo, recebe o rectângulo ocupado pela capa.
//
// Devolve false — sem escrever nada em `outFit` e deixando `outBuffer` limpo —
// se os bytes não forem um JPEG/PNG que os descodificadores saibam abrir (PNG
// entrelaçado ou de 16 bits por canal incluídos, que a PNGdec não suporta), se
// a descodificação falhar a meio, ou se a alocação de memória (PSRAM/heap)
// falhar. Nunca lança nem trava por causa de um ficheiro do utilizador
// malformado.
bool decodeCoverToBitmap(const uint8_t* data, size_t size, int boxW, int boxH, uint8_t* outBuffer,
                         CoverFit* outFit);

// Lê só a largura/altura nativas de `data` (JPEG ou PNG), sem descodificar
// pixel nenhum — usa-se para reservar o espaço vertical de uma ilustração no
// meio de um capítulo antes de decidir se vale a pena (e quando) pagar o
// custo do decodeCoverToBitmap completo. false nos mesmos casos que
// decodeCoverToBitmap (formato não reconhecido, ficheiro corrompido).
bool probeImageDimensions(const uint8_t* data, size_t size, int* outWidth, int* outHeight);

#endif
