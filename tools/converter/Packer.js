const zlib = require('zlib');
const fs = require('fs-extra');

class Packer {
    constructor(width, height) {
        this.width = width;
        this.height = height;
        this.pages = []; // Array of Buffers (Compressed)
        this.offsets = [];
    }

    addPage(rawBuffer) {
        // Input: Raw Buffer (w*h pixels, 1 byte per pixel)
        // 1. Pack into 1-bit bitmap (8 pixels per byte)
        const bitBuffer = this.packBits(rawBuffer);

        // 2. Compress using Deflate (Zlib) Level 9 (Best compression)
        const compressed = zlib.deflateSync(bitBuffer, { level: 9 });

        this.pages.push(compressed);
    }


    packBits(raw) {
        // We pack 8 pixels into 1 byte. MSB first.
        // Input: 0x00 (Black) or 0xFF (White)
        // IMPORTANT: Pack row-by-row to handle width that's not divisible by 8

        const bytesPerRow = Math.ceil(this.width / 8);
        const totalBytes = bytesPerRow * this.height;
        const out = Buffer.alloc(totalBytes);

        for (let y = 0; y < this.height; y++) {
            for (let x = 0; x < this.width; x++) {
                const pixelIndex = y * this.width + x;
                const pixel = raw[pixelIndex];

                // Calculate byte position in output buffer
                const byteIndex = y * bytesPerRow + Math.floor(x / 8);
                const bitIndex = 7 - (x % 8);

                if (pixel <= 128) {
                    // Set bit to 1 (Black) - inverted polarity
                    out[byteIndex] |= (1 << bitIndex);
                }
            }
        }
        return out;
    }

    writeToFile(path, coverBuffer = null) {
        // File Format:
        // [4: "BK32"]
        // [2: Version=3] (3 = Deflate Compression)
        // [2: Width]
        // [2: Height]
        // [2: PageCount]
        // [4: CoverDataLength] (0 if none)
        // [N: CoverData] (Should be Deflate compressed buffer)
        // [4 * PageCount: Table of Offsets]
        // [Data...]

        const pageCount = this.pages.length;
        const coverLen = coverBuffer ? coverBuffer.length : 0;

        // Header Static = 16 bytes.
        // Offsets start after CoverData.

        const offsetTableStart = 16 + coverLen;
        const offsetTableSize = 4 * pageCount;

        let currentOffset = offsetTableStart + offsetTableSize;
        const offsets = [];

        for (const p of this.pages) {
            offsets.push(currentOffset);
            currentOffset += p.length;
        }

        const fd = fs.openSync(path, 'w');

        // Header
        fs.writeSync(fd, Buffer.from("BK32"));
        // Helper to write UInt16LE
        const buf2 = Buffer.alloc(2);
        buf2.writeUInt16LE(3); fs.writeSync(fd, buf2); // Version 3
        buf2.writeUInt16LE(this.width); fs.writeSync(fd, buf2);
        buf2.writeUInt16LE(this.height); fs.writeSync(fd, buf2);
        buf2.writeUInt16LE(pageCount); fs.writeSync(fd, buf2);

        // Cover Data Length
        const buf4 = Buffer.alloc(4);
        buf4.writeUInt32LE(coverLen); fs.writeSync(fd, buf4);

        // Cover Data
        if (coverLen > 0) {
            fs.writeSync(fd, coverBuffer);
        }

        // Offsets
        for (const off of offsets) {
            buf4.writeUInt32LE(off);
            fs.writeSync(fd, buf4);
        }

        // Page Data
        for (const p of this.pages) {
            fs.writeSync(fd, p);
        }

        fs.closeSync(fd);
    }
}

module.exports = Packer;
