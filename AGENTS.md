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
  with a short bullet list as the body. A pull request's title follows them too: it decides the
  release when the pull request merges (see [Versions and releases](#versions-and-releases)).

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

[VERSION](VERSION) holds the version `X.Y.Z` of the last release of the plugin and is its only
copy; CMake reads it (`cmake/QarRvizVersion.cmake`) in the unit project and in
`qar_rviz_streaming`. ROS tools need a literal version in `qar_rviz_streaming/package.xml`: it
equals `VERSION`, the configure stops when they differ. Both change only in a release commit,
which CI makes. The full version of a build is derived, never typed; any build but a release
carries the next patch:

| HEAD | Version | Example (`VERSION` 0.3.0) |
|---|---|---|
| tagged `vX.Y.Z` | `X.Y.Z` | `0.3.0` |
| any other commit | `X.Y.(Z+1)-dev.<commits since the last v* tag>+<short sha>` | `0.3.1-dev.4+1a2b3c4d` |
| pull request build of the release workflow | `X.Y.(Z+1)-pr.<number>` | `0.3.1-pr.5` |

- The panel logs that version and tells it to the QAROS Hub as its app version; the build installs
  it as `share/qar_rviz_streaming/plugin_version`, which the register scripts record.
- A tag whose `X.Y.Z` differs from `VERSION` stops the CMake configure and the release workflow.
  `-DQAR_RVIZ_PLUGIN_VERSION_FULL=<version>` replaces the derived part (the workflow passes the
  release version): a release `X.Y.Z` must equal `VERSION`, a `-pr`/`-dev` version must carry the
  next patch.
- The version is fixed when CMake configures: configure again after a new commit or tag.
- Release tags are the plugin's own version, not the QAROS version. Which QAROS a release works
  with is recorded per zip in `manifest.json`: `c_api_header_sha256` and `min_qaros_version`, the
  `VERSION` of the `external/qaros` submodule the build used. Updating that submodule to a QAROS
  with another C API is a change users notice: give it a changelog entry.
- Semantic versioning: a `MINOR` version adds features and tries hard not to break users; when it
  must, the change is under **Breaking** with its migration step, and a removal is deprecated at
  least one minor version earlier where possible. A `PATCH` version only fixes. While the major
  version is 0, the CMake packages accept a request only for the same minor version.
- Tags go on `main`; there are no release branches and no candidate (`-rc`) tags.
- This repository follows the shared QAROS versioning scheme: [ADR-0001: One versioning scheme for every QAROS repository](https://github.com/Quaternar/qaros/blob/main/docs/adr/0001-one-versioning-scheme.md),
  refined for apps by [ADR-0002: QAROS apps release on every merge that changes behaviour](https://github.com/Quaternar/qaros/blob/main/docs/adr/0002-app-release-on-merge.md).

Every merge to `main` that changes behaviour is a release; nobody tags or bumps `VERSION` by hand.
`.github/workflows/app-release.yml` decides the bump of a merged pull request. If it is not
*none*, CI writes the new version into `VERSION` and `package.xml` (the files in
`.github/app-release.json`), moves the entries of `## [Unreleased]` under
`## [X.Y.Z] - YYYY-MM-DD`, commits `chore(release): vX.Y.Z`, tags it `vX.Y.Z` and pushes both. It
then starts the release workflow (`.github/workflows/release.yml`) on the tag, which builds the
zips and `manifest.json` and attaches them to the GitHub release `vX.Y.Z`.

A `release:*` label on the pull request wins; without one, its title decides:

| Label | Title (Conventional Commit) | Bump | From `0.3.0` |
|---|---|---|---|
| `release:major` | | major | `1.0.0` |
| | `feat!: ...`, `fix(x)!: ...`, or `BREAKING` in the title | major (minor below 1.0.0) | `0.4.0` |
| `release:minor` | `feat: ...` | minor | `0.4.0` |
| `release:patch` | `fix: ...`, `perf: ...`, `revert: ...` | patch | `0.3.1` |
| `release:none` | `docs`, `ci`, `chore`, `test`, `refactor`, `style`, `build` | none | none |

- The check **Release bump** on every pull request shows the version merging it releases. It fails
  when the title is no Conventional Commit of a known type and no `release:*` label is set.
- The release ships every `Unreleased` entry, so the changelog sets a floor: `Breaking` or
  `Removed` entries make it at least a breaking bump, `Features`, `Enhancements` or `Deprecated` at
  least a minor, `Bug fixes` or `Security` at least a patch.
- To merge without releasing (pull requests that only make sense together), label them
  `release:none`. The next releasing merge ships them all.
- A failed release build: fix it on `main`. If the fix was in CI only, run the release workflow
  again on the tag (`gh workflow run release.yml --ref vX.Y.Z -f version=X.Y.Z -f publish=true`).
  A tag never moves.
