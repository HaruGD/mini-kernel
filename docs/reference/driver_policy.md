# Driver Settings And Product Policy

Phase 3.6 separates intrinsic driver metadata from product build and activation
policy.

## Driver-Local Files

Every driver directory contains:

```text
settings.json
Makefile
```

`settings.json` owns stable identity, version, description, domain, entry
symbol, permissions, dependencies, imports, exports, and optional linked
integration facts. The local Makefile describes how the project can compile.
Neither file enables the driver or selects its artifact/stage/policy.

The machine-readable schema is
`config/schemas/driver-settings.schema.json`.

## Central Product Policy

`config/drivers.json` contains one entry for every driver settings directory:

```json
{
  "name": "hello_c",
  "path": "demo/hello_c",
  "enabled": true,
  "artifact": "drv",
  "load_stage": "runtime",
  "load_policy": "manual"
}
```

- `enabled=false` produces and activates nothing;
- `artifact` is `linked` or `drv`;
- `load_stage` is `boot`, `kernel`, or `runtime`;
- `load_policy` is `automatic` or `manual`.

The machine-readable schema is
`config/schemas/drivers-policy.schema.json`.

## Allowed Combinations

The seven allowed combinations are linked automatic at all three stages,
packaged automatic at all three stages, and packaged runtime manual. All
linked-manual, boot-manual, and kernel-manual entries are rejected.

Dependencies must resolve to known policy identities, must not point to a later
stage, and must not form a cycle. An enabled driver cannot depend on a disabled
driver, and an automatic driver cannot depend on a manual driver.

## Validation

```sh
make test-driver-policy
```

The validator rejects duplicate JSON keys, schema/field/type errors, unsafe or
missing paths, missing local files, central/local name mismatch, duplicate or
unlisted projects, unsupported artifact capabilities, forbidden combinations,
and dependency violations.

Since Phase 3.6C, `tools/gen_driver_build.py` turns the central policy into the
Make build lists, linked activation registry, and packaged activation plans.
Domain-local `driver.json` compatibility manifests are no longer build inputs.
The local `settings.json` supplies package and linked-integration facts; it
never selects the product artifact or activation stage.
