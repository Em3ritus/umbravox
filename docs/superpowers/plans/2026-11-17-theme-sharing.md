# Theme Sharing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Share button in the theme designer that files a submission in a public themes repository, automatic checks that turn it into a pull request with preview pictures, and a gallery whose themes open in the designer.

**Architecture:** Two repositories. The firmware repository (this one, branch `theme-sharing`) gains three pure designer modules, their wiring, and a Node tool that renders a pack to PNG with the real drawing engine. A new themes repository (developed locally in a sibling directory `umbravox-themes`, next to the firmware checkout; it is created on GitHub only when the owner asks) holds the submission form, intake scripts, workflows and the static gallery, and uses the firmware repository at a pinned tag for the pack checker and renderer.

**Tech Stack:** Plain ES modules, `node --test`, Node `zlib` (no npm dependencies), GitHub Actions, GitHub Pages, the existing `sim/umbravox --check-pack` and `tools/build-designer-wasm.sh`.

**Spec:** `docs/superpowers/specs/2026-11-17-theme-sharing-design.md`

## Global Constraints

- Passive only; nothing here touches the radio.
- Never push, create a repository, enable Pages, or open an issue/PR on GitHub unless the owner asks in that turn. Tasks 1-10 are local only; Task 11 is the owner-gated go-live.
- No owner real name, email or home path in any tracked file in either repository: `git grep -nE "/home/[a-z]+"` empty before any push.
- Commits: author `Em3ritus <152051018+Em3ritus@users.noreply.github.com>`, trailer `Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>`.
- Tests first; show the failing run before the code.
- Names used everywhere: themes repository `Em3ritus/umbravox-themes` (placeholder until the owner confirms), themes site `https://em3ritus.github.io/umbravox-themes/`, designer `https://em3ritus.github.io/umbravox/designer/`, form file `submit-theme.yml`, firmware pin tag `v1.2.1` or the first tag that contains Task 4.
- Pack file names are exactly `theme.json`, `emblem.bin`, `radar.vid`, `alert.snd`. Folder rule: `^[A-Za-z0-9_-]{1,31}$`.
- Submission limits: zip at most 2,097,152 bytes; at most 5 entries; unpacked total at most 2,097,152 bytes; one top-level folder.
- Limitation 13 in `docs/theme-packs.md` and `web/designer/js/limits.json` stays true: packs are checked for format only.

## Review Focus

1. A zip entry named `../x`, `/x`, `a/b/c/theme.json`, or a symlink entry: rejected, nothing written outside the temp folder (Task 6 tests).
2. A zip whose header claims a small size but inflates past the cap: rejected by the inflate limit, not after allocation (Task 6 tests).
3. An issue body with two zip links, a link to another host, or an attachment URL with extra path parts: rejected with a plain message (Task 6 tests).
4. `?theme=` with `../`, a URL, mixed case not in the index, or an index entry naming a file outside the four names: nothing fetched (Task 2 tests).
5. A theme name or folder that already exists in the gallery, compared case-insensitively: rejected (Task 7 tests).

---

## Part A: firmware repository (branch `theme-sharing`)

### Task 1: Share link builder

**Files:**
- Create: `web/designer/js/share.js`
- Test: `web/designer/test/share.test.mjs`

**Interfaces:**
- Produces: `THEMES_REPO = 'Em3ritus/umbravox-themes'`, `THEMES_SITE = 'https://em3ritus.github.io/umbravox-themes/'`, `FOLDER_RE = /^[A-Za-z0-9_-]{1,31}$/`, `shareUrl(theme, basedOn = '') -> string`.

- [ ] **Step 1: Write the failing tests**

