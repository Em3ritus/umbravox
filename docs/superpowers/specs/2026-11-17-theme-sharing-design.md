# Theme sharing: a submit button, a themes repository and a gallery (design)

Status: DRAFT for owner approval, 2026-11-17. Branch `theme-sharing`. This is project 2 of the four set out in
`2026-10-21-theme-packs-design.md` ("Sharing: a public themes repository, CI validation, preview gallery").

## Intent and why
People can already make a theme pack in the designer and put it on their own card. There is no way to pass one on
except sending a zip by hand, and the designer's "Share..." button only says sharing arrives later. The owner wants a
person who has just saved or installed a theme to be able to offer it to everyone with one button, and wants a visitor
to be able to pick a shared theme, open it in the designer, tweak it and install it.

## Goals
- **Submit from the designer.** After a pack is valid, a Share button saves the zip and opens a pre-filled submission
  form on GitHub. The person drags the zip in and submits. A GitHub account is required (owner decision).
- **Automatic checks.** Each submission is unpacked safely, checked with the device's real pack parser, and rendered to
  preview pictures. A submission that fails gets a comment that says why, in plain words.
- **Owner approval.** Nothing is public in the gallery until the owner approves it. Approval is merging a pull request
  that the automation prepared; the pull request shows the preview pictures.
- **Gallery.** A page listing approved themes with their pictures for the CYD and the Waveshare. Clicking a theme opens
  it in the designer, loaded and ready to tweak, download or install.
