# OS64 Project History

This directory records stable project milestones separately from the forward-
looking roadmap. Each milestone is anchored to an annotated Git tag and an
immutable commit, so later source and documentation changes do not alter the
recorded state.

## Timeline

| Date | Milestone | Tag | Commit | Commits | Files | Lines |
| --- | --- | --- | --- | ---: | ---: | ---: |
| 2026-07-12 | [Phase 3.5 stabilization complete](phase-3.5/README.md) | `phase-3.5-complete` | `27e18e3` | 150 | 351 | 47,906 |
| 2026-07-13 | [Phase 3.6 driver packaging complete](phase-3.6/README.md) | `phase-3.6-complete` | `5107211` | 154 | 361 | 49,563 |
| 2026-07-13 | [Phase 4 entry baseline](phase-4-entry/README.md) | `phase-4-entry` | `53053a6` | 155 | 365 | 50,092 |
| 2026-07-15 | [First supervised window](first-window/README.md) | `first-window` | `231f27e` | 168 | 416 | 57,217 |
| 2026-07-17 | [First public-SDK GUI application](first-gui-app/README.md) | `first-gui-app` | `4bafe1f` | 175 | 429 | 62,921 |
| 2026-07-20 | [Phase 4 GUI foundation complete](phase-4/README.md) | `phase-4-complete` | `8d44929` | 186 | 440 | 66,615 |

The documentation reorganization immediately after the Phase 4 entry baseline
is commit `b553657` (`156` commits, `366` tracked files, and `50,139` total
lines). It is an administrative change rather than a product milestone.

## Measurement

All statistics are calculated from the recorded commit, not from the current
working tree:

```sh
git rev-list --count <commit>
git ls-tree -r --name-only <commit> | wc -l
git archive --format=tar <commit> | tar -xOf - | wc -l
```

The line total covers every Git-tracked project file, including target code,
host tools, tests, configuration, and documentation. It is a repository growth
metric, not a source-only LOC measurement.

## Recording Future Milestones

For each meaningful event:

1. finish and verify the implementation;
2. commit the stable state;
3. add a milestone page and a row to this timeline;
4. create an annotated tag on the stable commit;
5. push the commit and the specific tag to the remote.

Suggested future milestone names include `first-window`, `first-mouse-input`,
`first-gui-app`, `first-animation`, `bad-apple`, and `aarch64-first-boot`.
