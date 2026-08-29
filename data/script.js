// Mobile nav: the sidebar becomes an off-canvas panel below 768px, driven by
// the .nav-open class on <body>. On desktop the class is inert.
function toggleNav() {
    setNav(!document.body.classList.contains('nav-open'));
}

function closeNav() {
    setNav(false);
}

function setNav(open) {
    document.body.classList.toggle('nav-open', open);
    const btn = document.querySelector('.nav-toggle');
    if (btn) btn.setAttribute('aria-expanded', open ? 'true' : 'false');
}

document.addEventListener('keydown', e => {
    if (e.key === 'Escape') closeNav();
});

function showTab(tabId) {
    closeNav();
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-links li').forEach(el => el.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');
    const navItems = ['dashboard', 'ereader', 'settings'];
    document.querySelectorAll('.nav-links li')[navItems.indexOf(tabId)].classList.add('active');

    // Load data when switching tabs
    if (tabId === 'ereader') {
        // Restores the last folder listing so the diff is there without having
        // to re-pick the folder (see the PC Library section).
        loadPcListing();
        fetchBooks();
        getReaderProgress();
    } else if (tabId === 'settings') {
        getWifiStatus();
        getDisplaySettings();
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// escapeHtml() serve para texto: escapa & < > mas deixa passar aspas e plicas,
// que é precisamente o que parte um valor dentro de um atributo. Nomes de
// ficheiro e títulos de EPUB contêm plicas com frequência ("O'Brien"), por isso
// tudo o que vai para um atributo passa por aqui.
function escapeAttr(text) {
    const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
    return String(text).replace(/[&<>"']/g, c => map[c]);
}

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        document.getElementById('battery-val').innerText = data.battery + '%' + (data.charging ? ' (Charging)' : '');
        document.getElementById('uptime-val').innerText = data.uptime;

        // Update version display
        if (data.version) {
            document.getElementById('current-ver').innerText = data.version;
            document.getElementById('version-display').innerText = data.version;
        }

        // Format free space in KB or MB
        const freeKB = Math.round(data.freeSpace / 1024);
        const totalKB = Math.round(data.totalSpace / 1024);
        document.getElementById('freespace-val').innerText = freeKB + ' / ' + totalKB + ' KB';

        // Update Header
        let voltageText = data.voltage.toFixed(2) + 'V';
        if (data.charging) {
            voltageText += ' ⚡';
            document.getElementById('header-voltage').style.color = '#00ff00'; // Bright Green for charging
        } else {
            document.getElementById('header-voltage').style.color = ''; // Default
        }
        document.getElementById('header-voltage').innerText = voltageText;

        const batIcon = document.getElementById('battery-icon');
        const level = parseInt(data.battery);

        // Snap to grid for CSS classes
        let visualLevel = 0;
        if (level > 90) visualLevel = 100;
        else if (level > 70) visualLevel = 80;
        else if (level > 50) visualLevel = 60;
        else if (level > 30) visualLevel = 40;
        else if (level > 10) visualLevel = 20;
        else visualLevel = 0;

        batIcon.setAttribute('data-level', visualLevel);

        // Update battery icon charging state
        if (data.charging) {
            batIcon.classList.add('charging');
        } else {
            batIcon.classList.remove('charging');
        }

    } catch (e) {
        console.error("Failed to fetch status", e);
    }
}

async function checkUpdate() {
    const btn = document.getElementById('check-update-btn');
    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    btn.innerText = "Checking...";
    msg.innerText = "";
    updateBtn.classList.add('hidden');

    try {
        const res = await fetch('/api/check_update');
        const data = await res.json();

        if (data.hasUpdate) {
            let updateParts = [];
            if (data.hasFirmware) updateParts.push("firmware");
            if (data.hasFilesystem) updateParts.push("web interface");

            // A tag e o corpo do release são texto de fora do dispositivo:
            // entram como conteúdo, não como marcação.
            msg.innerHTML = `<strong>New version available: ${escapeHtml(data.latest)}</strong>`;
            if (updateParts.length > 0) {
                msg.innerHTML += `<br><small>Includes: ${updateParts.join(" and ")}</small>`;
            }
            if (data.release_notes) {
                msg.innerHTML += `<br><small>${escapeHtml(data.release_notes)}</small>`;
            }
            msg.style.color = "var(--success)";
            updateBtn.classList.remove('hidden');
            btn.innerText = "Check Again";
        } else {
            msg.innerText = "You are up to date.";
            msg.style.color = "var(--text-secondary)";
            btn.innerText = "Check Again";
        }
    } catch (e) {
        msg.innerText = "Error checking update.";
        msg.style.color = "var(--danger)";
        btn.innerText = "Retry";
    }
}

async function performUpdate() {
    if (!confirm("Install update? Device will restart when complete.")) return;

    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    msg.innerText = "Downloading and installing update...";
    msg.style.color = "var(--accent)";
    updateBtn.classList.add('hidden');

    fetch('/api/update/all', { method: 'POST' });
    alert("Update started. The device will reboot when complete. This page will stop responding during the update.");
}

// === Ereader Book Management ===
// v1.2.2: case-insensitive extension checks (match firmware behavior)
const isEpub = f => f.toLowerCase().endsWith('.epub');
const isFont = f => f.toLowerCase().endsWith('.ttf');

let currentBooks = [];          // Server-provided order (books + fonts)
let saveOrderTimer = null;      // Debounce: avoid hammering flash on rapid clicks

async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    bookList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/books');
        const data = await res.json();
        currentBooks = (data.books || []);
        renderBooks();
    } catch (e) {
        bookList.innerHTML = '<p class="error">Error loading books.</p>';
        console.error("Failed to fetch books", e);
    }
}