```js
import { shareUrl, THEMES_REPO, FOLDER_RE } from '../js/share.js';
test('the share link opens the submission form with the fields filled', () => {
  const u = new URL(shareUrl({ name: 'MIDNIGHT EMBER', author: 'A & B' }, 'cosmic-two'));
  assert.equal(u.origin + u.pathname, 'https://github.com/' + THEMES_REPO + '/issues/new');
  assert.equal(u.searchParams.get('template'), 'submit-theme.yml');
  assert.equal(u.searchParams.get('title'), 'Theme: MIDNIGHT EMBER');
  assert.equal(u.searchParams.get('theme-name'), 'MIDNIGHT EMBER');
  assert.equal(u.searchParams.get('author'), 'A & B');
  assert.equal(u.searchParams.get('based-on'), 'cosmic-two');
});
test('no author and no based-on leave those fields out', () => {
  const u = new URL(shareUrl({ name: 'X', author: '' }));
  assert.equal(u.searchParams.has('author'), false);
  assert.equal(u.searchParams.has('based-on'), false);
});
test('the folder rule', () => {
  for (const ok of ['a', 'midnight-ember', 'A_1']) assert.ok(FOLDER_RE.test(ok));
  for (const bad of ['', '..', 'a/b', 'a b', 'x'.repeat(32), 'a.b']) assert.equal(FOLDER_RE.test(bad), false);
});
```

- [ ] **Step 2: Run** `node --test web/designer/test/share.test.mjs` — Expected: FAIL, cannot find `share.js`.
- [ ] **Step 3: Implement `share.js`** with `URLSearchParams`. A `basedOn` that fails `FOLDER_RE` is dropped.
- [ ] **Step 4: Run** the same command — Expected: PASS.
- [ ] **Step 5: Commit** `feat(designer): share link builder for the themes repository`.

### Task 2: Load a gallery theme by URL

**Files:**
- Create: `web/designer/js/gallery.js`
- Test: `web/designer/test/gallery.test.mjs`

**Interfaces:**
- Consumes: `THEMES_SITE`, `FOLDER_RE` (Task 1); `openPackFiles(files)` and `limits` from `model.js` / `limits.json`.
- Produces: `themeParam(search) -> string|null`; `fetchGalleryTheme(folder, fetchFn = fetch, site = THEMES_SITE) -> Promise<{ok:true, files:[{name,data:Uint8Array}], entry} | {ok:false, error}>`.
- `index.json` format (shared with Task 7): `{ "version": 1, "themes": [ { "folder", "name", "author", "basedOn", "format": 1, "files": ["theme.json", ...], "login", "issue", "added" } ] }`.

- [ ] **Step 1: Write the failing tests** (a fake `fetchFn` built from a map of URL to `{status, bytes}`):

```js
test('themeParam accepts only a folder name', () => {
  assert.equal(themeParam('?theme=midnight-ember'), 'midnight-ember');
  for (const s of ['', '?x=1', '?theme=', '?theme=../a', '?theme=https://x/y', '?theme=a/b', '?theme=a%2Fb']) assert.equal(themeParam(s), null);
});
test('a listed theme is fetched file by file from the themes site', async () => {
  const r = await fetchGalleryTheme('ember', fake, 'https://t.example/');
  assert.equal(r.ok, true);
  assert.deepEqual(r.files.map((f) => f.name), ['ember/theme.json', 'ember/emblem.bin']);
  assert.deepEqual(calls, ['https://t.example/index.json', 'https://t.example/themes/ember/theme.json', 'https://t.example/themes/ember/emblem.bin']);
});
test('a folder that is not in the index fetches nothing else', ...);          // error: 'That theme is not in the gallery.'
test('an index entry naming any other file is refused', ...);                  // files: ['theme.json','../x'] -> ok:false, only index.json fetched
test('a file over its limit is refused by Content-Length or by size read', ...); // radar.vid 800000 bytes -> ok:false
test('a failed fetch gives a plain message', ...);                             // status 404 -> error mentions 'could not be loaded'
test('the fetched files open through openPackFiles', ...);                     // openPackFiles(r.files).ok === true for a built pack
```

