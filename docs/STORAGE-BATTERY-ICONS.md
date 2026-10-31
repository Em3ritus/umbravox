# Native header storage and battery indicators

Icon-only SD is immediately left of battery. At 480: SD x376/w48, battery x424/w56; at 320: Bridge x196/w52, SD x248/w28, battery x276/w44. Header height remains 44. SD is read-only, with no touch target; battery retains its existing power-detail target. All other controls/order stay unchanged.

- Green SD card: successfully mounted, no captured writer failure this boot.
- Dim crossed SD card: no successful mount.
- Amber SD card with exclamation mark: a capture/export writer reported a failure. Latched until reboot; a later successful write does not erase the warning. Includes WiGLE error events, GPX/client/probe CSV errors, POI writes, PCAP open/write/damaged-file errors. This is not a full filesystem health check or a diagnostic-log write monitor.
- Battery + percentage: only with PMU available, cell present and percentage 0–100. Charging uses green and a plus sign; low battery (20% or less) uses amber.
- Crossed battery: PMU reports no cell. No fabricated percentage.
- Battery with question mark: sensing unavailable. Present cell with unavailable percentage shows `?` instead of a number.

Mount state is the existing cached `sdlog::present()` result. There is no added polling or bus traffic. Physical removal of an idle mounted card cannot be detected immediately by this indicator; a later failed write raises the amber warning. Reboot with a card fitted remounts it and clears the warning. The icon does not claim data was saved—REC status and file verification retain that role.

State flows through every native scene and its header dirty comparison, so changes repaint the header without repainting the body. Legacy UI is unchanged. Host tests check mount/error projection, ready/missing/error region repaint equality, PMU/cell/percentage/charging changes, and read-only SD geometry at both sizes. The independent hit contract is updated for the narrower Bridge and moved battery targets. Hardware startup can verify the mounted/no-cell case; other states are host-tested unless separately exercised on hardware.

Artwork spacing: the unchanged 14 px SD and 24 px battery form a centered 46 px group with an 8 px gap. Percentage aligns with the battery artwork. Wide battery hit remains 56×44, compact remains 44×44; SD artwork stays outside that hit.