- **Gallery updates on approval**, without a firmware Release run.
- **Remixes.** A changed copy of a gallery theme can be submitted as a new theme under a new name, with a "based on"
  credit (assistant's proposal; the owner did not object).

## Non-goals
- No upload service and no anonymous submissions (the "approach B" front door). The design must not block adding it later.
- No downloading on the device. Packs still arrive on the card or over USB.
- No scripting or full-UI themes: that is projects 3 and 4, to be brainstormed separately right after this one. The
  owner wants them ("100% overhaul the entire UI, design alert animations").
- No ratings, comments, accounts or search beyond what a static page can do.
- No automated content screening in this version (the owner may add some later; see "Left room for").

## Anti-goals (a failure even if it works)
- A submission that can run code in the workflow with write access, escape its folder, or exhaust the runner.
- A theme appearing in the gallery that the owner did not approve.
- A gallery theme that the current firmware would skip.
- A takedown or account action against the themes repository affecting the firmware repository's code or releases.
- The owner's real name, email or home path in any tracked file.

## Constraints (checked)
- The designer is a static page on GitHub Pages (`https://em3ritus.github.io/umbravox/designer/`); there is no server.
- A web page cannot attach a file to a GitHub form for the user. The user drags the zip in.
- A zip attached to an issue in a public repository can be downloaded without logging in: tested 2026-11-17 against an
  existing public issue attachment (`https://github.com/user-attachments/files/<id>/<name>.zip`, HTTP 200, `PK` header).
  Not tested: the size limit for zip attachments on github.com (older docs say 25 MB). A full pack is at most about
  1.4 MB (theme.json 4 KB, emblem.bin 9.5 KB, radar.vid 780 KB, alert.snd 576 KB), so this is expected to fit.
- `sim/umbravox --check-pack <folder>` runs the device's real loader and prints `OK <name>` or the reason.
- The designer's drawing engine (the shared `ui/` code as WebAssembly) already renders both boards under Node in the
  test suite (`web/designer/test/preview-boards.test.mjs`), so it can render preview pictures in a workflow.
- The designer has "Open an existing theme" from local files; it does not read any URL parameter today.
- The device loads at most 12 packs and rejects a duplicate theme name or one of the four built-in names.

## Approaches considered
1. **Submission becomes a pull request; gallery uses rendered pictures (chosen).** A workflow turns each submitted zip
   into a pull request with the theme folder and its pictures; the owner merges to approve; a static gallery shows the
   pictures. The pictures are both what the owner reviews and what the gallery shows. Cost: the most workflow code,
   and it handles files from strangers.
2. **Same submission flow; gallery draws every theme live** with the designer's engine. Always matches the firmware and
   shows video at its best, but the page is heavy on phones and the pull request has no picture to review.
3. **No automation at first:** the owner checks and adds each zip by hand. Cheapest, but every submission is manual
   work, which defeats an easy submit button.

The owner chose approach 1 on 2026-11-17.

## Decisions made by the owner
- A GitHub account is required to submit; the button opens a pre-filled GitHub form ("approach A").
- From the gallery, a theme opens in the designer to tweak and install.
- Content policy is option 3: pictures, video and sound are all accepted behind a required tick box ("I made this or
  have the right to share it"); the owner removes on complaint. The owner was told the tick box does not remove their
  exposure as the publisher and accepted that.
- Approach 1 above.

## Design

### Where things live
- A **separate public repository** (working name `umbravox-themes`, under the same account) holds the approved themes,
  the submission form, the workflows and the gallery page. It publishes its own GitHub Pages site, which is served at
  `https://em3ritus.github.io/umbravox-themes/`: the same origin as the designer, so the designer can fetch a theme's
  files directly. (Assistant's call; reason: isolation from the firmware repository.)
- The firmware repository changes only in the designer (the Share button and loading a theme from the gallery) and in
  the docs.

### Themes repository layout
```
themes/<folder>/theme.json, emblem.bin, radar.vid, alert.snd   exactly what the designer writes
themes/<folder>/meta.json        submitter's GitHub login, submission issue number, date, "based on" (optional)
previews/<folder>/cyd.png, waveshare.png
index.json                       generated list the gallery and the designer read
```
`<folder>` follows the device's folder-name rule (letters, digits, `-`, `_`).

### Submitting (designer)
- The Share button is enabled when the current pack passes the designer's checks. It downloads `<folder>.zip` (the
  same zip as "Download pack") and opens the themes repository's issue form in a new tab with the title, theme name,
  author and target boards pre-filled through the form's URL parameters.
- The form (a GitHub issue form) has: theme name, author credit, "based on" (optional), a field to drag the zip into,
  and two required tick boxes: the right-to-share statement and agreement to the content rule.
- The page tells the user, before leaving, that they must drag the saved zip into the form.

### Checking (themes repository workflow, on a new or edited submission issue)
Runs with read-only permissions on the untrusted content; the step that writes (comment, branch, pull request) never
executes anything from the submission.
1. Find exactly one `user-attachments/files/...zip` link in the issue body; otherwise comment and stop.
2. Download with a size cap (2 MB). Unpack with limits: at most 5 entries, only the four known file names under one
   folder, no absolute paths, no `..`, no links, total unpacked size capped at 2 MB.
3. Run the device's pack checker built from the firmware repository at a pinned commit. Also reject a theme name or
   folder already in the gallery.
4. Render `cyd.png` and `waveshare.png` (scan screen, a fixed frame) with the designer's engine at the same pinned commit.
5. Open a pull request adding `themes/<folder>/`, `meta.json` and `previews/<folder>/`, with the pictures shown in its
   description, and comment on the issue with the result. On failure, comment the parser's reason in plain words.
There is a clearly marked, empty "content screening" step between 3 and 4 for later automation.

### Approving
- The owner merges the pull request. A workflow on merge regenerates `index.json`, deploys the Pages site and closes the
  submission issue with a link to the theme in the gallery. Closing the pull request unmerged rejects the theme.
- Removing a theme (for a complaint or any reason) is deleting its folders; the same workflow republishes.

### Gallery
- A static page: one card per theme with name, author, "based on", both pictures and two actions: **Open in designer**
  and **Download zip**. Works at phone width. A "Report a theme" link opens a pre-filled report issue.
- A short content rule and a notice that themes are checked for format only and are made by other people.

### Opening a gallery theme in the designer
- `…/umbravox/designer/?theme=<folder>` makes the designer fetch `index.json` and that theme's files from the themes
  site, then load them through the same path as "Open an existing theme". `<folder>` is validated against the
  folder-name rule and must be listed in `index.json`; nothing else from the URL is used.
- If the theme cannot be fetched or fails the designer's checks, the designer says so and stays usable.
- A theme opened this way carries its gallery name as "based on" if it is later shared under a new name.

## Left room for
- An upload service (no GitHub account) that files the same kind of submission.
- Automated content screening in the marked workflow step.
- Pack types from projects 3 and 4: the checker and renderer are taken from a pinned firmware commit, so a newer
  format is supported by moving the pin; `index.json` carries a format version per theme.

## Left to the builder
- How the pinned checker and renderer reach the themes repository's workflow (build from a checkout of the firmware
  repository, or download a prebuilt bundle published by the firmware repository's Release run).
- The exact workflow split, PNG encoding, picture scale, and gallery styling (reuse the designer's look).
- How `index.json` is generated and cached.

## How it will be verified
- Host tests for: zip safety limits (path escape, too many entries, oversize, wrong names), issue body parsing, the
  `?theme=` validation, and index generation. Designer tests for loading a theme by URL and the Share link's parameters.
- One real end-to-end submission by the owner on the live repositories, including a rejected pack and a removed theme.
- The designer in a real browser, desktop and phone width.

## Open items for the owner
- The repository name (`umbravox-themes` is a placeholder) and whether its site may be public from day one.
- Wording of the content rule and the right-to-share statement.
- Whether the owner's own bundled-style packs should seed the gallery.