// Epoch seconds -> a short local date. 0 means the device had no clock when
// the event happened (no NTP since the last power cut, see TimeMgr.h): an
// absent date is shown as absent, never guessed at.
function formatDate(epoch) {
    if (!epoch) return '\u2014';
    return new Date(epoch * 1000).toLocaleDateString();
}

const STATUS_LABEL = { unread: 'Unread', reading: 'Reading', read: 'Read' };

function statusBadge(book) {
    const key = book.status || 'unread';
    let label = STATUS_LABEL[key] || 'Unread';
    // The percentage rides along with "reading" only. On a read book it is
    // noise, and on an unread one it would contradict the badge.
    if (key === 'reading' && typeof book.percent === 'number') {
        label += ' ' + book.percent + '%';
    }
    // A manual mark is flagged so it is obvious why a book says what it says
    // when the position suggests otherwise.
    const manual = book.override && book.override !== 'auto';
    return `<span class="book-badge ${key}"${manual ? ' title="Set manually"' : ''}>` +
           `${escapeHtml(label)}${manual ? ' \u270e' : ''}</span>`;
}

function bookDates(book) {
    const parts = [];
    if (book.startedAt) parts.push('Started ' + formatDate(book.startedAt));
    if (book.finishedAt) parts.push('Finished ' + formatDate(book.finishedAt));
    if (book.lastReadAt && !book.finishedAt) parts.push('Last read ' + formatDate(book.lastReadAt));
    if (!parts.length) return '';
    return `<span class="book-dates">${escapeHtml(parts.join(' \u00b7 '))}</span>`;
}

// The list the user is looking at, after the filter and sort. Kept separate
// from currentBooks, which stays in the device's own order because that is what
// /api/books/order persists.
function visibleBooks() {
    const filter = (document.getElementById('book-filter') || {}).value || 'all';
    const sort = (document.getElementById('book-sort') || {}).value || 'manual';

    let list = currentBooks.slice();
    if (filter !== 'all') {
        // Fonts have no reading state at all, so any status filter drops them.
        list = list.filter(b => isEpub(b.filename) && b.status === filter);
    }
    if (sort === 'title') {
        list.sort((a, b) => a.name.localeCompare(b.name));
    } else if (sort !== 'manual') {
        // Most recent first, with undated books last rather than treated as
        // 1970 — they are unknown, not old.
        list.sort((a, b) => {
            const av = a[sort] || 0, bv = b[sort] || 0;
            if (av === bv) return a.name.localeCompare(b.name);
            if (!av) return 1;
            if (!bv) return -1;
            return bv - av;
        });
    }
    return { list, reorderable: filter === 'all' && sort === 'manual' };
}

function renderBooks() {
    const bookList = document.getElementById('book-list');
    const note = document.getElementById('book-list-note');
    if (!currentBooks.length) {
        bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        if (note) note.classList.add('hidden');
        renderPcDiff();
        return;
    }

    const view = visibleBooks();
    if (note) note.classList.toggle('hidden', view.reorderable);
    if (!view.list.length) {
        bookList.innerHTML = '<p class="hint">No books match this filter.</p>';
        renderPcDiff();
        return;
    }

    const epubs = currentBooks.filter(b => isEpub(b.filename));
    // Nomes de ficheiro e títulos vêm dos EPUB enviados. Interpolá-los num
    // atributo onclick partia o handler ao primeiro título com plica e deixava
    // um nome escolhido a dedo executar código nesta página; agora viajam em
    // data-* (com escape de atributo) e o clique é tratado por delegação.
    bookList.innerHTML = view.list.map(book => {
        const bookIsFont = isFont(book.filename);
        const nameAttr = escapeAttr(book.filename);
        let orderBtns = '';
        if (!bookIsFont && epubs.length > 1 && view.reorderable) {
            const idx = epubs.indexOf(book);
            orderBtns = `
                <span class="order-btns">
                    <button class="btn-order" ${idx === 0 ? 'disabled' : ''} data-action="move" data-dir="-1" data-filename="${nameAttr}" title="Move up">▲</button>
                    <button class="btn-order" ${idx === epubs.length - 1 ? 'disabled' : ''} data-action="move" data-dir="1" data-filename="${nameAttr}" title="Move down">▼</button>
                </span>`;
        }
        let statusControls = '';
        if (!bookIsFont) {
            const override = book.override || 'auto';
            statusControls = `
                ${statusBadge(book)}
                <select class="book-status-select" data-action="status" data-filename="${nameAttr}" title="Override the status">
                    <option value="auto"${override === 'auto' ? ' selected' : ''}>Automatic</option>
                    <option value="unread"${override === 'unread' ? ' selected' : ''}>Unread</option>
                    <option value="reading"${override === 'reading' ? ' selected' : ''}>Reading</option>
                    <option value="read"${override === 'read' ? ' selected' : ''}>Read</option>
                </select>`;
        }
        return `
        <div class="book-item">
            ${orderBtns}
            <span class="book-title">${bookIsFont ? '📂 [Font] ' : '📖 '}${escapeHtml(book.name)}${bookIsFont ? '' : bookDates(book)}</span>
            ${statusControls}
            <span class="book-size">${Math.round(book.size / 1024)} KB</span>
            <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(book.name)}">Delete</button>
        </div>
    `}).join('');
    bindBookListActions();
    renderPcDiff();
}

