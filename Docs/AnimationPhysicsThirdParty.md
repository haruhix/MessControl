# Animation physics third-party sources

Verified from upstream source on 2026-10-10.

## KawaiiPhysics

- Author: pafuhana1213.
- Source: https://github.com/pafuhana1213/KawaiiPhysics
- Pinned commit: `feee5fe727261ba9cfe725c4af280aa9e381c31d`.
- Plugin descriptor version: 1.21.0.
- License: MIT. The complete upstream copyright and license are preserved at
  `Plugins/KawaiiPhysics/LICENSE`.
- Scope: plugin source, configuration, icons and localization; no sample
  characters, sample project, third-party models or optional Python editor
  toolset registration.
- Runtime interface: `FAnimNode_KawaiiPhysics`, from module `KawaiiPhysics`.
  This applies secondary motion to configured skeletal bone branches. It does
  not replace gameplay collision, character movement or authored animation.

## StylizedDynamics local experiment

- Author: Jared Taylor (Vaei).
- Source: https://github.com/Vaei/StylizedDynamics
- Inspected commit: `db9f3862c8bdf4675e27c4c4c945a83e1f1de7f2`.
- Plugin descriptor version: 1.0.0; upstream marks it beta and work in progress.
- The inspected repository has copyright notices but no LICENSE file or explicit
  redistribution grant in its README, descriptor or source headers. GitHub's
  repository metadata also reports `license: null`.
- The user requested local installation and comparison experiments. Its code,
  resources and documentation are installed under `Plugins/StylizedDynamics`
  for that local experiment; the inspection checkout is retained under ignored
  Saved/ThirdPartyDownloads.
- The exact plugin directory is excluded from Git with this checkout's
  `.git/info/exclude`, without modifying the project's shared `.gitignore`.
  Its source is not to be published or redistributed while terms are unresolved.
- The game adapter detects the optional local plugin. Another checkout without
  this source must build using the available providers and show StylizedDynamics
  as unavailable, rather than assuming it is bundled.
- Exported runtime interfaces include `FAnimNode_StylizedDynamics` and
  `FStylizedDynamicsPoseSolver`. This secondary motion solver supports selected
  bones, chains and optional squash/stretch against the authored pose.

Actual enablement, defaults and runtime validation are documented with the
game's comparison integration. Downloading or reading an upstream repository
does not establish redistribution permission.
