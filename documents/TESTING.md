# SLADE Mobile — Regression Baseline

> R0 baseline for the current WAD MVP. This document defines the behavior that must remain stable while the architecture is being refactored.

## 1. Purpose

R0 is a behavioral baseline, not a new feature set. Before R1 or later structural changes, the existing WAD workflow must be reproducible and its results must be verifiable after reopening the archive.

The baseline deliberately separates:

- **build verification** — the project can be built for the supported Android ABI;
- **workflow verification** — the existing UI/native workflow still works;
- **persistence verification** — edits survive Save/Save As and reopening;
- **safety verification** — Discard and IWAD protection behave as expected.

R0 does not require a new editor architecture, PK3/ZIP support, or Undo/Redo.

## 2. Environment

Record the environment used for a baseline run:

| Item | Value |
|---|---|
| Device / emulator | |
| Android version | |
| ABI | `arm64-v8a` |
| Build variant | `debug` |
| Source commit | |
| Test WAD | |
| Test WAD SHA-256 | |
| Date | |

The exact WAD fixture should be retained for repeatable runs. If a different fixture is used, record its checksum and expected entry count.

## 3. Build baseline

- [ ] `debug` APK builds successfully.
- [ ] Native C++ code compiles successfully for `arm64-v8a`.
- [ ] The application launches without a native library loading failure.
- [ ] Opening the test WAD does not produce a native crash.

A local/device build is the authoritative verification for this repository because the current project does not yet have a complete Android instrumentation/CI regression suite.

## 4. WAD workflow baseline

For every test below, verify both the immediate UI result and the reopened archive result where applicable.

### R0-01 — Open / Browse

**Action**

1. Open the test WAD through Android Storage Access Framework.
2. Browse the entry list.
3. Select representative entries.

**Expected**

- The WAD opens successfully.
- The expected entry count is displayed.
- Entry names and ordering are stable.
- No native crash occurs.

### R0-02 — Preview

**Action**

Preview representative supported resources from the fixture.

**Expected**

- Supported resources are decoded and displayed.
- Unsupported resources are rejected gracefully.
- Previewing an entry does not modify the archive or dirty state.

### R0-03 — Rename → Save As → Reopen

**Action**

1. Rename one editable entry.
2. Confirm the archive becomes dirty.
3. Save As to a new SAF document.
4. Close/reopen the saved WAD.

**Expected**

- The renamed entry is present under the new name.
- The output archive remains structurally valid.
- The original input WAD is unchanged.
- Reopening the output reproduces the rename.

### R0-04 — Delete → Save As → Reopen

**Action**

1. Delete one editable entry.
2. Save As to a new SAF document.
3. Reopen the output.

**Expected**

- The deleted entry is absent.
- The remaining entry order/content is preserved.
- The output archive opens successfully.

### R0-05 — Move → Save As → Reopen

**Action**

1. Move an editable entry to a different valid position.
2. Save As to a new SAF document.
3. Reopen the output.

**Expected**

- The entry appears at the requested position.
- Other entries retain their relative order unless the move necessarily changes it.
- The output archive opens successfully.

### R0-06 — Replace → Save → Reopen

**Action**

1. Replace an editable entry with replacement data of a compatible type.
2. Save using the existing save path.
3. Close/reopen the archive.

**Expected**

- Replacement data is present after reopening.
- The archive remains structurally valid.
- Dirty state clears after successful save.

### R0-07 — Discard Changes

**Action**

1. Make a visible archive edit.
2. Confirm the archive is dirty.
3. Invoke Discard Changes.
4. Reopen or inspect the archive again.

**Expected**

- In-memory edits are discarded.
- The source archive remains unchanged.
- The dirty state clears.
- The original entry data/order/names are restored.

### R0-08 — Save As does not mutate the source

**Action**

1. Record the source WAD checksum.
2. Make one or more edits.
3. Save As to another document.
4. Compare/reopen the source and destination independently.

**Expected**

- The source remains byte-for-byte unchanged unless it was explicitly saved in place.
- The destination contains the intended edits.

### R0-09 — In-place Save safety

**Action**

1. Open a PWAD that is allowed for in-place saving.
2. Make a supported edit.
3. Run the existing Save validation/commit flow.
4. Reopen the same document.

**Expected**

- Validation occurs before the original URI is opened with `rwt`.
- The resulting archive is structurally valid.
- The edit survives reopening.
- IWADs remain blocked from in-place save.

**Important limitation:** the current in-place save path is validated before commit but is not an atomic crash-safe replacement mechanism. R0 verifies the behavior that exists; it does not claim stronger guarantees.

## 5. Persistence invariants

For every successful save, verify as applicable:

- archive opens after reopening;
- entry count is correct;
- entry names are correct;
- entry order is correct after move/delete operations;
- modified entry sizes are correct;
- modified entry contents are correct;
- untouched entries remain unchanged;
- dirty state is cleared after successful save;
- a failed validation does not intentionally truncate the original before validation completes.

When practical, record SHA-256 checksums for the original and produced files. For modified archives, the exact output checksum is expected to differ; the useful invariant is structural/content correctness after reopening.

## 6. Evidence

For each baseline run, keep enough evidence to reproduce failures:

- source commit SHA;
- test fixture SHA-256;
- device/Android version;
- failing test ID;
- relevant entry name and expected/actual state;
- output archive, when it can be shared safely;
- logcat/native error information for crashes.

Do not treat a screenshot alone as proof of persistence. A save-related test is complete only after reopening and inspecting the resulting archive.

## 7. R0 exit criteria

R0 is complete when:

1. A debug build has been successfully produced.
2. R0-01 through R0-09 have been executed on a supported Android device/emulator, with results recorded.
3. Any discovered behavior mismatch is either fixed or explicitly documented as a known limitation.
4. The baseline fixture and its checksum are recorded.
5. The baseline commit is identified before starting R1.

Only after these criteria are met should structural refactoring begin.

## 8. Future automation

The manual baseline is intentionally the first step. Later work can turn the stable cases into automated native/Kotlin/instrumentation tests.

Candidate automated layers:

- native WAD serialization/reopen tests;
- Kotlin state/command tests;
- JNI contract tests;
- Android instrumentation tests for SAF workflows;
- CI build and smoke-test jobs.

Automation belongs to the later testing/CI phase unless a small test can be introduced without changing production behavior or creating speculative infrastructure.
