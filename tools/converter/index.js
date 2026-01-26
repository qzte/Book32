const puppeteer = require('puppeteer');
const sharp = require('sharp');
const AdmZip = require('adm-zip');
const fs = require('fs-extra');
const path = require('path');
const yargs = require('yargs/yargs');
const { hideBin } = require('yargs/helpers');
const Packer = require('./Packer');

// Configuration
const DEVICE_WIDTH = 300;  // Portrait orientation (was 400)
const DEVICE_HEIGHT = 400; // Portrait orientation (was 300)
const OUTPUT_DIR = './output';
const BOOKS_DIR = './books';

async function main() {
    const argv = yargs(hideBin(process.argv)).option('input', {
        alias: 'i',
        description: 'Input EPUB file (Optional: If omitted, scans ./books folder)',
        type: 'string',
        demandOption: false
    }).argv;

    // Launch Browser once
    console.log("Launching Processor (Multi-Core Mode)...");
    const browser = await puppeteer.launch({ headless: "new" });

    try {
        if (argv.input) {
            // Single Mode
            await convertBook(argv.input, browser);
        } else {
            // Batch Mode
            fs.ensureDirSync(BOOKS_DIR);
            const files = fs.readdirSync(BOOKS_DIR).filter(f => f.toLowerCase().endsWith('.epub'));

            if (files.length === 0) {
                console.log("No .epub files found in 'books' folder.");
            } else {
                console.log(`Found ${files.length} books. Checking...`);
                for (const f of files) {
                    const inputPath = path.join(BOOKS_DIR, f);
                    const outName = path.basename(f, path.extname(f)) + ".b32";
                    const outPath = path.join(OUTPUT_DIR, outName);

                    if (fs.existsSync(outPath)) {
                        console.log(`[SKIP] ${f} (Already exists)`);
                        continue;
                    }

                    console.log(`[CONVERTing] ${f}...`);
                    try {
                        await convertBook(inputPath, browser);
                    } catch (e) {
                        console.error(`Failed to convert ${f}:`, e);
                    }
                }
            }
        }
    } finally {
        await browser.close();
    }
}