async function setBookStatus(filename, status) {
    const msg = document.getElementById('book-status-msg');
    try {
        const res = await fetch('/api/books/status', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            // The browser's clock, not the device's: the device may have none
            // since its last power cut, and this is the one moment a good
            // timestamp is guaranteed to be at hand.
            body: JSON.stringify({ filename, status, at: Math.floor(Date.now() / 1000) })
        });
        if (!res.ok) {
            const body = await res.json().catch(() => ({}));
            throw new Error(body.message || res.statusText);
        }
        if (msg) { msg.innerText = 'Status saved.'; msg.style.color = 'var(--success)'; }
        // Refetch rather than patching in place: the derived status and the
        // finish date are the server's to decide, and guessing them here is how
        // the two drift apart.
        await fetchBooks();
    } catch (e) {
        if (msg) { msg.innerText = 'Could not save status: ' + e.message; msg.style.color = 'var(--danger)'; }
        console.error('Failed to set book status', e);
    }
}

// O ouvinte fica no contentor, que sobrevive à substituição do innerHTML, por
// isso basta ligá-lo uma vez.
let bookListBound = false;
function bindBookListActions() {
    if (bookListBound) return;
    const bookList = document.getElementById('book-list');
    if (!bookList) return;
    bookList.addEventListener('click', e => {
        const btn = e.target.closest('button[data-action]');
        if (!btn) return;
        if (btn.dataset.action === 'delete') {
            deleteBook(btn.dataset.filename, btn.dataset.name);
        } else if (btn.dataset.action === 'move') {
            moveBook(btn.dataset.filename, Number(btn.dataset.dir));
        }
    });
    // The status control is a <select>, so it needs 'change' rather than the
    // click delegation above.
    bookList.addEventListener('change', e => {
        const sel = e.target.closest('select[data-action="status"]');
        if (!sel) return;
        setBookStatus(sel.dataset.filename, sel.value);
    });
    bookListBound = true;
}

function moveBook(filename, dir) {
    // Swap within the .epub subsequence only; fonts keep their positions.
    const epubIdxs = currentBooks
        .map((b, i) => isEpub(b.filename) ? i : -1)
        .filter(i => i >= 0);
    const pos = epubIdxs.findIndex(i => currentBooks[i].filename === filename);
    const target = pos + dir;
    if (pos < 0 || target < 0 || target >= epubIdxs.length) return;

    const a = epubIdxs[pos], b = epubIdxs[target];
    [currentBooks[a], currentBooks[b]] = [currentBooks[b], currentBooks[a]];
    renderBooks();
    scheduleSaveOrder();
}

function scheduleSaveOrder() {
    clearTimeout(saveOrderTimer);
    saveOrderTimer = setTimeout(saveBookOrder, 500);
}

async function saveBookOrder() {
    const order = currentBooks
        .filter(b => isEpub(b.filename))
        .map(b => b.filename);
    try {
        await fetch('/api/books/order', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order })
        });
    } catch (e) {
        console.error("Failed to save book order", e);
    }
}

function uploadBook() {
    const fileInput = document.getElementById('book-file');
    const status = document.getElementById('upload-status');
    const progressContainer = document.getElementById('upload-progress');
    const progressBar = document.getElementById('upload-progress-bar');

    if (!fileInput.files.length) {
        status.innerText = "Please select a file.";
        status.style.color = "var(--danger)";
        return;
    }

    const file = fileInput.files[0];
    if (!isEpub(file.name) && !isFont(file.name)) {
        status.innerText = "Only .epub and .ttf files are supported.";
        status.style.color = "var(--danger)";
        return;
    }

    // Show progress bar and reset
    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
    status.innerText = "Uploading...";
    status.style.color = "var(--accent)";

    const formData = new FormData();
    formData.append('file', file);

    // Use XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();

    // Track upload progress
    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressBar.style.width = percentComplete + '%';
            status.innerText = `Uploading... ${Math.round(percentComplete)}%`;
        }
    });

    // Handle completion
    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            progressBar.style.width = '100%';
            status.innerText = "Upload complete!";
            status.style.color = "var(--success)";
            fileInput.value = '';

            // Hide progress bar after a delay
            setTimeout(() => {
                progressContainer.classList.add('hidden');
            }, 2000);

            fetchBooks();
        } else {
            progressContainer.classList.add('hidden');
            status.innerText = "Upload failed: " + xhr.responseText;
            status.style.color = "var(--danger)";
        }
    });

    // Handle errors
    xhr.addEventListener('error', () => {
        progressContainer.classList.add('hidden');
        status.innerText = "Upload error.";
        status.style.color = "var(--danger)";
        console.error("Upload failed");
    });

    // Send the request
    xhr.open('POST', '/api/books/upload');
    xhr.send(formData);
}

