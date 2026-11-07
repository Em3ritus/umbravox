# Release and deployment maintenance

The fork's current development and default branch is `main`.
The public installer is https://slickrick773.github.io/umbravox/ and is served
from the root of `gh-pages`. Source lives in `web/native/`.

## Flashing tool versions

The standalone C5 CLI is pinned in `tools/requirements-flash.txt`. See
[flashing tools](FLASHING-TOOLS.md) before building installation instructions.
Keep historical package source and firmware hashes intact when updating guidance.

## Preserve the working installer

Documentation and CI updates do not require redeploying the installer or rebuilding
published firmware. For an intentional installer update, run all native installer
tests, assemble into a fresh empty directory with
`tools/assemble-native-installer.py OUTPUT NM_ZIP WS35_ZIP WS28_ZIP XIAO_ZIP`, and verify
`SITE-SHA256SUMS.txt`. Include the reviewed XIAO node ZIP as the fourth package. Use the reviewed board ZIPs; assembly does not compile firmware.
Review the site diff, manifests, board order, revision identifiers and checksums
before updating `gh-pages`. Verify the served revision after Pages finishes.
Never substitute the legacy `web/dist` output for this native site.

The manual legacy web preview workflow only uploads an Actions artifact. It has
no Pages deployment job. CI builds the three native engine targets as well as
legacy CYD, node and coordinator targets, and tests the native installer separately.
A successful build is not hardware acceptance.

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
Use the combined `f963f3a` prerelease for the current published package set.
Design journals and implementation plans
record past decisions and are not the current product specification.
