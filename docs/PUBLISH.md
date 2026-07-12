# Publishing an InstaMAT2Remix release

A short checklist to cut a GitHub release from your machine. Run everything from
the repo root (`InstaMAT2RemixClaude/`) unless noted. The version is read from
`InstaMAT2Remix/PluginInfo.h` (`kPluginVersion`) — bump it there first; it drives
both the About dialog and the zip filename.

Examples below use `v0.0.2-alpha`; substitute the version you're shipping.

---

## 0. Verify locally (do this first)

```powershell
cd InstaMAT2Remix
.\build_plugin.ps1                     # build + dev-install; must succeed
cmake -S . -B build_test -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.6.3/msvc2019_64"
cmake --build build_test --target TestRemixConnector --config Debug
ctest --test-dir build_test -C Debug -V           # QtTest totals: 29 passed / 0 failed
cd ..
python -m pytest InstaMAT2Remix/tests/ -v          # expect 5/5
cd InstaMAT2Remix
.\package_release.ps1                              # watch for "Bundled MSVC runtime ..."
```

Confirm the produced zip contains the runtime + worker (the export worker fails
to start without the app-local CRT):

```powershell
$zip = Get-ChildItem dist\InstaMAT2Remix-v*-win64.zip | Select-Object -First 1
Add-Type -AssemblyName System.IO.Compression.FileSystem
$names = [System.IO.Compression.ZipFile]::OpenRead($zip.FullName).Entries.FullName
'vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','InstaMAT2RemixExport.exe',
'texconv.exe','InstaMAT2RemixPlugin.dll','InstaMAT2Remix.IMP' |
  ForEach-Object { if ($names -match [regex]::Escape($_)) { "OK       $_" } else { "MISSING  $_" } }
```

Every line must read `OK`.

---

## 1. Build the public snapshot + push

The public repo's `main` is a **clean snapshot**, not the local dev history
(`v0.0.1-alpha` was published as an orphan snapshot; GitHub then added
`.github/FUNDING.yml` on top — preserve it). Do NOT `git push origin HEAD`
from a dev branch: that would publish the private history.

```powershell
cd ..    # repo root
git fetch --prune origin                 # clears stale refs from the renamed old repo
git switch -c release/v0.0.2-alpha origin/main
# Overlay the release tree from the dev branch, keeping FUNDING.yml:
git checkout <dev-branch> -- .
git checkout origin/main -- .github/FUNDING.yml
# Strip internal-only paths (mirror the 0.0.1 snapshot exclusions).
# benchmark/ is 30+ files of tracked CMake build junk incl. binaries;
# docs/superpowers/ holds internal plans/specs (NOT docs/plans|specs):
git rm -r --cached --ignore-unmatch .cursor .vscode/texconv.exe benchmark `
  docs/history.md docs/superpowers docs/qml_accessibility_log.md `
  docs/TESTING_GUIDE.md InstaMAT2Remix/tests/__pycache__ .claude
git status              # review: version bump, README/CHANGELOG/RELEASE_NOTES, sources
git diff --stat origin/main              # only intended files
git commit -m "Release v0.0.2-alpha"
git push origin HEAD:main
```

## 2. Tag

```powershell
git tag -a v0.0.2-alpha -m "InstaMAT2Remix v0.0.2-alpha"
git push origin v0.0.2-alpha
```

## 3. Create the GitHub Release + upload the zip

Do NOT pass `--prerelease`: v0.0.1-alpha was published as a normal release, so
`releases/latest` (which excludes prereleases) tracks the newest build only if
every release stays non-prerelease. A prerelease v0.0.2 would leave "Latest"
pointing at the older zip.

```powershell
gh release create v0.0.2-alpha `
  "InstaMAT2Remix/dist/InstaMAT2Remix-v0.0.2-alpha-win64.zip" `
  --title "InstaMAT2Remix v0.0.2-alpha" `
  --notes-file "RELEASE_NOTES.md" `
  --verify-tag
```

## 4. What users do (already in the README / RELEASE_NOTES)

```text
1. Download InstaMAT2Remix-v*-win64.zip from Releases and unzip.
2. Close InstaMAT Studio.
3. powershell -ExecutionPolicy Bypass -File .\install.ps1
4. Start RTX Remix Toolkit (project open), then InstaMAT Studio.
5. Use the "RTX Remix Connector" menu.   (Uninstall: uninstall.ps1)
```

## 5. Optional hardening

- The `.github/workflows/release.yml` workflow is **manual-only** by default and
  builds + verifies the zip payload but does not publish. To make future
  releases tag-triggered, uncomment its `push: tags` block — test it once via
  **Actions → Release InstaMAT2Remix → Run workflow** first, and always
  sanity-check the CI zip's CRT payload (a hosted runner's VS redist layout may
  differ from a local build).
- Enable branch protection on `main` (require the `build.yml` checks):
  repo **Settings → Branches**.
