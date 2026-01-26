#include "B32Reader.h"

B32Reader::B32Reader() {
    _width = 0;
    _height = 0;
    _pageCount = 0;
    _coverLen = 0;
}

B32Reader::~B32Reader() {
    close();
}

void B32Reader::close() {
    if (_file) _file.close();
}

bool B32Reader::open(const char* path) {
    close();

    if (!LittleFS.exists(path)) return false;
    _file = LittleFS.open(path, "r");
    if (!_file) return false;

    // 1. Header (16 bytes)
    char magic[5] = {0};
    _file.readBytes(magic, 4);
    if (strcmp(magic, "BK32") != 0) {
        Serial.println("Invalid BK32 Magic");
        close();
        return false;
    }

    uint16_t version;
    _file.read((uint8_t*)&version, 2);
    if (version != 3) {
        Serial.printf("Unsupported BK32 Version: %d\n", version);
        close();
        return false;
    }

    _file.read((uint8_t*)&_width, 2);
    _file.read((uint8_t*)&_height, 2);
    _file.read((uint8_t*)&_pageCount, 2);
    
    uint32_t coverLen;
    _file.read((uint8_t*)&coverLen, 4);
    _coverLen = coverLen;

    // Calculated Offsets
    // Header is 4+2+2+2+2+4 = 16 bytes.
    // Cover Data starts at 16.
    _coverOffset = 16;
    
    // Offset Table starts after Cover Data
    _tableOffset = 16 + _coverLen;
    
    // Don't load entire offset table into memory - read on demand
    // For 2859 pages, that would be 11KB+ of RAM
    
    Serial.printf("Book Opened: %d pages, CoverLen: %d\n", _pageCount, _coverLen);
    return true;
}

bool B32Reader::getCover(uint8_t* buffer, size_t bufferSize) {
    if (_coverLen == 0) return false;
    
    // Seek to cover
    _file.seek(_coverOffset);
    
    uint8_t* compBuf = (uint8_t*)malloc(_coverLen);
    if(!compBuf) return false;
    
    _file.read(compBuf, _coverLen);
    
    // Decompress
    size_t outLen = bufferSize;
    
    // tinfl_decompress_mem_to_mem(void *pOut_buf, size_t out_buf_len, const void *pSrc_buf, size_t src_buf_len, int flags);
    // Returns bytes written on success, or (size_t)-1 on failure
    size_t result = tinfl_decompress_mem_to_mem(buffer, outLen, compBuf, _coverLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
    
    free(compBuf);
    
    if (result == (size_t)-1) {
        Serial.println("Cover Decompression Failed");
        return false;
    }
    
    // We can update bufferSize if caller passed a pointer? No, we took it by value.
    // Assuming outLen is sufficient.
    
    return true;
}

bool B32Reader::readPage(uint16_t index, uint8_t* buffer) {
    if (index >= _pageCount) return false;
    
    // Read page offset from file on-demand
    _file.seek(_tableOffset + (index * 4));
    uint32_t startOffset;
    _file.read((uint8_t*)&startOffset, 4);
    
    uint32_t nextOffset;
    if (index == _pageCount - 1) {
        nextOffset = _file.size();
    } else {
        // Read next offset
        uint32_t nextOff;
        _file.read((uint8_t*)&nextOff, 4);
        nextOffset = nextOff;
    }
    
    size_t compLen = nextOffset - startOffset;
    
    if (compLen == 0) return false; // Empty page?
    
    _file.seek(startOffset);
    
    // Allocate compression buffer in PSRAM to avoid stack overflow
    uint8_t* compBuf = (uint8_t*)ps_malloc(compLen);
    if(!compBuf) {
        Serial.println("OOM Reading Page (PSRAM)");
        return false;
    }
    
    _file.read(compBuf, compLen);
    
    // Expected output size = bytes per row * height
    // For non-byte-aligned widths, we need ceil(width/8) bytes per row
    size_t bytesPerRow = (_width + 7) / 8;  // Equivalent to ceil(width/8)
    size_t outLen = bytesPerRow * _height;
    
    // Use streaming decompression with heap-allocated state to avoid stack overflow
    tinfl_decompressor *decomp = (tinfl_decompressor*)ps_malloc(sizeof(tinfl_decompressor));
    if (!decomp) {
        free(compBuf);
        Serial.println("Failed to allocate decompressor state");
        return false;
    }
    
    tinfl_init(decomp);
    
    size_t in_bytes = compLen;
    size_t out_bytes = outLen;
    const uint8_t *pIn = compBuf;
    uint8_t *pOut = buffer;
    
    tinfl_status status = tinfl_decompress(decomp, pIn, &in_bytes, buffer, pOut, &out_bytes, 
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    
    free(decomp);
    free(compBuf);
    
    if (status != TINFL_STATUS_DONE) {
        Serial.printf("Page %d Decompression Failed (status: %d)\n", index, status);
        return false;
    }
    
    return true;
}