// === PC Library ===
//
// The page is served over http:// from the device, so the File System Access
// API is out of reach (it needs a secure context) and so is any handle that
// would survive a reload. What does work on plain HTTP is <input
// webkitdirectory>, which yields the folder listing — names and sizes, no file
// contents — and that is all a diff needs.
//
// Consequences, both visible in the UI below: the folder must be re-picked
// before anything can actually be sent, and nothing is ever written back to
// the PC.

const PC_LISTING_KEY = 'book32.pcListing';

let pcFiles = new Map();   // original name -> File, only for a folder picked this session
let pcListing = null;      // { takenAt, files: [{name, size}] } — survives a reload

function pcSupported() {
    return 'webkitdirectory' in document.createElement('input');
}

function loadPcListing() {
    // Private windows, cleared site data and browsers set to block storage all
    // land here; the section just starts empty.
    try {
        const raw = localStorage.getItem(PC_LISTING_KEY);
        if (raw) pcListing = JSON.parse(raw);
    } catch (e) {
        pcListing = null;
    }
}

function savePcListing() {
    try {
        localStorage.setItem(PC_LISTING_KEY, JSON.stringify(pcListing));
    } catch (e) {
        // Not worth surfacing: the diff still works for this session.
        console.warn('Could not remember the folder listing', e);
    }
}

function pickPcFolder(input) {
    const files = Array.from(input.files || []).filter(f => isEpub(f.name));
    pcFiles = new Map();
    files.forEach(f => pcFiles.set(f.name, f));
    pcListing = {
        takenAt: Date.now(),
        files: files.map(f => ({ name: f.name, size: f.size }))
    };
    savePcListing();
    // Let the same folder be picked twice in a row (the input would otherwise
    // fire no change event the second time).
    input.value = '';
    renderPcDiff();
}

// Books on the device, by the original filename — which is what the PC folder
// holds, since uploads truncate the stored name but /api/books reports the
// original.
function readerBookNames() {
    const map = new Map();
    currentBooks.filter(b => isEpub(b.filename)).forEach(b => map.set(b.name, b));
    return map;
}

function renderPcDiff() {
    const card = document.getElementById('pc-library-card');
    if (!card) return;
    if (!pcSupported()) {
        // Most mobile browsers. Hiding beats showing a control that silently
        // does nothing.
        card.classList.add('hidden');
        return;
    }

    const age = document.getElementById('pc-listing-age');
    const out = document.getElementById('pc-diff');
    const sendAll = document.getElementById('pc-send-all');

    if (!pcListing || !pcListing.files.length) {
        age.innerText = '';
        out.innerHTML = '<p class="hint">No folder chosen yet.</p>';
        sendAll.disabled = true;
        return;
    }

    const canSend = pcFiles.size > 0;
    age.innerHTML = canSend
        ? `Folder read just now \u2014 ${pcListing.files.length} EPUB(s).`
        : `Listing from ${escapeHtml(new Date(pcListing.takenAt).toLocaleString())}. ` +
          `<strong>Choose the folder again to send anything</strong> \u2014 the browser does not let this page keep access to it across reloads.`;

    const onReader = readerBookNames();
    const pcNames = new Set(pcListing.files.map(f => f.name));

    const onlyPc = pcListing.files.filter(f => !onReader.has(f.name));
    const both = pcListing.files.filter(f => onReader.has(f.name));
    const onlyReader = Array.from(onReader.values()).filter(b => !pcNames.has(b.name));

    let html = '';

    html += `<h4 class="pc-group">Only on PC (${onlyPc.length})</h4>`;
    if (!onlyPc.length) {
        html += '<p class="hint">Nothing missing from the reader.</p>';
    } else {
        html += onlyPc.map(f => `
            <div class="book-item">
                <span class="book-title">\u2b06\ufe0f ${escapeHtml(f.name)}</span>
                <span class="book-size">${Math.round(f.size / 1024)} KB</span>
                <button class="btn-order" data-pc-action="send" data-name="${escapeAttr(f.name)}" ${canSend ? '' : 'disabled'}>Send</button>
            </div>`).join('');
    }

    html += `<h4 class="pc-group">Only on the reader (${onlyReader.length})</h4>`;
    if (!onlyReader.length) {
        html += '<p class="hint">Nothing on the reader that is missing from the folder.</p>';
    } else {
        html += onlyReader.map(b => `
            <div class="book-item">
                <span class="book-title">\u26a0\ufe0f ${escapeHtml(b.name)}</span>
                <span class="book-size">${Math.round(b.size / 1024)} KB</span>
                <button class="btn-delete" data-pc-action="delete" data-filename="${escapeAttr(b.filename)}" data-name="${escapeAttr(b.name)}">Delete</button>
            </div>`).join('');
    }

    // Matching is by name, so a book renamed on the PC shows up on both sides
    // at once. The sizes are what make that recognisable, so they are worth
    // showing even for the books that need no action.
    html += `<h4 class="pc-group">On both (${both.length})</h4>`;
    if (!both.length) {
        html += '<p class="hint">Nothing in common.</p>';
    } else {
        html += both.map(f => {
            const b = onReader.get(f.name);
            const sizeDiffers = b && Math.abs(b.size - f.size) > 1024;
            return `
            <div class="book-item">
                <span class="book-title">\u2705 ${escapeHtml(f.name)}</span>
                <span class="book-size${sizeDiffers ? ' warn' : ''}">${Math.round(f.size / 1024)} KB${sizeDiffers ? ' \u2260 ' + Math.round(b.size / 1024) + ' KB on reader' : ''}</span>
            </div>`;
        }).join('');
    }

    out.innerHTML = html;
    sendAll.disabled = !canSend || !onlyPc.length;
    bindPcActions();
}