- [ ] **Step 2: Run** `node --test web/designer/test/gallery.test.mjs` — Expected: FAIL, missing module.
- [ ] **Step 3: Implement `gallery.js`.** `themeParam` reads only the `theme` key and tests `FOLDER_RE`. Folder lookup in the index is exact-match. Allowed names are the four pack names; per-file size caps are `limits.MAX_JSON`, `MAX_EMBLEM`, `MAX_CLIP`, `MAX_SOUND`.
- [ ] **Step 4: Run** — Expected: PASS.
- [ ] **Step 5: Commit** `feat(designer): fetch a gallery theme by folder name`.

### Task 3: Wire the Share button and `?theme=` into the designer

**Files:**
- Modify: `web/designer/js/app.js` (the `btn-share` handler near line 477; `refreshChecks` near line 302; `boot` near line 697; `state`), `web/designer/index.html` (button title, a "Browse shared themes" link on the Start view to `THEMES_SITE`), `web/designer/js/limits.json` (one new limitation `sharing`, `where: ["design"]`), `web/designer/js/limitations.js` (`DESIGN_PLACES.sharing`), `tools/assemble-designer.sh` only if it lists js files one by one (it copies `js/*`, so no change expected).
- Test: `web/designer/test/limitations.test.mjs`, `web/designer/test/limits.test.mjs` (existing count assertions change by one), new `web/designer/test/share-wiring.test.mjs`.

**Interfaces:**
- Consumes: `shareUrl`, `THEMES_SITE` (Task 1); `themeParam`, `fetchGalleryTheme` (Task 2); existing `download()`, `currentPack()`, `begin()`, `openPackFiles`.
- Produces: `state.basedOn` (string, set when a gallery theme is opened, cleared by a new blank theme or template).

- [ ] **Step 1: Write the failing tests.** `share-wiring.test.mjs` reads `app.js` and `index.html` as text (the pattern used by `web/native/test/page.test.mjs`):

```js
test('the Share button no longer says sharing arrives later', () => assert.equal(/Sharing arrives in a later release/.test(read('web/designer/js/app.js')), false));
test('app.js uses the share link and the gallery loader', () => { const s = read('web/designer/js/app.js'); assert.match(s, /from '\.\/share\.js'/); assert.match(s, /from '\.\/gallery\.js'/); assert.match(s, /themeParam\(location\.search\)/); });
test('the Start view links to the gallery', () => assert.match(read('web/designer/index.html'), /umbravox-themes/));
```
Plus: the limits tests expect the new `sharing` entry with text "Sharing needs a GitHub account. Shared themes are public, and you must have the right to share any picture, video or sound in them."

- [ ] **Step 2: Run** `npm test` — Expected: the new tests FAIL; everything else passes.
- [ ] **Step 3: Implement.** Share click: if checks fail, do nothing (button disabled with the same title as Download); otherwise call `download()`, `window.open(shareUrl(currentTheme(), state.basedOn), '_blank', 'noopener')`, and set `action-msg` to "Saved NAME.zip and opened the submission form. Drag the zip into the form, tick the two boxes and submit." Boot: after `buildStart()`, if `themeParam(location.search)` is set, `fetchGalleryTheme`, then `openPackFiles` and `begin(...)`, set `state.basedOn`; on any failure show the error in `open-msg` and leave the Start view usable. A shared theme keeps its name; `refreshChecks` adds a warning (not an error) when `state.basedOn` is set and the name is unchanged: "Change the name before sharing: the gallery already has a theme called NAME."
- [ ] **Step 4: Run** `npm test` — Expected: PASS, all files.
- [ ] **Step 5: Commit** `feat(designer): Share opens the submission form; ?theme= opens a gallery theme`.

### Task 4: Render a pack to preview pictures (Node tool)

**Files:**
- Create: `tools/pack-preview.mjs`, `web/designer/js/png.js`
- Test: `web/designer/test/pack-preview.test.mjs`, `web/designer/test/png.test.mjs`

