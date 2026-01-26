#ifndef B32_READER_H
#define B32_READER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// 1. Miniz is often built-in to ESP32 Arduino Core, but sometimes hidden.
// We can try using <rom/miniz.h> or if that fails, use standard zlib if available.
// ESP32-S3 ROM has miniz.
#include <rom/miniz.h>

class B32Reader {
public:
    B32Reader();
    ~B32Reader();

    bool open(const char* path);
    void close();

    // Metadata
    uint16_t getWidth() const { return _width; }
    uint16_t getHeight() const { return _height; }
    uint16_t getPageCount() const { return _pageCount; }
    
    // Cover
    bool hasCover() const { return _coverLen > 0; }
    // Returns a pointer to the decompressed cover bitmap (allocates memory, caller must free? No, let's keep it internal or return raw)
    // Actually, Display works best with raw buffers.
    // For now, let's just expose a method to load cover into a buffer.
    bool getCover(uint8_t* buffer, size_t bufferSize);

    // Reading
    // Decompresses page 'index' into 'buffer'. Buffer must be large enough (width*height/8 bytes).
    bool readPage(uint16_t index, uint8_t* buffer);

private:
    File _file;
    
    uint16_t _width;
    uint16_t _height;
    uint16_t _pageCount;
    uint32_t _coverLen;
    uint32_t _coverOffset; // Start of cover data in file
    uint32_t _tableOffset; // Start of offset table

    // Helper for decompression
    bool decompressChunk(uint32_t offset, size_t compressedLen, uint8_t* outBuffer, size_t outLen);
};

#endif