let pcBound = false;
function bindPcActions() {
    if (pcBound) return;
    const out = document.getElementById('pc-diff');
    if (!out) return;
    out.addEventListener('click', e => {
        const btn = e.target.closest('button[data-pc-action]');
        if (!btn) return;
        if (btn.dataset.pcAction === 'send') {
            sendToReader([btn.dataset.name]);
        } else if (btn.dataset.pcAction === 'delete') {
            deleteBook(btn.dataset.filename, btn.dataset.name);
        }
    });
    pcBound = true;
}

// Free bytes on the ebook partition. Returns null when it cannot be read, and
// the caller then goes ahead rather than blocking on a diagnostic.
async function ebookFreeBytes() {
    try {
        const res = await fetch('/api/fs');
        const data = await res.json();
        const e = data.ebooks;
        if (!e || typeof e.total !== 'number' || typeof e.used !== 'number') return null;
        return e.total - e.used;
    } catch (err) {
        return null;
    }
}

function uploadOne(file) {
    return new Promise((resolve, reject) => {
        const form = new FormData();
        form.append('file', file);
        const xhr = new XMLHttpRequest();
        xhr.addEventListener('load', () => {
            if (xhr.status === 200) resolve();
            else reject(new Error(xhr.responseText || xhr.statusText));
        });
        xhr.addEventListener('error', () => reject(new Error('network error')));
        xhr.open('POST', '/api/books/upload');
        xhr.send(form);
    });
}

async function sendMissingToReader() {
    const onReader = readerBookNames();
    const missing = pcListing.files.filter(f => !onReader.has(f.name)).map(f => f.name);
    await sendToReader(missing);
}

async function sendToReader(names) {
    const status = document.getElementById('pc-status');
    const sendAll = document.getElementById('pc-send-all');
    const files = names.map(n => pcFiles.get(n)).filter(Boolean);

    if (!files.length) {
        status.innerText = 'Choose the folder again first \u2014 the browser cannot reopen it on its own.';
        status.style.color = 'var(--danger)';
        return;
    }

    // The ebook partition is 10 MB and a batch send fills it quickly. Checking
    // first turns a run of half-written uploads into one clear refusal.
    const needed = files.reduce((sum, f) => sum + f.size, 0);
    const free = await ebookFreeBytes();
    if (free !== null && needed > free) {
        status.innerText = `Not enough space: ${Math.round(needed / 1024)} KB to send, ` +
                           `${Math.round(free / 1024)} KB free. Delete something first.`;
        status.style.color = 'var(--danger)';
        return;
    }

    sendAll.disabled = true;
    status.style.color = 'var(--accent)';
    let sent = 0;
    const failed = [];
    for (const file of files) {
        status.innerText = `Sending ${sent + 1} of ${files.length}: ${file.name}`;
        try {
            // Serially, never in parallel: LittleFS takes one writer, and the
            // upload endpoint rejects a second request while one is in flight.
            await uploadOne(file);
            sent++;
        } catch (e) {
            failed.push(`${file.name} (${e.message})`);
        }
    }

    if (failed.length) {
        status.innerText = `Sent ${sent} of ${files.length}. Failed: ${failed.join('; ')}`;
        status.style.color = 'var(--danger)';
    } else {
        status.innerText = `Sent ${sent} book(s).`;
        status.style.color = 'var(--success)';
    }
    await fetchBooks();
}

async function deleteBook(filename, displayName) {
    // Use display name for confirmation, filename for API call
    const nameToShow = displayName || filename;
    if (!confirm(`Delete "${nameToShow}"?`)) return;

    try {
        const res = await fetch('/api/books/delete?name=' + encodeURIComponent(filename), {
            method: 'DELETE'
        });

        if (res.ok) {
            fetchBooks();
        } else {
            alert("Failed to delete book.");
        }
    } catch (e) {
        alert("Error deleting book.");
        console.error("Delete failed", e);
    }
}

// Initial Load
setInterval(fetchStatus, 5000);
fetchStatus();
getReaderSettings();
getReaderProgress();
getSleepSettings();
getWifiStatus();
getDisplaySettings();