**Interfaces:**
- Consumes: `createPreview(M)`, `BOARDS`, `loadDraft(theme, emblem, clip)`, `renderDraft(screen, frame, {board})`; `openPackFiles`.
- Produces: `encodePng(rgba: Uint8Array, w, h, scale = 1) -> Uint8Array` (8-bit RGB, nearest-neighbour integer scale, Node `zlib.deflateSync`); CLI `node tools/pack-preview.mjs <pack-folder> --out <dir>` writing `<dir>/cyd.png` (160x120 at scale 3, 480x360) and `<dir>/waveshare.png` (480x320 at scale 1), scan screen, frame 24. Exit 0 on success; exit 1 with one line on stderr when the pack does not open or the wasm is missing. It reads only the four pack file names from the folder.

- [ ] **Step 1: Write the failing tests**

```js
test('encodePng writes a valid PNG of the scaled size', () => {
  const png = encodePng(new Uint8Array([255,0,0,255, 0,255,0,255]), 2, 1, 3);
  assert.deepEqual([...png.subarray(0, 8)], [137,80,78,71,13,10,26,10]);
  const dv = new DataView(png.buffer, png.byteOffset);
  assert.equal(dv.getUint32(16), 6); assert.equal(dv.getUint32(20), 3);       // IHDR width, height
  // inflate IDAT, check first pixel is red and pixel at x=3 is green
});
test('pack-preview writes both pictures for a template pack', () => {
  const folder = writePack(buildPack(makeTemplate(TEMPLATES[0].id).theme, makeTemplate(TEMPLATES[0].id).emblem));
  const r = spawnSync('node', ['tools/pack-preview.mjs', folder, '--out', out], { cwd: ROOT, encoding: 'utf8' });
  assert.equal(r.status, 0, r.stderr);
  // cyd.png is 480x360, waveshare.png is 480x320, both start with the PNG signature
});
test('pack-preview fails with one line for a folder with no theme.json', ...); // status 1, stderr matches /theme\.json/
test('two different templates give different cyd.png bytes', ...);
```
(Use the real shape `makeTemplate` returns; read `model.js` lines 53-80 first.)

- [ ] **Step 2: Run** `node --test web/designer/test/png.test.mjs web/designer/test/pack-preview.test.mjs` — Expected: FAIL, missing modules.
- [ ] **Step 3: Implement** `png.js` (signature, IHDR, one IDAT, IEND; CRC from `zip.js` `crc32`) and the CLI.
- [ ] **Step 4: Run** `npm test` — Expected: PASS, all files.
- [ ] **Step 5: Commit** `feat(tools): pack-preview renders a pack to PNG with the device's drawing code`.

### Task 5: Docs for the firmware repository

**Files:**
- Modify: `docs/theme-packs.md` (new section "Share a theme" after "Install by hand": the button, the form, what is checked, that the owner approves, the content rule in one paragraph, opening from the gallery; limitation list gains the sharing item and the "17" heading and count sentence are updated), `CHANGELOG.md` (Unreleased / Added), `web/designer/test/limits.test.mjs` if it asserts the doc count.

- [ ] **Step 1:** Run `npm test` and `make -C sim test` first; note any test that pins the limitation count in the docs.
- [ ] **Step 2:** Write the docs. State plainly what is not yet verified (no live submission until Task 11).
- [ ] **Step 3:** Run `npm test` — Expected: PASS.
- [ ] **Step 4: Commit** `docs: sharing a theme and opening one from the gallery`.

---

## Part B: themes repository (local directory `umbravox-themes`, `git init`, branch `main`, not pushed)

### Task 6: Intake: read the form and unpack the zip safely

**Files:**
- Create: `package.json` (`{"private": true, "type": "module", "scripts": {"test": "node --test \"test/*.test.mjs\""}}`), `scripts/intake.mjs`, `scripts/zipread.mjs`
- Test: `test/intake.test.mjs`, `test/zipread.test.mjs`

