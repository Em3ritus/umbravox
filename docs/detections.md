# What UMBRAVOX detects

UMBRAVOX is a **passive, receive-only** scanner. It never transmits, probes, or
connects — it only listens to the WiFi and BLE a device already broadcasts and
decides whether that radio is worth naming.

## How confidence works

Every signature carries a **confidence**, and confidence is per *signature*, not
per device type — the same threat can be HIGH from one piece of evidence and LOW
from another:

| Confidence | Meaning | Alerts? |
|-----------|---------|---------|
| **HIGH** | Evidence specific to the product (a registered vendor block, a vendor service UUID, a product name) | Yes |
| **MED**  | Strong but shareable evidence (a setup SSID, a standards UUID that says "a tracker" but not which) | Yes |
| **LOW**  | A shared identifier — a chip vendor's OUI that every dev board on earth also uses | Logged, quiet |

The **ATTUNE** screen sets an **alert floor**. At the default floor you are
alerted on everything **HIGH** and **MED**; **LOW** matches are recorded as weak
evidence but stay quiet unless you lower the floor. A LOW match is a lead, not a
verdict — matching a shared chip prefix and shouting "camera!" just trains you to
ignore the alert.

> The exact OUI / UUID / company-id / name tables are the source of truth in
> [`ui/signatures.cpp`](../ui/signatures.cpp); this page is the human summary.

## Evidence vectors

A hit can come from any of: a **WiFi OUI** (first 3 bytes of a BSSID), a **WiFi
SSID** prefix, a **BLE 16-bit service UUID**, a **BLE SIG company id**, or a
**BLE advertised name** (matched case-insensitively as a substring, for devices
that hide behind a locally-administered MAC).

---

## Surveillance & law enforcement

### ALPR — automated licence-plate readers
| Vendor | Evidence | Confidence |
|--------|----------|-----------|
| Motorola / Vigilant | 5 WiFi OUIs | **HIGH** |
| Genetec | 2 WiFi OUIs | **HIGH** |

### BODYCAM — body cameras & LE equipment
| Vendor | Evidence | Confidence |
|--------|----------|-----------|
| Axon | SSIDs `AB2-`/`AB3-`/`AB4-`/`AXON-`, OUI `00:25:DF`, BLE UUIDs `0xFC81/0xFE6B/0xFE6C`, company `0x034D` | **HIGH** |
| Axon (shared blocks) | OUIs `E4:05:40`, `28:24:FF` | LOW |
| Motorola / WatchGuard | OUI `00:1D:96` | **MED** |
| Digital Ally | OUI `00:23:BD` | **MED** |

### FLOCK — Flock Safety cameras & sensors
| Evidence | Confidence |
|----------|-----------|
| Flock's own registered block `B4:1E:52` | **HIGH** |
| BLE names `Penguin`, `Pigvision`, `FS Ext Battery` | **HIGH** |
| Setup SSID `flock-` | **MED** |
| BLE company `0x09C8` (Xuntong) | **MED** |
| BLE name `Flock` (could be coincidental) | **MED** |
| ~40 field-sourced OUIs (Espressif / Silabs / OEM blocks) | LOW |

### RAVEN — gunshot detectors
| Vendor | Evidence | Confidence |
|--------|----------|-----------|
| ShotSpotter / SoundThinking | OUI `D4:11:D6` | **HIGH** |
| (generic) | BLE UUIDs `0x3100`–`0x3500` | **MED** |

---

## Cameras & wearables

### CAMERA — fixed / consumer cameras
| Vendor | Confidence |
|--------|-----------|
| Ring (15 OUIs) | **HIGH** (two MED) |
| Hikvision, Axis, Verkada, Avigilon, i-PRO (Panasonic) | **HIGH** |
| Wyze | **HIGH** (one MOD at LOW) |
| Amazon, Blink | **MED** |
| Arlo, Tuya, Realtek | LOW |

### GLASSES — camera glasses
| Vendor | Evidence | Confidence |
|--------|----------|-----------|
| Ray-Ban Meta | BLE UUID `0xFD5F` | **HIGH** |
| Snap Spectacles | BLE UUID `0xFE45` | **HIGH** |
| Meta / Luxottica / Vuzix | OUI `98:59:49`, BLE companies/UUIDs | **MED** |

---

## Trackers

### TRACKER — Find My / Bluetooth tags
| Vendor | Evidence | Confidence |
|--------|----------|-----------|
| Tile | UUIDs `0xFEED`, `0xFEEC`, `0xFD84` | **HIGH** |
| Chipolo | UUIDs `0xFE33`/`0xFE65`, company `0x08C3` | **HIGH** |
| Samsung SmartTag | UUID `0xFD5A` | **HIGH** |
| DULT "unwanted tracker" standard | UUID `0xFCB2` | **MED** |
| Find My Device network | UUID `0xFEAA` | LOW |
| Apple (AirTag et al.) | company `0x004C` — every Apple device sends it | LOW |

---

## Drones

### DRONE — Remote ID
| Source | Evidence | Confidence |
|--------|----------|-----------|
| Open Drone ID (any maker) | BLE UUID `0xFFFA` | **HIGH** |
| Skydio (US LE airframe) | OUI `38:1D:14` | **HIGH** |
| DJI | 10 OUIs | **MED** |

---

## Pentest tools & skimmers

### PENTEST
| Device | Evidence | Confidence |
|--------|----------|-----------|
| WiFi Pineapple | SSID `Pineapple_` | **HIGH** |
| Flipper Zero | BLE UUIDs `0x3081`–`0x3083`, company `0x0E29`, OUI `0C:FA:22` | **HIGH** |
| Flipper Zero (name) | BLE name `Flipper` | **MED** |
| Deauther | SSID `pwned` | **MED** |
| Hak5 (locally-admin) | OUIs `02:C0:CA`, `02:13:37` | LOW |

### SKIMMER — card skimmers
| Module | Evidence | Confidence |
|--------|----------|-----------|
| HC-05 / HC-06 serial modules | BLE name | **MED** |
| HC-03 / HC-08 / BT05 modules | BLE name | LOW |

> ⚠️ **Hardware limit:** the classic HC-05/HC-06 skimmers field research actually
> finds on gas pumps use **Classic Bluetooth (BR/EDR)**, and the ESP32-C5 radio
> is **BLE-only** — so a genuine classic-BT skimmer is *invisible* to UMBRAVOX no
> matter the signature. These names fire only on a BLE module/clone advertising
> them. The names also recur in hobby gear, so they're a warning, not proof.
> (Three legacy classic-BT skimmer OUIs sit in the WiFi table and can never
> fire — a WiFi BSSID never carries a Bluetooth MAC.)

---

## Behavioural — no signature, detected by activity

These have nothing to look up; they're a rate that's wrong or a pair of beacons
that contradict each other:

| Threat | What it is |
|--------|-----------|
| **DEAUTH** | A deauthentication / disassociation flood |
| **ROGUE** | One SSID advertised by two BSSIDs that disagree on security (evil twin) |
| **FOLLOW** | A tracker that has travelled with you across locations |

---

*Signature data derived in part from SquachWatch-CYD (GPL-3.0) and community
Flock field-research datasets; see [`NOTICE`](../NOTICE) for attribution.*