function getReaderSettings() {
    fetch('/api/settings/reader')
        .then(response => response.json())
        .then(data => {
            if (data.refreshFrequency) {
                document.getElementById('refresh-rate').value = data.refreshFrequency;
            }
            if (data.fontSize) {
                document.getElementById('font-size').value = data.fontSize;
            }
            if (data.fontFamily !== undefined) {
                document.getElementById('font-family').value = data.fontFamily;
            }
        })
        .catch(error => console.error('Error loading reader settings:', error));
}

function saveReaderSettings() {
    const refreshRate = parseInt(document.getElementById('refresh-rate').value);
    const fontSize = parseInt(document.getElementById('font-size').value);
    const fontFamily = parseInt(document.getElementById('font-family').value);
    const statusDiv = document.getElementById('reader-settings-status');

    fetch('/api/settings/reader', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ refreshFrequency: refreshRate, fontSize: fontSize, fontFamily: fontFamily }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

// Bookmarks and "go to %" both act on this book: the one with a saved
// reading position, same scope as the "Reading Progress" card above them.
let currentLastBook = '';

function getReaderProgress() {
    fetch('/api/reader/progress')
        .then(response => response.json())
        .then(data => {
            const status = document.getElementById('reader-progress-status');
            currentLastBook = data.exists ? (data.lastBook || '') : '';
            updateBookScopedLabels();
            loadBookmarks();

            if (!status) return;
            if (data.exists) {
                const name = data.displayName || data.lastBook || 'Saved book';
                const page = data.page || 1;
                status.textContent = `${name} - page ${page}${data.resumeOnBoot ? ' (will resume on boot)' : ''}`;
            } else {
                status.textContent = 'No saved reading position.';
            }
        })
        .catch(error => console.error('Error loading reader progress:', error));
}

function updateBookScopedLabels() {
    const label = currentLastBook || 'the current book';
    ['bookmarks-book-name', 'goto-percent-book-name'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.textContent = label;
    });
}

function resetReaderProgress() {
    if (!confirm('Reset saved reading progress? This will not delete any books.')) return;

    const statusDiv = document.getElementById('reader-progress-reset-status');
    fetch('/api/reader/progress', { method: 'DELETE' })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = 'Reading progress reset.';
                statusDiv.style.color = 'green';
                getReaderProgress();
                setTimeout(() => statusDiv.textContent = '', 3000);
            } else {
                statusDiv.textContent = 'Error resetting progress.';
                statusDiv.style.color = 'red';
            }
        })
        .catch(error => {
            console.error('Error resetting reader progress:', error);
            statusDiv.textContent = 'Connection error.';
            statusDiv.style.color = 'red';
        });
}

// === Bookmarks and "go to %" (v1.14.0) ===
// Both act on currentLastBook (see getReaderProgress). Applying a jump — via
// a bookmark or a percent — writes into the reader's saved position, which
// only takes effect the next time that book is opened on the device (WiFi is
// off while the reader is actually running, same as library reorder and
// progress import already work this way).

function loadBookmarks() {
    const list = document.getElementById('bookmarks-list');
    const emptyHint = document.getElementById('bookmarks-empty-hint');
    if (!list) return;

    if (!currentLastBook) {
        list.innerHTML = '';
        if (emptyHint) emptyHint.classList.remove('hidden');
        return;
    }
    if (emptyHint) emptyHint.classList.add('hidden');

    fetch('/api/bookmarks?book=' + encodeURIComponent(currentLastBook))
        .then(response => response.json())
        .then(data => renderBookmarks(data.bookmarks || []))
        .catch(error => {
            console.error('Error loading bookmarks:', error);
            list.innerHTML = '<p class="error">Error loading bookmarks.</p>';
        });
}

function renderBookmarks(bookmarks) {
    const list = document.getElementById('bookmarks-list');
    if (!list) return;

    if (!bookmarks.length) {
        list.innerHTML = '<p class="hint">No bookmarks yet.</p>';
        return;
    }
    list.innerHTML = bookmarks.map(b => {
        const label = b.label && b.label.length ? b.label : `Page ${b.page}`;
        return `
        <div class="book-item">
            <span class="book-title">${escapeHtml(label)}</span>
            <span class="book-size">page ${b.page}</span>
            <button class="btn-order" data-action="jump" data-seq="${b.seq}" title="Apply next time this book opens on the device">Jump</button>
            <button class="btn-delete" data-action="remove" data-seq="${b.seq}">Delete</button>
        </div>`;
    }).join('');
    bindBookmarkListActions();
}

let bookmarkListBound = false;
function bindBookmarkListActions() {
    if (bookmarkListBound) return;
    const list = document.getElementById('bookmarks-list');
    if (!list) return;
    list.addEventListener('click', e => {
        const btn = e.target.closest('button[data-action]');
        if (!btn) return;
        const seq = Number(btn.dataset.seq);
        if (btn.dataset.action === 'jump') jumpBookmark(seq);
        else if (btn.dataset.action === 'remove') removeBookmark(seq);
    });
    bookmarkListBound = true;
}

