# Rules for changing qaros-ros-rviz

For everyone who changes this repository, people and coding agents alike. How to build and test is
in [docs/DEVELOPING.md](docs/DEVELOPING.md) and [docs/TESTING.md](docs/TESTING.md).

## Public repository

- This repository is public. Commits, pull requests, issues, code comments and documentation never
  cite internal documents (internal architecture decision records, private notes or paths); link a
  public issue or pull request instead, here or in
  [Quaternar/Quaternar](https://github.com/Quaternar/Quaternar/issues).
- No AI attribution anywhere: no `Co-Authored-By` trailer for a tool, no "Generated with" line, no
  tool or assistant name in commits, pull requests or issues.
- Commit subjects follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
  (`feat:`, `fix:`, `docs:`, `build:`, `ci:`, `refactor:`, `test:`; `!` marks a breaking change),
  with a short bullet list as the body.

## Changelog

[CHANGELOG.md](CHANGELOG.md) is written with every change, not at release time:

- Every change a user can notice gets one entry under `## [Unreleased]`, in the same commit as the
  change. A user is whoever runs the plugin in RViz2, runs the launchers and scripts, installs a
  release asset, builds against the libraries or runs the examples: a panel control, setting,
  environment variable, launcher parameter, script option, release asset or manifest field, API,
  default or documented behaviour.
- One line per entry: what changed for the user, and what they must do if anything, followed by a
  link to the related GitHub issue or pull request when there is one, as a full markdown link, for
  example `([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5))`. Issues of
  `Quaternar/Quaternar` may be linked as well.
- Sections in this order, only those that have entries: `### Breaking`, `### Features`,
  `### Enhancements`, `### Bug fixes`, `### Deprecated`, `### Removed`, `### Security`.
- `Breaking` is anything that makes a user act: a removed or renamed setting, control or API, a
  changed default, frame, unit or other assumption, a changed install step or release asset
  format. The entry carries its migration step.
- No entry for refactors, tests, CI or internal build changes that a user cannot notice.
- A later commit that refines the same change amends its existing `Unreleased` entry.

## Versions and releases

[VERSION](VERSION) holds the next planned version `X.Y.Z` of the plugin and is its only copy; CMake
reads it (`cmake/QarRvizVersion.cmake`) in the unit project and in `qar_rviz_streaming`. ROS tools
need a literal version in `qar_rviz_streaming/package.xml`: keep it equal to `VERSION`, the
configure stops when they differ. The full version of a build is derived, never typed:

| HEAD | Version | Example |
|---|---|---|
| tagged `vX.Y.Z` | `X.Y.Z` | `0.2.1` |
| tagged `vX.Y.Z-rc.N` | `X.Y.Z-rc.N` | `0.2.1-rc.1` |
| any other commit | `X.Y.Z-dev.<commits since the last v* tag>+<short sha>` | `0.2.1-dev.4+1a2b3c4d` |
| pull request build of the release workflow | `X.Y.Z-pr.<number>` | `0.2.1-pr.5` |

- The panel logs that version and tells it to the QAROS Hub as its app version; the build installs
  it as `share/qar_rviz_streaming/plugin_version`, which the register scripts record.
- A tag whose `X.Y.Z` differs from `VERSION` stops the CMake configure and the release workflow.
  `-DQAR_RVIZ_PLUGIN_VERSION_FULL=<version>` replaces the derived part (the workflow passes the
  release version); its `X.Y.Z` must equal `VERSION` too.
- The version is fixed when CMake configures: configure again after a new commit or tag.
- Release tags are the plugin's own version, not the QAROS version. Which QAROS a release works
  with is recorded per zip in `manifest.json`: `c_api_header_sha256` and `min_qaros_version`, the
  `VERSION` of the `external/qaros` submodule the build used. Updating that submodule to a QAROS
  with another C API is a change users notice: give it a changelog entry.
- Semantic versioning: a `MINOR` version adds features and tries hard not to break users; when it
  must, the change is under **Breaking** with its migration step, and a removal is deprecated at
  least one minor version earlier where possible. A `PATCH` version only fixes. While the major
  version is 0, the CMake packages accept a request only for the same minor version.
- Tags go on `main`; there are no release branches.

To release `X.Y.Z`:

1. Optional release candidate: tag the commit to test `vX.Y.Z-rc.N` on `main` and push the tag;
   the release workflow publishes it as a GitHub pre-release.
2. Release commit: move the entries of `## [Unreleased]` under a new heading
   `## [X.Y.Z] - YYYY-MM-DD` and leave an empty `## [Unreleased]` above it. Change nothing else in
   this commit.
3. Tag that commit `vX.Y.Z` on `main` and push the tag. The release workflow
   (`.github/workflows/release.yml`) builds the zips and `manifest.json` and attaches them to the
   GitHub release `vX.Y.Z`.
4. Bump: in the next commit set `VERSION` and the `<version>` of `qar_rviz_streaming/package.xml`
   to the next planned version (`X.(Y+1).0`), never in the tagged commit.
