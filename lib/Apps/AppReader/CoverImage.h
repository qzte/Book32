#ifndef COVER_IMAGE_H
#define COVER_IMAGE_H

#include <Arduino.h>

// Book32 v1.19.0 — capa do EPUB (bytes JPEG crus) para bitmap monocromo do
// tamanho exacto de um item da biblioteca.
//
// Usa a dithering Floyd-Steinberg já embutida na JPEGDEC (pixel type
// ONE_BIT_DITHERED) em vez de qualquer heurística própria de conversão para
// preto-e-branco: o resultado visual não pôde ser verificado num ecrã e-ink
// real nesta sessão, por isso preferiu-se um algoritmo já testado e usado
// por outros projectos de e-ink (ver examples/dithering/ na própria
// biblioteca) a inventar um. A redução ao tamanho exacto do item da
// biblioteca é feita à parte, por amostragem do vizinho mais próximo — a
// JPEGDEC só sabe reduzir por factores fixos de 2/4/8.
//
// Só JPEG é suportado nesta versão: a maioria das capas de EPUB comerciais
// são JPEG, e um EPUB com capa PNG fica simplesmente sem capa (o item volta
// ao desenho genérico de sempre) em vez de arriscar um segundo descodificador
// não verificável. Ver docs/plans/2026-08-31-capas-reais-biblioteca-design.md.

// Descodifica `jpegData` (jpegSize bytes) para um bitmap 1bpp empacotado,
// exactamente outWidth x outHeight, na convenção do
// Adafruit_GFX::drawBitmap(): bit=1 pinta na cor pedida, bit=0 deixa o fundo
// como estava. `outBuffer` tem de ter pelo menos ((outWidth+7)/8)*outHeight
// bytes, já alocados pelo chamador.
//
// Devolve false (outBuffer fica por tocar) se os bytes não forem um JPEG que
// a JPEGDEC saiba abrir, se a descodificação falhar a meio, ou se a alocação
// de memória (PSRAM/heap) falhar — nunca lança nem trava por causa de um
// ficheiro do utilizador malformado.
bool decodeJpegCoverToBitmap(const uint8_t* jpegData, size_t jpegSize, int outWidth, int outHeight,
                             uint8_t* outBuffer);

#endif