function addBookmark() {
    const status = document.getElementById('bookmarks-status');
    if (!currentLastBook) {
        status.textContent = 'Open a book on the device first.';
        status.style.color = 'red';
        return;
    }
    const labelInput = document.getElementById('bookmark-label');
    const label = labelInput ? labelInput.value.trim() : '';

    fetch('/api/bookmarks/add', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ book: currentLastBook, label })
    })
        .then(response => response.json().then(data => ({ ok: response.ok, data })))
        .then(({ ok, data }) => {
            if (!ok) throw new Error(data.error || 'Request failed');
            if (labelInput) labelInput.value = '';
            status.textContent = 'Bookmark added.';
            status.style.color = 'green';
            setTimeout(() => status.textContent = '', 3000);
            loadBookmarks();
        })
        .catch(error => {
            console.error('Error adding bookmark:', error);
            status.textContent = 'Could not add bookmark: ' + error.message;
            status.style.color = 'red';
        });
}

function removeBookmark(seq) {
    const status = document.getElementById('bookmarks-status');
    fetch('/api/bookmarks/remove', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ book: currentLastBook, seq })
    })
        .then(response => {
            if (!response.ok) throw new Error('Request failed');
            loadBookmarks();
        })
        .catch(error => {
            console.error('Error removing bookmark:', error);
            status.textContent = 'Could not remove bookmark.';
            status.style.color = 'red';
        });
}

function jumpBookmark(seq) {
    const status = document.getElementById('bookmarks-status');
    fetch('/api/bookmarks/jump', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ book: currentLastBook, seq })
    })
        .then(response => {
            if (!response.ok) throw new Error('Request failed');
            status.textContent = 'Will resume there next time this book opens on the device.';
            status.style.color = 'green';
            setTimeout(() => status.textContent = '', 5000);
        })
        .catch(error => {
            console.error('Error jumping to bookmark:', error);
            status.textContent = 'Could not jump to bookmark.';
            status.style.color = 'red';
        });
}

function goToPercent() {
    const status = document.getElementById('goto-percent-status');
    if (!currentLastBook) {
        status.textContent = 'Open a book on the device first.';
        status.style.color = 'red';
        return;
    }
    const input = document.getElementById('goto-percent');
    const percent = Math.max(0, Math.min(100, Number(input.value)));
    input.value = percent;

    fetch('/api/reader/goto', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ book: currentLastBook, percent })
    })
        .then(response => {
            if (!response.ok) throw new Error('Request failed');
            status.textContent = `Will jump to ~${percent}% next time this book opens on the device.`;
            status.style.color = 'green';
            setTimeout(() => status.textContent = '', 5000);
        })
        .catch(error => {
            console.error('Error setting go-to-percent:', error);
            status.textContent = 'Could not set the jump.';
            status.style.color = 'red';
        });
}

// === Library State (v1.8.0) ===
// Export/import of reading progress + book metadata + manual order.
function exportLibraryState() {
    const status = document.getElementById('library-state-status');
    status.style.color = '';
    status.textContent = 'Preparing export...';

    fetch('/api/library/export')
        .then(response => {
            if (!response.ok) throw new Error('HTTP ' + response.status);
            return response.blob();
        })
        .then(blob => {
            // The device has no RTC, so the date in the filename comes from the
            // browser.
            const stamp = new Date().toISOString().slice(0, 10);
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `book32-state-${stamp}.json`;
            document.body.appendChild(a);
            a.click();
            a.remove();
            URL.revokeObjectURL(url);
            status.textContent = 'State exported.';
            status.style.color = 'green';
            setTimeout(() => status.textContent = '', 4000);
        })
        .catch(error => {
            console.error('Export failed', error);
            status.textContent = 'Export failed.';
            status.style.color = 'red';
        });
}

function importLibraryState(input) {
    const file = input.files && input.files[0];
    if (!file) return;
    input.value = '';  // allow re-picking the same file after a failure

    const status = document.getElementById('library-state-status');
    status.style.color = '';

    // The device caps the bundle at 64 KB; fail here rather than after the
    // upload. The server remains the authority.
    if (file.size > 64 * 1024) {
        status.textContent = 'File too large (limit 64 KB).';
        status.style.color = 'red';
        return;
    }
    if (!confirm('Import reading state? For each book the furthest-ahead page wins.')) return;

    status.textContent = 'Importing...';

    const form = new FormData();
    form.append('state', file, file.name);

    fetch('/api/library/import', { method: 'POST', body: form, credentials: 'include' })
        .then(response => response.json().then(body => ({ ok: response.ok, body })))
        .then(({ ok, body }) => {
            if (!ok || body.status !== 'ok') {
                status.textContent = 'Import failed: ' + (body.message || 'unknown error');
                status.style.color = 'red';
                return;
            }
            let msg = `${body.merged} updated, ${body.added} added, ${body.skipped} already ahead`;
            if (body.pending > 0) {
                msg += `. ${body.pending} waiting for the .epub to be uploaded`;
            }
            status.textContent = msg + '.';
            status.style.color = 'green';
            getReaderProgress();
            fetchBooks();
        })
        .catch(error => {
            console.error('Import failed', error);
            status.textContent = 'Connection error.';
            status.style.color = 'red';
        });
}