**Interfaces:**
- Produces:
  - `parseSubmission(body: string) -> {ok:true, name, author, basedOn, zipUrl} | {ok:false, error}`. The issue-form body is `### Heading\n\nvalue` blocks; an empty optional field reads `_No response_`. Headings: `Theme name`, `Author credit`, `Based on`, `Theme zip`, `Agreement`. Both checkboxes must be `- [x]` (either case). Exactly one URL matching `^https://github\.com/user-attachments/files/\d+/[A-Za-z0-9._-]+\.zip$` in the whole body.
  - `readZip(bytes: Uint8Array, limits = LIMITS) -> {ok:true, folder, files:[{name, data}]} | {ok:false, error}`; `LIMITS = { zipBytes: 2097152, entries: 5, totalBytes: 2097152 }`. Supports stored (0) and deflate (8) via `zlib.inflateRawSync(data, { maxOutputLength })`. Reads the central directory; rejects encryption, zip64, any method other than 0/8, a name that is absolute, contains `..`, `\`, or more than one `/`, a folder failing `^[A-Za-z0-9_-]{1,31}$`, a base name outside the four pack names, a duplicate name, a directory entry other than the single top folder, a Unix-mode symlink (external attributes `0xA000`), a CRC mismatch, and missing `theme.json`.
  - CLI `node scripts/intake.mjs --body <file> --zip <file> --out <dir>`: writes `<dir>/<folder>/...` and `<dir>/submission.json` (`{folder, name, author, basedOn}`); exit 1 with one plain line on stdout for the issue comment.

- [ ] **Step 1: Write the failing tests.** `zipread.test.mjs` builds zips in the test with a small local writer (copy of the designer's stored-zip layout, plus a deflate case made with `zlib.deflateRawSync`). One test per rejection named above, each asserting `ok:false` and a message naming the problem; one test that the designer-shaped zip (`ember/theme.json`, `ember/emblem.bin`) returns `folder: 'ember'` and byte-identical data; one "claims 10 bytes, inflates to 3 MB" test asserting rejection. `intake.test.mjs`: a well-formed body; unticked box; two links; a link to `https://example.com/a.zip`; `_No response_` for Based on gives `basedOn: ''`; a Based on value failing the folder rule is an error.
- [ ] **Step 2: Run** `npm test` — Expected: FAIL, missing modules.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `npm test` — Expected: PASS.
- [ ] **Step 5: Commit** `feat: read a submission form and unpack its zip within strict limits`.

### Task 7: Gallery index

**Files:**
- Create: `scripts/build-index.mjs`
- Test: `test/build-index.test.mjs`

**Interfaces:**
- Consumes: layout `themes/<folder>/{theme.json,...,meta.json}`; `meta.json` = `{ "login", "issue", "added": "YYYY-MM-DD", "author", "basedOn" }`.
- Produces: `buildIndex(root) -> {version: 1, themes: [...]}` in the format given in Task 2, sorted by `added` descending then folder; `findClash(index, {folder, name}) -> string|null` (case-insensitive on both; also rejects the four built-in names `UMBRAVOX`, `BAPH0MAU5`, `CVLT OF BAPH0MAU5`, `COSMIC MUFFIN`); CLI `node scripts/build-index.mjs <root> --out <file>` and `node scripts/build-index.mjs <root> --clash <folder> <name>` (exit 1 and one line when it clashes).

- [ ] **Step 1: Write the failing tests:** two theme folders give two entries with `files` listing only the pack files present (never `meta.json`); the `name` comes from `theme.json`; a folder without `meta.json` or with an unparsable `theme.json` makes the CLI exit 1 naming the folder; `findClash` cases: same folder different case, same name different case, built-in name, no clash.
- [ ] **Step 2: Run** `npm test` — Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** — Expected: PASS.
- [ ] **Step 5: Commit** `feat: build the gallery index and detect name clashes`.

### Task 8: Gallery page

**Files:**
- Create: `site/index.html`, `site/gallery.js`, `site/gallery.css`
- Test: `test/gallery-page.test.mjs`

**Interfaces:**
- Consumes: `index.json` (Task 7), `previews/<folder>/cyd.png` and `waveshare.png`, `themes/<folder>/<file>`.
- Produces: `cardHtml(entry, {designer, repo}) -> string` and `escapeHtml(s)` exported from `site/gallery.js`; the page fetches `index.json` relative to itself and fills `<main id="cards">`.

