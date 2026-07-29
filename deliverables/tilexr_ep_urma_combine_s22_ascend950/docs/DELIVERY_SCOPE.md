# Delivery Scope

## What is included

The `code/` tree preserves repository-relative paths and contains the complete
Combine-specific implementation plus every modified TileXR integration file
needed by this work:

- AIV kernel and shared protocol definitions;
- host launch, workspace layout, start-gate window, and public API;
- UDMA layout/transport integration and public UDMA structures;
- root, communication, operator, and demo CMake integration;
- Ascend 950 environment setup;
- variant builder, multihost runner, demo, strict/profile reporters;
- source guards, layout tests, start-gate tests, and report tests.

It is an overlay for TileXR base commit `db4303e`, not a duplicate of the full
TileXR repository or its CANN/third-party dependencies.

## What is intentionally excluded

- build directories and installation trees;
- historical variants, failed campaigns, and contaminated captures;
- machine-specific ports, systemd units, watchdogs, and `/tmp` paths;
- CANN, HCCP/RA, compiler, driver, and other third-party binaries;
- P36/S28 experimental code identities. The source supports other layouts,
  but the delivered default and evidence select S22.

The machine-bound `*_192.sh` campaign wrappers and old S1/P44 source-guard
fixtures are retained only to make the validated tests and experiment history
auditable. New users should use `scripts/build_s22_variant.sh`, the generic
multihost runner, and `scripts/render_s22_profile.sh`; the generic runner in this
delivery requires all host, device-map, remote-root, and communication endpoint
settings explicitly.

## Provenance

The validated operator build came from source SHA256
`51d7c1a771f958ff656a847d264c7708653ad1563119ff7184cdf120ba55962d` and
kernel-source SHA256
`8c59da19e23856c9a2ba8800d8cf612adedd9e9f9544d9cfbf7f04fe52ad3b47`.
The delivered operator/kernel sources preserve that implementation. The
delivered Python report tool and its unit test add only the presentation option
that collapses start gate from steady-state views, so the full delivery tree has
its own per-file hashes in `SOURCE_MANIFEST.tsv` and `SHA256SUMS`.

The included remote manifest is the validated profiling build used to create
the detailed report. Production builds must set profiling OFF and will
therefore have different binary hashes.
