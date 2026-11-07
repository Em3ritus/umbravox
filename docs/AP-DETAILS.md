# AP detail presentation

The native detail screen keeps the full stored printable SSID (up to 32 bytes), labels the complete Wi-Fi address BSSID, and shows the observed band, channel and RSSI. BLE records use ADDR and do not claim Wi-Fi authentication or cipher information.

AUTH and CIPHER are separate rows. AUTH summarizes captured capability bits, including WPA2/WPA3 PSK/SAE transition mode, enterprise (ENT), and OWE. CIPHER lists observed pairwise TKIP and/or CCMP. Missing cipher information is NOT REPORTED, never inferred from a WPA version. A record with no security evidence is not highlighted as OPEN.

These are advertised capability sets, not the security negotiated by a particular client. The existing two-byte capture field combines capabilities; it does not retain per-protocol suite associations, group cipher, GCMP variants or enterprise authentication details. This change adds no per-AP capture allocation and does not change WiGLE export formatting or radio behavior.

Full identity and signal history were already supplied by the capture pipeline. This increment clarifies presentation rather than claiming new capture support. Channel width, PHY, vendor lookup and other discovery fields remain later work. A future raw-SSID treatment would be needed to display arbitrary non-printable SSID bytes losslessly; the existing capture pipeline sanitizes names.

Preview fixture: a full-length printable SSID and WPA2/WPA3 transition network at 480x320 and 320x240. Existing baseline remains 3ef1536; no flash included.
