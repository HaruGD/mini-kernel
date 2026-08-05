# Repository Instructions

These instructions apply to the entire repository.

## Source Of Truth

Do not rely on conversation memory for project behavior, policy, status, or
completion claims. Before changing a subsystem:

1. read the **Core Philosophy: Documented By Design** section in `README.md`;
2. inspect the relevant documents under `docs/architecture/`,
   `docs/reference/`, and `docs/phases/`;
3. inspect the current implementation and tests;
4. resolve discrepancies explicitly instead of assuming that either the code
   or documentation is current.

The repository is the project's durable memory. Conversation summaries and
prior assistant statements are not authoritative project contracts.

## Documented By Design

Undocumented externally observable behavior is a bug. A feature is not
complete until all applicable parts are complete together:

- implementation;
- public contract and ABI documentation;
- explicit success results and distinct error codes;
- ownership, lifecycle, cancellation, and cleanup rules;
- permissions, trust boundaries, and user-memory rules;
- concurrency, lock ordering, execution-context, and blocking rules;
- resource limits, failure handling, and recovery behavior;
- positive, negative, fault-injection, regression, and soak coverage;
- measured execution evidence recorded in the relevant phase documents.

Do not introduce ambiguous `NULL`, zero, or boolean failure conventions when a
caller needs the cause. Public operations must expose documented result
semantics and stable, specific error codes. Output values must have documented
success and failure-state rules.

Prefer a machine-readable single source of truth for public contracts. Generate
shared constants, SDK bindings, reference material, and ABI checks from that
source when practical. Tests must reject stale generated artifacts and ABI or
documentation drift.

## Change Discipline

- Update code, documentation, and affected tests in the same work item.
- Do not mark a phase or feature complete without passing its documented exit
  gate and recording real evidence.
- Preserve unrelated user changes and do not rewrite project history.
- Verify changes in proportion to their risk, including inherited regression
  suites for affected contracts.
- Commit completed work before handing it back to the user. Do not create an
  empty commit when a request is read-only or makes no repository change.