// === Sleep Settings ===
function getSleepSettings() {
    fetch('/api/settings/sleep')
        .then(response => response.json())
        .then(data => {
            if (data.sleepTimeout !== undefined) {
                document.getElementById('sleep-timeout').value = data.sleepTimeout;
            }
            if (data.sleepMessage !== undefined) {
                document.getElementById('sleep-message').value = data.sleepMessage;
            }
        })
        .catch(error => console.error('Error loading sleep settings:', error));
}

function saveSleepSettings() {
    const sleepTimeout = parseInt(document.getElementById('sleep-timeout').value);
    const sleepMessage = document.getElementById('sleep-message').value;
    const statusDiv = document.getElementById('sleep-settings-status');

    fetch('/api/settings/sleep', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ sleepTimeout: sleepTimeout, sleepMessage: sleepMessage }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving sleep settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

// === Display Orientation ===
function getDisplaySettings() {
    fetch('/api/settings/display')
        .then(response => response.json())
        .then(data => {
            if (data.rotation !== undefined) {
                document.getElementById('display-rotation').value = data.rotation;
            }
        })
        .catch(error => console.error('Error loading display settings:', error));
}

function saveDisplaySettings() {
    const rotation = parseInt(document.getElementById('display-rotation').value);
    const statusDiv = document.getElementById('display-settings-status');

    fetch('/api/settings/display', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ rotation: rotation }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Orientation applied.";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error applying orientation.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving display settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

// === Wi-Fi / Hotspot ===
function getWifiStatus() {
    fetch('/api/wifi/status')
        .then(response => response.json())
        .then(data => {
            const el = document.getElementById('wifi-status');
            if (!el) return;
            if (data.sta_connected) {
                el.textContent = `Connected to "${data.sta_ssid}" (${data.sta_ip}), signal ${data.rssi} dBm.`;
            } else if (data.ap_active) {
                el.textContent = `Hotspot mode — network "${data.ap_ssid}" at ${data.ap_ip}. Join a Wi-Fi network below to get online.`;
            } else {
                el.textContent = 'Not connected.';
            }
        })
        .catch(error => console.error('Error loading Wi-Fi status:', error));
}

function scanWifi() {
    const sel = document.getElementById('wifi-ssid');
    const status = document.getElementById('wifi-connect-status');
    status.style.color = 'var(--accent)';
    status.textContent = 'Scanning…';

    let tries = 0;
    const poll = () => {
        fetch('/api/wifi/scan')
            .then(response => response.status === 202 ? null : response.json())
            .then(data => {
                if (!data) {
                    if (tries++ < 10) { setTimeout(poll, 1000); return; }
                    status.textContent = 'Scan timed out. Try again.';
                    status.style.color = 'var(--danger)';
                    return;
                }
                const nets = (data.networks || []).filter(n => n.ssid);
                if (nets.length === 0) {
                    status.textContent = 'No networks found.';
                    status.style.color = 'var(--text-secondary)';
                    return;
                }
                // O SSID vai para dentro de um atributo, por isso passa por
                // escapeAttr: escapeHtml deixa passar aspas, e um SSID é
                // texto que qualquer aparelho ao alcance pode escolher —
                // bastava chamar-lhe `"><img onerror=...>` para injectar
                // markup nesta página.
                sel.innerHTML = nets.map(n =>
                    `<option value="${escapeAttr(n.ssid)}">${escapeHtml(n.ssid)} (${n.rssi} dBm)${n.secure ? ' 🔒' : ''}</option>`
                ).join('');
                status.textContent = `Found ${nets.length} network(s).`;
                status.style.color = 'var(--success)';
            })
            .catch(error => {
                console.error('Wi-Fi scan failed:', error);
                status.textContent = 'Scan error.';
                status.style.color = 'var(--danger)';
            });
    };
    poll();
}

function connectWifi() {
    const ssid = document.getElementById('wifi-ssid').value;
    const password = document.getElementById('wifi-pass').value;
    const status = document.getElementById('wifi-connect-status');

    if (!ssid) {
        status.textContent = 'Select a network first (tap Scan).';
        status.style.color = 'var(--danger)';
        return;
    }

    status.textContent = `Connecting to "${ssid}"…`;
    status.style.color = 'var(--accent)';

    fetch('/api/wifi/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: password })
    })
        .then(response => response.json())
        .then(() => {
            let tries = 0;
            const poll = () => fetch('/api/wifi/status')
                .then(response => response.json())
                .then(data => {
                    if (data.sta_connected) {
                        status.textContent = `Connected! Book32 is online at ${data.sta_ip}. You can rejoin your home Wi-Fi on your phone.`;
                        status.style.color = 'var(--success)';
                        getWifiStatus();
                    } else if (tries++ < 15) {
                        setTimeout(poll, 1000);
                    } else {
                        status.textContent = 'Could not connect — check the password and try again.';
                        status.style.color = 'var(--danger)';
                    }
                })
                .catch(() => { if (tries++ < 15) setTimeout(poll, 1000); });
            poll();
        })
        .catch(error => {
            console.error('Wi-Fi connect failed:', error);
            status.textContent = 'Connection request failed.';
            status.style.color = 'var(--danger)';
        });
}
