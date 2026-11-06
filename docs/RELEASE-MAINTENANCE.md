# Release and deployment maintenance

`main` is the development and default branch.
The public installer is https://em3ritus.github.io/umbravox/, deployed to GitHub Pages by the **Release** workflow
(`.github/workflows/release.yml`); there is no `gh-pages` branch. Source lives in `web/native/`.

## Making a release

Run it from `main` on a commit you have tested on hardware: Actions > Release > Run workflow, and enter the tag
(for example `v1.1.0`). It runs the host and installer tests, builds the four installable boards
(`nm-cyd-c5-engine`, `ws-c5-35-engine`, `ws-c5-28-engine`, `xiao-c5-node`), packages each with
`tools/package-firmware.py` (images, `manifest.json`, `SHA256SUMS.txt`, source snapshot), assembles the installer site
from those ZIPs with `tools/assemble-native-installer.py` (which re-verifies every checksum and size limit), creates the
GitHub release holding the four ZIPs (named `umbravox-<board>.zip`, so `releases/latest/download/...` links never
change), then deploys the site. A successful run is not hardware acceptance. By hand, the same steps work locally:
`pio run -e <board>`, `package-firmware.py`, then `assemble-native-installer.py OUTPUT NM_ZIP WS35_ZIP WS28_ZIP XIAO_ZIP`
into a fresh empty directory, and check `SITE-SHA256SUMS.txt`.

## Flashing tool versions

The standalone C5 CLI is pinned in `tools/requirements-flash.txt`. See
[flashing tools](FLASHING-TOOLS.md) before building installation instructions.
Keep historical package source and firmware hashes intact when updating guidance.

## Preserve the working installer

Documentation and CI updates do not redeploy the installer or rebuild published firmware: only the Release workflow
deploys Pages. Never substitute the legacy `web/dist` output for the native site (the manual legacy preview workflow only
uploads an Actions artifact). CI builds the native engine targets as well as legacy CYD, node and coordinator targets,
and tests the native installer and the packager separately.

## Release evidence

A release tag identifies its source revision. Each firmware ZIP identifies its own
build revision and includes matching source, checksums and all four installation
images. Existing binaries may be reused unchanged in a combined release; state
those revisions explicitly rather than implying they were rebuilt from its tag.
Waveshare 2.8-inch, Waveshare 3.5-inch and XIAO ESP32-C5 browser installation
and boot have been confirmed. NM-CYD-C5 flashing and operation are confirmed;
the installation method and image-verification evidence were not reported.
See [node validation](XIAO-NODE-VALIDATION.md) for the two-node recovery and
stationary GPS recording checks; a moving field run remains open.

Before publication, verify project identity and destination for both Git and API
writes, run the installed privacy checks, review the final text and images, and
verify asset hashes. Local hooks are clone-specific and do not protect API writes.
Do not publish private guard configuration or captured field data.

## Historical references

All board history lives in `main`. The former `ws-c5-28`, `ws-c5-35`, and
`ui-input-presentation` branches were removed on 2026-11-02 after verifying every
commit is reachable from `main` (recovery SHAs 3548d43, 0e9266a, 8d7e455).
`gh-pages` contains deployment output only.
Earlier prereleases remain available for provenance and recovery. Their historical
board support and validation notes apply to those artifacts, not the current set.
The `f963f3a` prerelease is slickrick773's, on his repository; it is not part of this repository's releases.
Design journals and implementation plans
record past decisions and are not the current product specification.
