# Flashing tools

Use **esptool 5.6.0** for command-line C5 flashing. This is the current PyPI
release checked on 2026-11-03 and the version used for the XIAO node package
and Waveshare 3.5-inch bench installation. Install the pinned tool in a virtual
environment:

```sh
python3 -m venv .venv-flash
# macOS/Linux:
source .venv-flash/bin/activate
# Windows PowerShell: .venv-flash/Scripts/Activate.ps1
python -m pip install -r tools/requirements-flash.txt
python -m esptool version
```

Use each board package's offsets, images and flash capacity. The tested native
USB procedure uses `--no-stub --baud 460800`; WCH/UART bridge boards may need
115200. Do not substitute handheld images for node images. Follow
[XIAO node installation](XIAO-NODE.md) or the README inside the handheld ZIP.

The pinned PlatformIO platform supplies its own build-time esptool. Installing
the CLI above does not replace that package. Use Python 3.12 or 3.13 for this
project's PlatformIO build; the pinned platform rejects Python 3.14.
For physical flashing, use the standalone CLI above rather than assuming
`pio run -t upload` selects the same esptool version.

The browser installer uses **esptool-js 0.7.0** plus the project's tested C5 ROM
adapter. Python esptool and esptool-js have independent version numbers and
release cycles. Browser changes require installer tests and hardware validation;
a Python CLI update does not update the browser vendor bundle.

Historical bench notes describe the tools and workarounds used at their recorded
revision. They are evidence of those sessions, not the current installation guide.

Source: https://pypi.org/project/esptool/5.6.0/