- [ ] **Step 1: Write the failing tests:** `cardHtml` for an entry named `<b>X</b>` by `A"B` contains the escaped text and no raw `<b>`; contains `href="https://em3ritus.github.io/umbravox/designer/?theme=ember"`; contains both picture paths with `alt` text and `loading="lazy"`; shows "Based on" only when set; contains a report link to `https://github.com/Em3ritus/umbravox-themes/issues/new?template=report-theme.yml&theme=ember`; `index.html` has a viewport meta tag, the content notice text "Themes are made by other people and are checked for format only.", and no external script or stylesheet host.
- [ ] **Step 2: Run** `npm test` — Expected: FAIL.
- [ ] **Step 3: Implement.** One column at phone width, 16 px side gutter, dark and light via `prefers-color-scheme`. "Download zip" is built in the browser from the theme's files (copy `makeZip` from the designer into `site/zip.js` with a comment naming its source) so no zip is stored in the repository. An empty index shows "No themes yet."
- [ ] **Step 4: Run** — Expected: PASS.
- [ ] **Step 5: Commit** `feat: the gallery page`.

### Task 9: Forms, rules and README

**Files:**
- Create: `.github/ISSUE_TEMPLATE/submit-theme.yml` (ids `theme-name`, `author`, `based-on`, `zip` (textarea), `agreement` (two required checkboxes); labels `submission`), `.github/ISSUE_TEMPLATE/report-theme.yml` (ids `theme`, `reason`; label `report`), `.github/ISSUE_TEMPLATE/config.yml` (`blank_issues_enabled: false`), `CONTENT-RULES.md`, `README.md`, `LICENSE-NOTE.md`
- Test: `test/forms.test.mjs`

- [ ] **Step 1: Write the failing tests:** the form's field labels equal the headings `parseSubmission` reads (`Theme name`, `Author credit`, `Based on`, `Theme zip`, `Agreement`); the ids equal the query keys `shareUrl` writes (`theme-name`, `author`, `based-on`); both checkboxes are `required: true`; `CONTENT-RULES.md` exists and the form links to it. Parse the YAML with a few regular expressions (no dependency).
- [ ] **Step 2: Run** `npm test` — Expected: FAIL.
- [ ] **Step 3: Write the files.** Draft wording for the owner to edit: right-to-share box "I made everything in this theme, or I have the right to share it publicly."; rules box "I have read the content rules and agree this theme may be removed at any time." Rules, short: nothing you do not have the right to share; nothing hateful, sexual or targeting a person; themes can be removed without notice; how to report one.
- [ ] **Step 4: Run** — Expected: PASS.
- [ ] **Step 5: Commit** `docs: submission form, report form and content rules`.

### Task 10: Workflows

**Files:**
- Create: `.github/workflows/submission.yml`, `.github/workflows/publish.yml`, `.github/workflows/tests.yml`, `FIRMWARE_PIN` (one line: the firmware tag), `scripts/assemble-site.mjs`
- Test: `test/workflows.test.mjs`, `test/assemble-site.test.mjs`

**Interfaces:**
- Consumes: `intake.mjs`, `build-index.mjs` (Tasks 6-7); from the firmware repository at `FIRMWARE_PIN`: `make -C sim umbravox`, `sim/umbravox --check-pack <folder>`, `tools/build-designer-wasm.sh`, `node tools/pack-preview.mjs <folder> --out <dir>` (Task 4), `tools/emsdk-version.txt`.
- Produces: `assembleSite(root, out)` copying `site/*`, `themes/` (pack files only, not `meta.json`), `previews/`, and a fresh `index.json` into `out`.

