# Blender extension distribution

The integration supports Blender 4.2 and newer. Its version is independent of
the four-part osci-render product version and lives in
`blender/osci_render/blender_manifest.toml`.

The public remote repository is `https://osci-render.com/blender/index.json`.
It is served by the `osci-render-website` GitHub Pages project. The repository
and its ZIP are free and need no license token. Blender checks package hashes
and handles subsequent updates through Get Extensions.

## Build and test

Use a Python 3.11+ interpreter and a Blender executable:

```sh
python3 ci/build_blender_extension.py --blender /path/to/blender --output /tmp/new-blender-repository
python3 ci/test_blender_extension.py --blender /path/to/blender --repository /tmp/new-blender-repository
```

The output directory must not already contain that version's ZIP. Packaging
uses Blender's validator, builder and repository generator; ZIP timestamps are
normalized for reproducible publication. The source, manifest and GPL license
are included. Tests use a temporary user profile and a local HTTP repository.

`.github/workflows/blender.yaml` checks Blender 4.2, 4.5 and 5.2 and uploads a
repository artifact. Bump the extension version whenever changing shipped code.

## Publish

After committing and testing the extension, run the **Publish Blender extension**
workflow in `osci-render-website`, passing the full osci-render source commit SHA.
That workflow builds and tests the package, updates `public/blender`, records
source provenance, commits to the website's master branch and explicitly
dispatches its Pages deployment. It uses the website's standard `GITHUB_TOKEN`;
no cross-repository write token is needed. Branch protections may require the
website owner to permit that publication workflow or commit the staged files
through their normal review process.

Already published package URLs are immutable. Publication refuses to change
the bytes for an existing version or downgrade the stable index. Older ZIPs
remain available, but the index lists only the current release.

The installer endpoint should be published before distributing the new installer.
The initial ZIP and index are staged in the website project alongside the manual
installation page. Subsequent releases use the workflow above.

## Installer behavior

Select **osci-render**, then **Blender integration**. Setup shows a version selector,
Install (or Update for an existing installation), and Back. The selector also lets
users choose a custom Blender installation. Replacement approval appears only
for an older enabled add-on; manual installation and logs appear when needed.
Multiple detected profiles are available in the selector; setup can be repeated
for each. Blender setup is independent of the product download.

Setup requires Blender to be closed and runs a bundled helper through Blender's
Python command line. Only the osci-render repository is refreshed. The download
invocation uses `--online-mode`; the saved global online-access preference is
unchanged. Inspect/prepare/install/verify run in separate processes. Verification
checks that the extension loads in a new process after preferences were saved.
Blender 4.2 may return FINISHED after a subprocess download failure, so the helper
also checks its error diagnostics.

Migration requires the explicit Replace checkbox. Other enabled osci-render
copies are disabled in preferences but their files are retained. This also
handles the historical broken unregister() without loading both copies in the
installation process. If setup fails, the previous enabled selection is restored;
the repository and any successfully downloaded new files can remain. A setup log
is available through the UI. A hard process interruption can require retrying setup.

Detection covers normal macOS application locations, Windows App Paths and
Blender Foundation directories, PATH, common Linux archives, Snap and Flatpak.
Custom installations can be selected manually. Flatpak/Snap launch and Windows
runtime behavior require platform validation; macOS is also tested locally.

The manual/offline ZIP installs into Blender's local repository and does not
receive remote updates until migrated. The website explains both paths.
