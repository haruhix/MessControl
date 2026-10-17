# KawaiiPhysics upstream snapshot

- Repository: https://github.com/pafuhana1213/KawaiiPhysics
- Commit: `feee5fe727261ba9cfe725c4af280aa9e381c31d`
- Retrieved: 2026-10-10
- Upstream descriptor version: 1.21.0
- License: MIT; see `LICENSE`. Copyright (c) 2019-2026 pafuhana1213.

This project includes the upstream plugin's Source, Resources, Config and
Content/Localization directories. Source is initially copied without changes.
The repository's sample project, sample characters and other third-party sample
assets are excluded. Content/Python is excluded: it is optional editor toolset
registration and is not required by the runtime animation node.

The upstream test sources are retained, but its optional KawaiiPhysicsTests
module is omitted from the project descriptor to keep editor iteration small.
Runtime, Sequencer and editor authoring modules remain enabled.

Local compatibility patch: KawaiiPhysicsTestGameplayTags.cpp includes its own
header first to satisfy this project's UE 5.8 IWYU checks. Runtime source is unchanged.