`submission.yml`, trigger `issues: [opened, edited]`, only when the issue has the `submission` label. Top-level `permissions: {}`.
- Job `check` (`permissions: contents: read`): check out this repository; write the issue body from `github.event.issue.body` through an environment variable to a file (never interpolated into a script); `curl --max-filesize 2097152 --fail --location` the zip; `node scripts/intake.mjs`; `--clash` check; check out the firmware repository at `FIRMWARE_PIN` into `fw/`; build the sim and run `--check-pack`; a commented, empty step named "Content screening (none yet)"; install emscripten as the firmware CI does and build the wasm; `pack-preview`; upload artifact `submission` (the theme folder, `submission.json`, two PNGs) and a `result.txt` holding either `OK` or the one-line reason. The job never fails on a bad submission: it records the reason.
- Job `propose` (`needs: check`, `permissions: contents: write, pull-requests: write, issues: write`): downloads the artifact and runs no code from it; re-checks that every file name is one of the four pack names or the two PNGs and the folder passes the folder rule; on `OK` writes `themes/<folder>/` + `meta.json` (login from `github.event.issue.user.login`, issue number, UTC date) and `previews/<folder>/`, force-pushes branch `submit/<issue-number>`, opens or updates the pull request whose body shows both pictures by their raw URL on that branch and says `Closes #N`, then comments on the issue; on failure only comments the reason.

`publish.yml`, trigger `push` to `main` and `workflow_dispatch`: `node scripts/assemble-site.mjs . _site`, upload and deploy to Pages (`permissions: pages: write, id-token: write, contents: read`). `tests.yml`: `npm test` on push and pull request.

- [ ] **Step 1: Write the failing tests.** `workflows.test.mjs` (text checks): `submission.yml` has top-level `permissions: {}`; the `check` job block contains no `write`; `${{ github.event.issue.body }}` and `.title` appear only on `env:` lines, never inside `run:`; every `uses:` of a non-`actions/` action is pinned to a 40-character commit; `publish.yml` triggers only on `main` and `workflow_dispatch`. `assemble-site.test.mjs`: the output has `index.html`, `index.json`, the pack files and PNGs, and no `meta.json`.
- [ ] **Step 2: Run** `npm test` — Expected: FAIL.
- [ ] **Step 3: Write the workflows and `assemble-site.mjs`.**
- [ ] **Step 4: Run** `npm test` — Expected: PASS. Then a local dry run of the whole path without GitHub: build a pack zip with the designer's `buildPack` + `makeZip`, a sample issue body, run `intake.mjs`, `--check-pack` and `pack-preview` from the firmware worktree, `assemble-site.mjs`, and `python3 -m http.server -d _site`; fetch `index.json` and one PNG with curl. Record the commands and output in the commit message body.
- [ ] **Step 5: Commit** `ci: submission checks, pull request proposal and site publishing`.

---

## Part C: go-live (every step needs the owner's ask in that turn)

### Task 11: Publish and prove it end to end

- [ ] **Step 1:** Owner confirms the repository name and the form wording. If the name changes, update `THEMES_REPO`, `THEMES_SITE` (firmware Task 1) and the themes repository's links; re-run both test suites.
- [ ] **Step 2:** Firmware: push `theme-sharing`, open the pull request, merge when CI passes, run the Release workflow (new tag) so the designer is live. Set `FIRMWARE_PIN` to that tag.
- [ ] **Step 3:** Themes: create the public repository, push `main`, enable Pages from Actions, and turn on "Allow GitHub Actions to create and approve pull requests" in the repository settings. Create the `submission` and `report` labels.
- [ ] **Step 4:** The owner submits one real theme from the live designer. Expected: a comment and a pull request with two pictures within a few minutes. Merge it; the gallery shows it; "Open in designer" loads it; install it on a board.
- [ ] **Step 5:** Submit one broken pack (for example a `theme.json` with a built-in name). Expected: a plain-language comment, no pull request.
- [ ] **Step 6:** Remove the test theme's folders; the gallery no longer lists it.
- [ ] **Step 7:** Check the gallery and the designer in a real browser at desktop and phone width. Update `docs/theme-packs.md` with what was and was not verified.