async function convertBook(epubPath, browser) {
    const tempDir = path.join(__dirname, 'temp_' + Date.now()); // Unique temp dir

    try {
        // 1. Extract EPUB
        fs.ensureDirSync(tempDir);
        const zip = new AdmZip(epubPath);
        zip.extractAllTo(tempDir, true);

        // 2. Find Content
        const opfPath = findOpf(tempDir);
        if (!opfPath) throw new Error("Could not find OPF file in EPUB");

        const spine = parseSpine(opfPath);
        if (spine.length === 0) throw new Error("Empty spine");

        // Initialize Packer
        const packer = new Packer(DEVICE_WIDTH, DEVICE_HEIGHT);

        // 2.5 Extract Cover (Thumbnail: 120x160)
        let coverBuffer = null;
        try {
            const coverPath = findCover(tempDir, opfPath);
            if (coverPath) {
                console.log(`    Found Cover: ${path.basename(coverPath)}`);
                // Process Cover
                const { data } = await sharp(coverPath)
                    .resize(120, 160) // Menu Thumbnail Size
                    .flatten({ background: { r: 255, g: 255, b: 255 } })
                    .toColourspace('b-w')
                    .threshold(128)
                    .raw()
                    .toBuffer({ resolveWithObject: true });

                // Pack bits then Compress
                const packedBits = packer.packBits(data);
                // packer has zlib exposed? No, let's use zlib directly in index.js too or expose a helper.
                // Packer.js handles page compression internally in addPage, but coverBuffer is manual.
                const zlib = require('zlib');
                coverBuffer = zlib.deflateSync(packedBits, { level: 9 });
            }
        } catch (e) {
            console.warn("    Warning: Failed to process cover:", e.message);
        }

        // --- CONCURRENCY SETUP ---
        const CONCURRENCY = 4; // number of parallel pages
        const pages = [];
        for (let i = 0; i < Math.min(CONCURRENCY, spine.length); i++) {
            const p = await browser.newPage();
            await p.setViewport({ width: DEVICE_WIDTH, height: DEVICE_HEIGHT });
            pages.push(p);
        }

        let completedChapters = 0;
        const totalChapters = spine.length;
        const chapterResults = new Array(totalChapters); // [index] -> [buffer1, buffer2...]

        // Worker function
        async function processWorker(pagePool, queue) {
            while (queue.length > 0) {
                const job = queue.shift(); // { index, path }
                const page = pagePool.pop(); // Claim a page

                try {
                    // Update progress (approximate)
                    const percent = Math.round((completedChapters / totalChapters) * 100);
                    process.stdout.write(`\r    Processing Chapter ${completedChapters + 1}/${totalChapters} [${percent}%] `);

                    const fileUrl = `file://${job.path}`;
                    await page.goto(fileUrl, { waitUntil: 'networkidle0' });

                    // Inject CSS
                    await page.addStyleTag({
                        content: `
                            @import url('https://fonts.googleapis.com/css2?family=Merriweather:wght@300;400&display=swap');
                            body, html {
                                margin: 0 !important;
                                padding: 10px !important;
                                width: ${DEVICE_WIDTH}px !important;
                                height: ${DEVICE_HEIGHT}px !important;
                                column-width: ${DEVICE_WIDTH - 20}px !important;
                                column-gap: 0 !important;
                                column-fill: auto !important;
                                overflow-y: hidden !important;
                                font-family: 'Merriweather', Georgia, serif !important;
                                font-size: 16px !important;
                                font-weight: 300 !important;
                                line-height: 1.6 !important;
                                text-align: justify;
                                color: #000 !important;
                                background: #fff !important;
                                -webkit-font-smoothing: antialiased;
                                text-rendering: optimizeLegibility;
                            }
                            p {
                                margin: 0 0 0.8em 0 !important;
                                text-indent: 1.5em;
                            }
                            h1, h2, h3 {
                                font-weight: 400 !important;
                                margin: 1em 0 0.5em 0 !important;
                                line-height: 1.3 !important;
                            }
                            img {
                                max-width: 100%;
                                max-height: ${DEVICE_HEIGHT - 20}px;
                                object-fit: contain;
                            }
                        `
                    });

                    const scrollWidth = await page.evaluate(() => document.body.scrollWidth);
                    const chapterPages = Math.ceil(scrollWidth / DEVICE_WIDTH);

                    const chapterBuffers = [];

                    for (let i = 0; i < chapterPages; i++) {
                        await page.evaluate((offset) => {
                            document.documentElement.scrollLeft = offset;
                        }, i * DEVICE_WIDTH);

                        await new Promise(r => setTimeout(r, 20)); // Render delay

                        const buffer = await page.screenshot({
                            clip: { x: 0, y: 0, width: DEVICE_WIDTH, height: DEVICE_HEIGHT },
                            encoding: 'binary'
                        });

                        const { data } = await sharp(buffer)
                            .resize(DEVICE_WIDTH, DEVICE_HEIGHT)
                            .flatten({ background: { r: 255, g: 255, b: 255 } })
                            .gamma(1.2) // Slight gamma adjustment for better text
                            .toColourspace('b-w')
                            .threshold(140) // Slightly higher threshold for crisper text
                            .raw()
                            .toBuffer({ resolveWithObject: true });

                        chapterBuffers.push(data);
                    }

                    chapterResults[job.index] = chapterBuffers;
                    completedChapters++;

                } catch (e) {
                    console.error(`\nError in worker processing chapter ${job.index}:`, e);
                } finally {
                    pagePool.push(page); // Return page to pool
                }
            }
        }

        // Prepare Queue
        const queue = spine.map((path, index) => ({ index, path }));

        // Launch Workers
        const numWorkers = Math.min(CONCURRENCY, queue.length);
        const workers = [];
        for (let i = 0; i < numWorkers; i++) {
            workers.push(processWorker(pages, queue));
        }

        await Promise.all(workers);
        process.stdout.write('\n');

        // Close pages
        for (const p of pages) await p.close();

        // 8. Assemble all into Packer
        let totalPages = 0;
        for (const buffs of chapterResults) {
            if (buffs) {
                for (const b of buffs) {
                    packer.addPage(b);
                    totalPages++;
                }
            }
        }

        // Save .b32
        const outName = path.basename(epubPath, path.extname(epubPath)) + ".b32";
        const outPath = path.join(OUTPUT_DIR, outName);
        fs.ensureDirSync(OUTPUT_DIR);
        packer.writeToFile(outPath, coverBuffer);

        console.log(`  -> Saved: ${outName} (${totalPages} pages)`);

    } finally {
        if (fs.existsSync(tempDir)) fs.removeSync(tempDir);
    }
}

