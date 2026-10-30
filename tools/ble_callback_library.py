"""Expose Arduino BLE's existing NimBLE callback-only result mode locally.

PlatformIO pre script: never modifies the installed framework. Preserve the
library and its license in the build directory, add one public setter, and link
that copy instead of the framework BLE library. Fail if expected semantics drift.
"""
from pathlib import Path

ANCHOR = "#if defined(CONFIG_NIMBLE_ENABLED)\n  void setDuplicateFilter(bool enabled);"
SETTER = """#if defined(CONFIG_NIMBLE_ENABLED)
  // Set before starting discovery. Zero releases each device after its callback.
  void setMaxResults(uint8_t count) { m_maxResults = count; }
  void setDuplicateFilter(bool enabled);"""


def patched_header(header, source):
    if header.count(ANCHOR) != 1 or "void setMaxResults(" in header:
        raise RuntimeError("BLEScan API changed: review callback-only integration")
    callback = "pScan->m_pAdvertisedDeviceCallbacks->onResult(*advertisedDevice);"
    release = "if (pScan->m_maxResults == 0 && advertisedDevice->m_callbackSent) {\n        pScan->erase(advertisedAddress);"
    start = source.find("int BLEScan::handleGAPEvent(")
    end = source.find("case BLE_GAP_EVENT_DISC_COMPLETE:", start)
    block = source[start:end] if start >= 0 and end > start else ""
    if block.count(release) != 1 or block.count(callback) != 2:
        raise RuntimeError("BLEScan callback release semantics changed")
    # Both immediate and scan-response callbacks must finish before deletion.
    if block.rfind(callback) > block.index(release):
        raise RuntimeError("BLEScan release no longer follows callback")
    return header.replace(ANCHOR, SETTER)


def patched_source(source):
    # Callback-only active scanning retains scannable advertisements until a
    # response arrives. Bound the unanswered set; blescan recycles it every 2s.
    anchor = "        advertisedDevice = new BLEAdvertisedDevice();"
    begin = source.find("int BLEScan::handleGAPEvent(")
    if begin < 0 or source[begin:].count(anchor) != 1:
        raise RuntimeError("BLE pending-result allocation changed")
    pos = source.index(anchor, begin)
    guard = """        if (pScan->m_maxResults == 0 && !pScan->m_scan_params.passive &&
            pScan->m_scanResults.m_vectorAdvertisedDevices.size() >= 16) {
          return 0; // bounded unanswered scan-response cache
        }
"""
    return source[:pos] + guard + source[pos:]


def stage_library(original, destination):
    original, destination = Path(original), Path(destination)
    header = original / "src/BLEScan.h"
    source = original / "src/BLEScan.cpp"
    patched = patched_header(header.read_text(), source.read_text())
    patched_cpp = patched_source(source.read_text())
    # Remove obsolete sources only from this owned build-local staging directory.
    if destination.exists():
        for old in destination.rglob("*"):
            if old.is_file() and not (original / old.relative_to(destination)).is_file():
                old.unlink()
    # Refresh only changed files, retaining incremental build timestamps.
    for src in original.rglob("*"):
        if not src.is_file():
            continue
        target = destination / src.relative_to(original)
        target.parent.mkdir(parents=True, exist_ok=True)
        data = patched.encode() if src == header else patched_cpp.encode() if src == source else src.read_bytes()
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    return destination / "src"


def configure(env):
    if env.subst("$PIOENV") not in ("nm-cyd-c5", "ws-c5-35", "ws-c5-35-engine"):
        return
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    build = Path(env.subst("$BUILD_DIR"))
    libraries = build / "ble-libraries"
    stage_library(framework / "libraries/BLE", libraries / "BLE")
    # Let normal dependency resolution/linking handle BLE and its IDF components.
    # This location wins over the framework's library search path.
    env.Prepend(LIBSOURCE_DIRS=[str(libraries)])


if "Import" in globals():
    Import("env")
    configure(env)
