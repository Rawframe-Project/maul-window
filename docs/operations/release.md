# Release

Releases are made by release-please from the conventional commit titles on `main`. Versions follow Semantic Versioning; before 1.0.0 any release may change the API, the ABI and the behavior, and a breaking change bumps the minor version. Tags are `vX.Y.Z`.

## Steps

1. release-please keeps a draft pull request titled `chore: release X.Y.Z` open. It moves the version between the markers in `include/maul-window/base.h`, where CMake reads it and from which `MWIN_ABI_VERSION` follows, updates `.release-please-manifest.json`, and adds an entry to `CHANGELOG.md` from the `feat`, `fix` and `perf` titles.
2. Rewrite that entry in user language before merging: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security, with the platform first where a change is one platform's. Leave out refactors, tests, CI and documentation. Say what a caller must change for a breaking change.
3. Check that the `required` CI job passed on the release pull request, including every platform cell.
4. Mark the pull request ready and merge it. release-please tags the merge commit and publishes the GitHub release with the changelog entry as its notes. The Pages workflow publishes the documentation from `main`.
5. The `assets` job of `.github/workflows/release.yml` attaches the source archive `maul-window-X.Y.Z.tar.gz`, its `SHA256SUMS`, an SPDX SBOM and a build provenance attestation. A user verifies a download with `sha256sum -c SHA256SUMS` and `gh attestation verify maul-window-X.Y.Z.tar.gz -R Rawframe-Project/maul-window`.

## Notes

- Before 1.0 every release has its own ABI: the shared library's `SOVERSION` is the whole version, the CMake package accepts only the exact version, and `mwinRun` refuses an application def stamped by another release's headers.
- The shared family files must match their canonical copies in the organization's `.github` repository before a release.
- Maul UI and Maul RHI's test seam fetch this library by tag; each moves its pin in a pull request of its own after a release.
