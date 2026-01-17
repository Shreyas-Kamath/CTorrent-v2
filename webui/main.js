const fileInput = document.getElementById("torrent-file");
const addBtn = document.getElementById("add-btn");
const listContainer = document.getElementById("torrent-list");
document.getElementById("modal-overlay").onclick = closeModal;

let pendingRemoveTorrentHash = null;

function setupUI() {
    // Open file picker
    addBtn.onclick = () => fileInput.click();

    // Handle file upload
    fileInput.onchange = handleTorrentUpload;
}

async function handleTorrentUpload(e) {
    const file = e.target.files[0];
    if (!file) return;

    const form = new FormData();
    form.append("torrent", file);

    const res = await fetch("/api/torrents/add", {
        method: "POST",
        body: form
    });

    const data = await res.json();
    // console.log("Add result:", data);

    fileInput.value = "";
    loadTorrents();
}

async function loadTorrents() {
    let res = await fetch("/api/torrents");
    let torrents = await res.json();

    listContainer.innerHTML = "";

    torrents.forEach((t, i) => {
        let progress = parseFloat(t.progress)
        let size = parseInt(t.size)

        listContainer.innerHTML += `
            <div class="torrent-row">
                <div>${i + 1}</div>
                <div>${t.name}</div>
                <div>
                    <div class="progress-bar">
                        <div class="progress-fill" style="width:${progress.toFixed(2)}%"></div>
                    </div>
                    <small>${progress.toFixed(2)}%</small>
                </div>

                <div>${formatBytes(t.downloaded)}</div>
                <div>${formatBytes(t.uploaded)}</div>
                <div>${formatBytes(size)}</div>
                <div class="status-${t.status}">${t.status}</div>

                <div class="info-cell">
                    ${t.trackers}
                    <span class="info-icon" onclick="showTrackers('${t.hash}')">&#9432;</span>
                </div>

                <div class="info-cell">
                    ${t.peers}
                    <span class="info-icon" onclick="showPeers('${t.hash}')">&#9432;</span>
                </div>

                <div class="controls">
                    <button class="green-btn" onclick="startTorrent('${t.id}')">Start</button>
                    <button class="yellow-btn" onclick="stopTorrent('${t.id}')">Stop</button>
                    <button class="red-btn" onclick="openDeleteModal('${t.hash}')">Remove</button>
                </div>
            </div>
        `;
    });
}

async function startTorrent(id) {
    await fetch(`/api/torrents/${id}/start`, { method: "POST" });
    loadTorrents();
}

async function stopTorrent(id) {
    await fetch(`/api/torrents/${id}/stop`, { method: "POST" });
    loadTorrents();
}

async function confirmRemoveTorrent() {
    if (!pendingRemoveTorrentHash) return;

    const deleteFiles =
        document.getElementById("delete-files-checkbox").checked;

    await fetch(`/api/torrents/${pendingRemoveTorrentHash}/remove`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            delete_files: deleteFiles
        })
    });

    closeDeleteModal();
    loadTorrents();
}

function formatBytes(bytes) {
    if (!Number.isFinite(bytes)) return "—";
    const units = ["B", "KB", "MB", "GB", "TB"];
    let i = 0;
    while (bytes >= 1024 && i < units.length - 1) {
        bytes /= 1024;
        i++;
    }
    return `${bytes.toFixed(2)} ${units[i]}`;
}

function openModal(title) {
    document.getElementById("modal-title").textContent = title;
    document.getElementById("modal-overlay").classList.remove("hidden");
    document.getElementById("modal").classList.remove("hidden");
}

function closeModal() {
    document.getElementById("modal-overlay").classList.add("hidden");
    document.getElementById("modal").classList.add("hidden");

    stopModalPolling();
}

function openDeleteModal(hash) {
    pendingRemoveTorrentHash = hash;
    document.getElementById("delete-files-checkbox").checked = false;

    document.getElementById("delete-modal-overlay").classList.remove("hidden");
    document.getElementById("delete-modal").classList.remove("hidden");
}

function closeDeleteModal() {
    pendingRemoveTorrentHash = null;

    document.getElementById("delete-modal-overlay").classList.add("hidden");
    document.getElementById("delete-modal").classList.add("hidden");
}


function renderModalTable(headers, rows) {
    const thead = document.querySelector("#modal-table thead");
    const tbody = document.querySelector("#modal-table tbody");

    thead.innerHTML = "<tr>" + headers.map(h => `<th>${h}</th>`).join("") + "</tr>";
    tbody.innerHTML = "";

    rows.forEach(row => {
        tbody.innerHTML += "<tr>" +
            headers.map(h => `<td>${row[h] ?? "—"}</td>`).join("") +
            "</tr>";
    });
}

let modalPollTimer = null;

async function showPeers(hash) {
    openModal("Peers");

    async function loadPeers() {
        const res = await fetch(`/api/torrents/${hash}/peers`);
        const peers = await res.json();

        renderModalTable(
            ["ip", "version", "progress", "requests", "choked", "interested"],
            peers
        );
    }

    await loadPeers();
    modalPollTimer = setInterval(loadPeers, 2000);
}

async function showTrackers(hash) {
    openModal("Trackers");

    async function loadTrackers() {
        const res = await fetch(`/api/torrents/${hash}/trackers`);
        const trackers = await res.json();

        renderModalTable(
            ["url", "reachable", "status", "peers", "interval", "timer"],
            trackers
        );
    }

    await loadTrackers();
    modalPollTimer = setInterval(loadTrackers, 2000);
}

function stopModalPolling() {
    if (modalPollTimer) {
        clearInterval(modalPollTimer);
        modalPollTimer = null;
    }
}

setupUI();
loadTorrents();
setInterval(loadTorrents, 1500);