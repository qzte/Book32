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
    // CRITICAL: this must stay true. ESP Web Tools' actual behaviour is the
    // opposite of what the field name suggests - when this is false (or
    // absent), it erases the ENTIRE flash chip automatically, with no
    // prompt and no way to opt out (see esphome/esp-web-tools
    // src/install-dialog.ts: "Default is to erase a device that does not
    // support Improv Serial"). Only `true` shows the user a confirmation
    // dialog where they can decline the erase. A `false` here silently
    // wiped a real device's bootloader, WiFi settings and ebook library on
    // 2026-08-25 before this was caught - do not change it back.
    new_install_prompt_erase: true,
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