// Helpers
function findOpf(startDir) {
    const files = fs.readdirSync(startDir);
    for (const f of files) {
        const full = path.join(startDir, f);
        if (fs.statSync(full).isDirectory()) {
            const found = findOpf(full);
            if (found) return found;
        } else if (f.endsWith('.opf')) {
            return full;
        }
    }
    return null;
}

function parseSpine(opfPath) {
    const content = fs.readFileSync(opfPath, 'utf-8');
    const manifest = {};

    // Improved Manifest Parsing
    const itemTagRegex = /<item\s+([^>]+)>/gi;
    let match;
    while ((match = itemTagRegex.exec(content)) !== null) {
        const attrs = match[1];
        const idMatch = attrs.match(/id=["']([^"']+)["']/i);
        const hrefMatch = attrs.match(/href=["']([^"']+)["']/i);

        if (idMatch && hrefMatch) {
            manifest[idMatch[1]] = hrefMatch[1];
        }
    }

    // Improved Spine Parsing
    const spineRefs = [];
    const itemRefRegex = /<itemref\s+([^>]+)>/gi;
    while ((match = itemRefRegex.exec(content)) !== null) {
        const attrs = match[1];
        const idRefMatch = attrs.match(/idref=["']([^"']+)["']/i);
        if (idRefMatch) {
            spineRefs.push(idRefMatch[1]);
        }
    }

    console.log(`    Debug: Found ${Object.keys(manifest).length} manifest items and ${spineRefs.length} spine items.`);

    const opfDir = path.dirname(opfPath);
    return spineRefs.map(id => {
        const href = manifest[id];
        if (!href) {
            console.warn(`    Warning: Spine itemref '${id}' not found in manifest.`);
            return null;
        }
        const decodedHref = decodeURIComponent(href);
        return path.resolve(opfDir, decodedHref);
    }).filter(p => p !== null);
}

function findCover(rootDir, opfPath) {
    const content = fs.readFileSync(opfPath, 'utf-8');
    const metaRegex = /<meta\s+[^>]*name=["']cover["']\s+[^>]*content=["']([^"']+)["']/i;
    let match = metaRegex.exec(content);

    if (match) {
        let coverId = match[1];
        console.log(`    Debug: Found cover ID via meta name="cover": ${coverId}`);
        const idRegex = new RegExp(`<item\\s+[^>]*id=["']${coverId}["'][^>]*>`, 'i');
        const itemMatch = idRegex.exec(content);

        if (itemMatch) {
            const attrs = itemMatch[0];
            const hrefMatch = attrs.match(/href=["']([^"']+)["']/i);
            if (hrefMatch) {
                return path.resolve(path.dirname(opfPath), decodeURIComponent(hrefMatch[1]));
            }
        }
    }

    const itemPropRegex = /<item\s+[^>]*properties=["'][^"']*cover-image[^"']*["'][^>]*>/i;
    match = itemPropRegex.exec(content);
    if (match) {
        const attrs = match[0];
        const hrefMatch = attrs.match(/href=["']([^"']+)["']/i);
        if (hrefMatch) {
            console.log(`    Debug: Found cover via properties="cover-image"`);
            return path.resolve(path.dirname(opfPath), decodeURIComponent(hrefMatch[1]));
        }
    }

    return null;
}

main().catch(err => console.error(err));
