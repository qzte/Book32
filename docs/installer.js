// The manifest is built at runtime from latest.json instead of a committed
// manifest-vX.json file, so this script never needs editing when a new
// version ships — only docs/latest.json and docs/firmware/*-vX.bin change,
// and both are written by .github/workflows/release.yml on every tag.
//
// Firmware and filesystem paths are versioned (firmware-vX.bin) rather than
// reused (firmware.bin) so a browser or CDN cache can never serve a stale
// binary under a name that still matches the current manifest.
const FIRMWARE_OFFSET = 65536;   // app0, see partitions_16MB.csv
const LITTLEFS_OFFSET = 5308416; // spiffs, see partitions_16MB.csv

const versionBadge = document.querySelector('[data-version-badge]');
const installButton = document.querySelector('esp-web-install-button');
const errorBox = document.querySelector('[data-installer-error]');

function showError(message) {
  if (errorBox) {
    errorBox.textContent = message;
    errorBox.hidden = false;
  }
}

async function init() {
  let info;
  try {
    const res = await fetch('latest.json', { cache: 'no-store' });
    if (!res.ok) throw new Error(`latest.json HTTP ${res.status}`);
    info = await res.json();
    if (!info || typeof info.version !== 'string' || !info.version) {
      throw new Error('latest.json missing a valid "version" field');
    }
  } catch (err) {
    showError('Could not load the latest Book32 version. Reload the page or try again later.');
    return;
  }

  const version = info.version;
  if (versionBadge) {
    versionBadge.textContent = `Version ${version}`;
    versionBadge.hidden = false;
  }

  const manifest = {
    name: 'Book32',
    version,
    new_install_prompt_erase: false,
    builds: [
      {
        chipFamily: 'ESP32-S3',
        improv: false,
        parts: [
          { path: new URL(`firmware/firmware-v${version}.bin`, document.baseURI).href, offset: FIRMWARE_OFFSET },
          { path: new URL(`firmware/littlefs-v${version}.bin`, document.baseURI).href, offset: LITTLEFS_OFFSET },
        ],
      },
    ],
  };

  const manifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));

  if (installButton) {
    installButton.setAttribute('manifest', manifestUrl);
    installButton.manifest = manifestUrl;
    installButton.hidden = false;
  }
}

init();
