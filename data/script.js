function showTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-links li').forEach(el => el.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');
    const navItems = ['dashboard', 'ereader', 'todo', 'settings'];
    document.querySelectorAll('.nav-links li')[navItems.indexOf(tabId)].classList.add('active');

    // Load data when switching tabs
    if (tabId === 'ereader') {
        fetchBooks();
        switchDeviceApp('Reader');
    } else if (tabId === 'todo') {
        fetchTodos();
        switchDeviceApp('Todo');
    }
}

// Switch the app on the device
async function switchDeviceApp(appName) {
    try {
        await fetch('/api/app/switch?name=' + encodeURIComponent(appName));
    } catch (e) {
        console.log("Could not switch device app:", e);
    }
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

        if (data.time) {
            document.getElementById('time-val').innerText = data.time;
        }

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

            msg.innerHTML = `<strong>New version available: ${data.latest}</strong>`;
            if (updateParts.length > 0) {
                msg.innerHTML += `<br><small>Includes: ${updateParts.join(" and ")}</small>`;
            }
            if (data.release_notes) {
                msg.innerHTML += `<br><small>${data.release_notes}</small>`;
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
async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    bookList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/books');
        const data = await res.json();

        if (data.books && data.books.length > 0) {
            bookList.innerHTML = data.books.map(book => {
                const isFont = book.filename.endsWith('.ttf');
                return `
                <div class="book-item">
                    <span class="book-title">${isFont ? '📂 [Font] ' : '📖 '}${book.name}</span>
                    <span class="book-size">${Math.round(book.size / 1024)} KB</span>
                    <button class="btn-delete" onclick="deleteBook('${book.filename}', '${book.name}')">Delete</button>
                </div>
            `}).join('');
        } else {
            bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        }
    } catch (e) {
        bookList.innerHTML = '<p class="error">Error loading books.</p>';
        console.error("Failed to fetch books", e);
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
    if (!file.name.endsWith('.epub') && !file.name.endsWith('.ttf')) {
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

function getReaderSettings() {
    fetch('/api/settings/reader')
        .then(response => response.json())
        .then(data => {
            if (data.refreshFrequency) {
                document.getElementById('refresh-rate').value = data.refreshFrequency;
            }
        })
        .catch(error => console.error('Error loading reader settings:', error));
}

function saveReaderSettings() {
    const refreshRate = parseInt(document.getElementById('refresh-rate').value);
    const statusDiv = document.getElementById('reader-settings-status');

    fetch('/api/settings/reader', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ refreshFrequency: refreshRate }),
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

// === Todo Management ===
async function fetchTodos() {
    const todoList = document.getElementById('todo-list');
    todoList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/todos');
        const data = await res.json();

        if (data.todos && data.todos.length > 0) {
            todoList.innerHTML = data.todos.map(todo => `
                <div class="todo-item ${todo.completed ? 'completed' : ''}" data-id="${todo.id}">
                    <input type="checkbox" class="todo-checkbox" ${todo.completed ? 'checked' : ''} onchange="toggleTodo(${todo.id})">
                    <span class="todo-text" ondblclick="startEditTodo(${todo.id}, this)">${escapeHtml(todo.text)}</span>
                    <div class="todo-actions">
                        <button class="btn-edit" onclick="startEditTodo(${todo.id}, this.parentElement.previousElementSibling)">Edit</button>
                        <button class="btn-delete" onclick="deleteTodo(${todo.id})">Delete</button>
                    </div>
                </div>
            `).join('');
        } else {
            todoList.innerHTML = '<p class="hint">No tasks yet. Add one above!</p>';
        }
    } catch (e) {
        todoList.innerHTML = '<p class="error">Error loading tasks.</p>';
        console.error("Failed to fetch todos", e);
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

async function addTodo() {
    const input = document.getElementById('new-todo-input');
    const text = input.value.trim();

    if (!text) {
        input.focus();
        return;
    }

    try {
        const res = await fetch('/api/todos/add', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ text: text })
        });

        if (res.ok) {
            input.value = '';
            fetchTodos();
        } else {
            alert("Failed to add task.");
        }
    } catch (e) {
        alert("Error adding task.");
        console.error("Add todo failed", e);
    }
}

async function toggleTodo(id) {
    try {
        const res = await fetch('/api/todos/toggle', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: id })
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to toggle task.");
        }
    } catch (e) {
        alert("Error toggling task.");
        console.error("Toggle todo failed", e);
    }
}

function startEditTodo(id, spanElement) {
    const currentText = spanElement.textContent;
    const input = document.createElement('input');
    input.type = 'text';
    input.value = currentText;
    input.className = 'todo-edit-input';

    const saveEdit = async () => {
        const newText = input.value.trim();
        if (newText && newText !== currentText) {
            await editTodo(id, newText);
        } else {
            fetchTodos(); // Restore original if cancelled
        }
    };

    input.onblur = saveEdit;
    input.onkeydown = (e) => {
        if (e.key === 'Enter') {
            input.blur();
        } else if (e.key === 'Escape') {
            input.value = currentText;
            input.blur();
        }
    };

    spanElement.replaceWith(input);
    input.focus();
    input.select();
}

async function editTodo(id, text) {
    try {
        const res = await fetch('/api/todos/edit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id: id, text: text })
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to edit task.");
            fetchTodos();
        }
    } catch (e) {
        alert("Error editing task.");
        console.error("Edit todo failed", e);
        fetchTodos();
    }
}

async function deleteTodo(id) {
    if (!confirm("Delete this task?")) return;

    try {
        const res = await fetch('/api/todos/delete?id=' + encodeURIComponent(id), {
            method: 'DELETE'
        });

        if (res.ok) {
            fetchTodos();
        } else {
            alert("Failed to delete task.");
        }
    } catch (e) {
        alert("Error deleting task.");
        console.error("Delete todo failed", e);
    }
}

// Handle Enter key in todo input
document.addEventListener('DOMContentLoaded', () => {
    const todoInput = document.getElementById('new-todo-input');
    if (todoInput) {
        todoInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                addTodo();
            }
        });
    }
});
