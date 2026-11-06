# Phase 2 / Lesson 1 — Strict integer parsing with `std::expected`

## Contract

`parse_positive_int(std::string_view)` returns `std::expected<int, ParseError>`.
It accepts **ASCII decimal digits only**, with no whitespace or sign. Leading
zeros are allowed. The numeric value must be between 1 and `INT_MAX` inclusive.

| Input | Result |
|---|---|
| `"42"` | `42` |
| `"0042"` | `42` |
| `""` | `empty_input` |
| `" 42 "` | `invalid_character` |
| `"-1"` / `"+1"` | `invalid_character` |
| `"12x"` | `invalid_character` |
| `"0"` | `out_of_range` |
| Integer overflow | `out_of_range` |

If the input contains **any** non-digit, `invalid_character` takes precedence
over numeric overflow. This makes error classification deterministic regardless
of how many digits precede the invalid character.

## Rationale

- The parser performs no trimming or allocations, and does not throw.
- `std::from_chars` converts the digits without locale dependence.
- An explicit digit-validation pass gives precise `invalid_character` precedence.
  This second pass has a cost; do not infer performance without benchmarking.
- Failure is returned as a value. Accessing `.value()` on a failed
  `std::expected` would throw `std::bad_expected_access<ParseError>`.

## Run

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev -R '^parsing\.positive-int$' --output-on-failure
./build/dev/cpp/error_handling/cpp_mastery_parsing_tests --list
./build/dev/cpp/error_handling/cpp_mastery_parsing_tests
```

RED intentionally returns `empty_input` for every input. Observe the failures,
then apply GREEN and verify all 13 cases pass. The repository should register
13 CTest entries after this lesson, up from 12.

## Phase 2 / Lesson 2 — Error propagation without reclassification

`parse_zero_based_index(std::string_view)` composes the strict positive parser.
It returns `std::expected<int, ParseError>` and converts a one-based decimal index
from `[1, INT_MAX]` into a zero-based index from `[0, INT_MAX - 1]`.

- `"1"` yields `0`; `"0042"` yields `41`; `INT_MAX` yields `INT_MAX - 1`.
- Whitespace, signs, invalid characters, zero, and overflow are rejected by
  the existing parser **without changing the error category**.
- When parsing fails, the function returns `std::unexpected(parsed.error())`.
  Using `return parsed.error();` would not construct this result type, because
  `ParseError` is not implicitly convertible to `int`.
- On success, dereferencing `parsed` is safe because we checked `has_value()`.
  Subtracting one cannot underflow: the positive parser never returns zero.
- This wrapper performs no heap allocation and declares `noexcept`.

Run the focused CTest target with:

```sh
ctest --preset dev -R '^parsing\.zero-based-index$' --output-on-failure
```

The RED stub deliberately always reports `empty_input`, so most new tests
fail. GREEN propagates errors unchanged and transforms only successful values.
There are now 14 project CTest entries, including two parsing suites.

## Phase 2 / Lesson 3 — Error translation with preserved causes

`cpp_mastery::config::read_worker_count(std::string_view)` consumes the strict
positive integer parser from Lesson 1. The configuration domain requires a
worker count in **[1, 64]**, with exactly the same ASCII-digit-only grammar.

| Input | Result | Error origin |
|---|---|---|
| `"12"`, `"00064"` | `12`, `64` | success |
| `""` | `ConfigError{field, ParseError::empty_input}` | parsing |
| `"0"` | `ConfigError{field, ParseError::out_of_range}` | parsing |
| `"12x"` | `ConfigError{field, ParseError::invalid_character}` | parsing |
| `"65"` | `ConfigError{field, ConfigConstraintError::worker_count_above_maximum}` | domain |
| `"99999999999999999999"` | `ConfigError{field, ParseError::out_of_range}` | parsing |

The `ConfigError::cause` field is a `std::variant<ParseError, ConfigConstraintError>`.
It holds **exactly one** of these alternatives; callers can use
`std::get_if<ParseError>(&error.cause)` to inspect the original parsing cause,
or `std::get_if<ConfigConstraintError>(&error.cause)` for a domain limit error.
A variant avoids an inconsistent state such as a domain-limit error with a
spurious parse cause.

`ConfigError::field` is a non-owning `std::string_view` with a deliberate lifetime
contract: this API always returns the view into the **string literal**
`"worker_threads"`. The error never borrows the caller's potentially temporary
input string; a caller can retain the error after discarding the input.

The wrapper performs no dynamic allocation and is `noexcept`. Its first layer
parses and classifies syntax/range errors; its second layer enforces the domain
maximum. It never maps an otherwise valid but excessive worker count to
`ParseError::out_of_range` because that would lose the cause's provenance.

Run the additional CTest entry with:

```sh
ctest --preset dev -R '^config\.worker-count$' --output-on-failure
```

RED deliberately returns `empty_input` for all inputs; GREEN implements the
composition. After Lesson 3, 15 project CTest entries should exist.

## Phase 2 / Lesson 4 — Atomic configuration construction

`read_render_distance(std::string_view)` uses the strict parser and enforces the
independent **[1, 32]** domain limit. Its `ConfigError::field` is always the
static literal `"render_distance"`. `ConfigConstraintError` and `ConfigError`
now live in a shared `ConfigError.hpp` header, so the two validators do not
depend on one another's declarations.

`read_engine_config(workers, distance)` validates **both** fields and returns
`std::expected<EngineConfig, ConfigErrors>`, where `ConfigErrors` is
`std::vector<ConfigError>`. Its contract is:

- On success, return a complete `EngineConfig{worker_threads, render_distance}`.
  The object is constructed only after both validators succeed.
- On failure, return **all** independently detected errors, ordered first by
  `worker_threads`, then by `render_distance`.
- Preserve the original parsing or domain cause, without replacing one with the other.
- Never borrow the input storage in diagnostics. Field views refer to static literals.
- Unlike the individual no-allocation validators, the vector-based builder is
  **not `noexcept`**: collecting errors may throw (for example, `std::bad_alloc`).

| Workers | Distance | Result |
|---|---|---|
| `"8"` | `"24"` | `EngineConfig{8, 24}` |
| `"64"` | `"32"` | `EngineConfig{64, 32}` |
| `"65"` | `"12x"` | worker limit, then invalid character |
| `"0"` | `"33"` | zero out of range, then render limit |
| `"8"` | `"12x"` | only the render-distance parsing error |

RED deliberately uses incorrect placeholders so both new suites fail.
GREEN composes `read_worker_count` and `read_render_distance`, collects
errors in a deterministic order, and only then creates the valid object.
No default or partial `EngineConfig` escapes.

Run the two new suites with:

```sh
ctest --preset gcc16 -R '^config\.(render-distance|engine-config)$' --output-on-failure
```

After this lesson, the complete repository should register **17 CTest suites**.

## Phase 2 / Lesson 5 — Strong exception guarantee and RAII

`runtime::RuntimeState` owns a pair of render buffers and committed runtime settings.
The `ResourceFactory` seam supports deterministic failure injection. The included
`CpuResourceFactory` allocates real CPU-backed byte storage as a lightweight
substitute for GPU resources; it does not call any GPU API.

- Prepare two resources into a local `ResourceSet` owned by `std::unique_ptr`.
- If the second allocation fails, RAII automatically frees the first.
- The original settings and both original resource handles remain unchanged.
- Commit only after all allocations succeed: non-throwing `unique_ptr::swap`
  followed by non-throwing assignment of two integers.
- After swapping, the staging object's destructor releases the old resources.
- The factory is assumed not to mutate the active runtime as an external side
  effect. That limitation matters for real GPU queues and worker pools.

The RED implementation intentionally updates `worker_threads` before preparation.
GREEN moves that assignment into the non-throwing commit.

Run `ctest --preset gcc16 -R '^runtime\.transaction$' --output-on-failure`.
The completed project registers **18** CTest entries.

## Phase 2 / Lesson 6 — Parked worker pools and external side effects

`runtime::WorkerRuntime` stages CPU-backed render resources together with a new
`ParkedWorkerPool`. Each staged `std::jthread` waits at an atomic gate, with a stop callback
that cancels the whole staged pool if construction fails.
The pool constructor waits for each launched thread to park, so the test hook can
reliably simulate failure at the **fifth thread** after four have parked.

- On failed staging, `std::jthread` destruction requests stop, changes the atomic gate to cancelled, wakes the
  atomic wait, and joins every previously started thread.
  The staged render resources are also destroyed through RAII.
- RED publishes the new render state **before** starting the new worker pool;
  a launch failure leaves new settings with the old workers.
- GREEN prepares a complete bundle first and publishes it via a non-throwing
  `std::unique_ptr::swap`. Only **after** publication does `activate()` release
  the parked workers (atomic store and `notify_all`).
- The worker task is a **simulated** atomic counter increment. Real tasks may
  have irreversible external effects and cannot simply be rolled back.
- This is a **single-controller**, non-concurrent-access model: the bundle swap
  does not make concurrent readers safe. A real engine requires synchronized
  publication and explicit worker lifecycle and task-draining policies.
- The launch hook throws before constructing the selected `std::jthread` to
  deterministically emulate an operating-system thread-creation failure.

Run `ctest --preset gcc16 -R '^runtime\.worker-pool$' --output-on-failure`.
After this lesson the project registers **19** CTest entries.

## Phase 2 / Lesson 7 — Immutable snapshots for concurrent readers

`SnapshotRuntime` is a deliberately **separate** single-writer publisher. It
uses `std::atomic<std::shared_ptr<const RuntimeSnapshot>>` for publication and
reader ownership. Readers call `acquire()` and retain the returned handle while
reading settings and render-buffer metadata. The snapshot is immutable after
construction; its CPU-backed resources remain alive until the last reader lets go.

- Resource preparation may throw and is never published partially.
- Publish a complete snapshot with release semantics; acquire with acquire
  semantics. A reader can see the old or new snapshot, never a half-built one.
- If a factory throws or returns a null buffer, the published snapshot remains
  unchanged, and staging is destroyed through RAII.
- The tests use an intentionally blocked second allocation to expose the RED
  publication gap deterministically, without relying on scheduling luck.
- An immutable snapshot prevents data races on the settings and buffer metadata,
  but the atomic `shared_ptr` is **not necessarily lock-free**.
- This component does not activate, stop, or retire worker pools; GPU command
  completion and deferred GPU destruction also remain out of scope.
- Updates are single-writer; multiple concurrent writers would need additional
  serialization or compare-and-exchange logic to prevent lost updates.

RED clears the current snapshot before preparing the replacement; GREEN stages
the replacement first, then publishes it as one atomic operation.

Run `ctest --preset gcc16 -R '^runtime\.snapshot-publication$' --output-on-failure`.
After Lesson 7 the full repository registers **20** CTest suites.

## Phase 2 / Lesson 8 — Deferred resource retirement with a GPU timeline

`GpuRetirementQueue` is a **CPU-only simulation** of one ordered GPU submission
domain. It does not send GPU commands or use Vulkan. A single controller uses
`submit(snapshot)` to register a submission and its owning snapshot; only
after preventing *all new submissions* referencing that snapshot may it call
`retire(snapshot)`. Retire seals the snapshot against later submits, even
when collection has removed its GPU record but CPU readers still own it.
Weak tombstones retain control-block identity without extending resource lifetime.

`complete_through(serial)` advances a monotone, contiguous completion high-water
mark, bounded by the last issued submission ID. `collect()` releases only
retired snapshots whose **last** recorded submission is completed. Collection
works with out-of-order retirement registrations; a late retirement with no
GPU use is immediately eligible. CPU readers' shared pointers remain valid
independently of GPU completion. The queue owns its tracked snapshots until
collection, even if the publisher and its readers release their references.

Safety boundaries: this is a **single-controller API**, not thread-safe.
Callers must synchronize readers / submissions with the retirement boundary.
A reader that acquired an old snapshot must not submit GPU work after its
retirement; the queue rejects a late submit, but it cannot enforce a complete
external GPU submission protocol. One completion high-water mark is correct
only for one ordered domain; independent queues need separate dependencies.
For real Vulkan, timeline semaphore/fence completion must be queried from the
actual queue, and shutdown must drain in-flight uses before destroying the
retirement manager. Neither ASan nor UBSan substitutes for that synchronization.
`std::shared_ptr` also does not protect the GPU if a command referencing a raw
GPU resource escaped registration.

RED intentionally collects all retired snapshots even when GPU submissions
are outstanding. GREEN fixes the eligibility test while retaining the same
public API, error checks, and ownership contracts.

Run `ctest --preset gcc16 -R '^runtime\.gpu-retirement$' --output-on-failure`.
The completed repository should register **21** CTest suites.

## Phase 2 / Lesson 9 — Multiple independent GPU completion timelines

`runtime::MultiQueueGpuRetirementQueue` is a separate CPU-only simulation of
**graphics, compute, and transfer**, each with its own ordered submission and
completion serial. Submission 12 on graphics cannot be compared with submission
7 on compute. The queue tracks each snapshot's LAST use independently in all
three domains. Zero means it was never used on that queue.

- RED intentionally releases a retired snapshot when **any** used queue has
  completed. Seven tests expose premature destruction.
- GREEN requires **every used queue** to have completed its last submission.
  Unused queues impose no completion requirement.
- CPU `shared_ptr` readers may keep the snapshot alive after the queue drops
  its own reference. Weak tombstones reject late submission of a retired
  snapshot while a CPU owner still holds it.
- `submit` advances only the selected queue's counter, after any required
  record allocation succeeds. `complete_through` rejects regression and
  completion beyond the corresponding queue's submitted serial.
- `retire` seals the snapshot. Its two potentially allocating steps use a
  rollback path so allocation failure leaves the registration unchanged.
- Single-controller API: callers serialize submissions, retirement and
  completion reports, and stop new submissions before `retire`.
- This is NOT Vulkan: the caller must supply truthful completion events. Real
  cross-queue hazards also require explicit synchronization, memory barriers,
  queue-family ownership handling, and coordinated shutdown before queue teardown.

Run `ctest --preset gcc16 -R '^runtime\.multi-queue-retirement$' --output-on-failure`.
After Lesson 9, the project registers **22** CTest suites.

## Phase 2 / Lesson 10 — Transfer-to-graphics execution and ownership handoff

`CrossQueueHandoff` is a separate CPU-only model for one mesh buffer, transfer
writes, and graphics reads. A transfer timeline signal and graphics wait model
an execution AND memory dependency; the wait must cover the vertex-input stage.
A wait at `fragment_shader` does not cover a vertex-input read.

- For `HandoffSharing::concurrent`, ownership handoff barriers are unnecessary,
  but semaphore signal/wait synchronization is still required.
- For `HandoffSharing::exclusive` with the same queue-family index, no queue-family
  ownership transfer is required, but cross-queue execution ordering is still needed.
- For exclusive sharing across different queue families, a matching release and
  acquire are mandatory, ordered by a signal/wait pair. This simplified model
  uses `all_commands` for the wait scope on ownership transfers, matching the
  default path without Vulkan's optional ownership-transfer-all-stages extension.
- An additional transfer write invalidates the prior synchronization and
  ownership records until a NEW signal, wait, release, and acquire are made.
- A pending or invalid handoff causes `graphics_read()` to throw rather than
  claim that a read is safe. The simulator does not execute GPU commands.
- Only a single controller may call this model's methods. This lesson models
  no graphics-to-transfer return handoff, memory-range tracking, image layouts,
  GPU completion, memory barriers beyond the abstract handoff, or Vulkan APIs.
- Retirement queues in Lessons 8/9 are distinct from this synchronization model.

RED deliberately treats any signaled/waited transfer value as sufficient, even
if the wait scope or ownership handoff is missing. GREEN checks all requirements.

Run `ctest --preset gcc16 -R '^runtime\.cross-queue-handoff$' --output-on-failure`.
After Lesson 10 the repository registers **23** CTest suites.

## Phase 2 / Lesson 11 — Coordinated submission admission

`CoordinatedRuntime` composes immutable `RuntimeSnapshot` ownership, the Lesson 9
`MultiQueueGpuRetirementQueue`, and a Lesson 10 `CrossQueueHandoff` readiness check.
One synchronized admission protocol protects a snapshot between selection and GPU
submission registration:

- `acquire()` gives a CPU-only immutable snapshot. It is **not** GPU permission.
- `admit()` records an outstanding ticket under the same mutex as publication.
- `publish()` stages new resources privately and publishes them atomically. Old
  generations remain tracked while tickets are outstanding.
- A successful `Admission::submit(queue)` records a modeled accepted GPU use and
  consumes its ticket; a failed submission retains the ticket for retry/cancellation.
- `Admission::cancel()` and its destructor release pending tickets without issuing
  phantom GPU submissions. An old generation is sealed only after all tickets
  resolve. Retirement then observes each queue's independent completion timeline.
- `submit_graphics_after(handoff)` requires the Lesson 10 logical cross-queue
  synchronization precondition before registering a graphics use. This is a
  CPU teaching check, **not** a real Vulkan submit or ownership transfer.

`CoordinatedRuntime` is single-writer for `publish()`, but readers and admission
operations may run concurrently. A ticket retains its internal coordinator state,
so cancellation is safe even if the public controller has been destroyed.

The mock `submit()` is the **acceptance boundary**. A real Vulkan integration
would need another reservation/rollback protocol around a fallible `vkQueueSubmit`.
The simulation does not execute GPU work and has no real device-loss or shutdown
semantics. The owner must ensure real GPU operations are quiescent before actual
resource destruction. Do not independently submit snapshots through a separate
retirement queue, since that bypasses this controller's admission lock.

RED seals retired snapshots before previously granted admissions resolve. GREEN
waits for the pending count to reach zero before sealing.

Run `ctest --preset gcc16 -R '^runtime\.coordinated-pipeline$' --output-on-failure`.
After Lesson 11 the project registers **24** CTest suites.

## Phase 2 / Lesson 12 — Submission failure recovery without fictional GPU work

`SubmissionRecoveryRuntime` composes Lesson 11's `CoordinatedRuntime` admission
with a CPU-only backend that reports whether a simulated GPU submit was accepted.
The model distinguishes four outcomes:

| Backend result | Action | GPU serial issued? |
|---|---|---|
| `accepted` | Register the admitted use on its queue | Yes, on successful registration |
| `not_enqueued` | Cancel the admission | No |
| `unknown` / backend throws | Quarantine the admission for explicit resolution | No, until acceptance is confirmed |
| `device_lost` | Quarantine the attempt; enter fail-closed state | No new serial |

**RED bug:** it treats `unknown` as a confirmed rejection and cancels the
admission prematurely. A later publication may retire resources while GPU
acceptance is still unverified. **GREEN** retains a move-only admission in an
incident record. Only `confirm_not_enqueued(id)` may cancel it; otherwise
`confirm_accepted(id)` registers the GPU use. An incident can be resolved once.

- Preallocate recovery-record capacity *before* calling the backend. If backend
  acceptance is confirmed but registration throws, retain the admission as a
  `registration_pending` incident. Retrying registration never resubmits work
  to the backend. An accepted incident cannot be marked `not_enqueued`.
- The wrapper is single-controller, but it composes with the coordinator's
  serialized publication/submission protocol. Backend callbacks must not
  recursively access the wrapper; no actual driver call takes place.
- Uncertain outcomes remain unresolved until external evidence establishes
  acceptance or rejection. A thrown exception is **not** proof of rejection.
- `device_lost` blocks further admissions, publications, completions, and
  collections. This lesson does not implement device reset/recreation or
  establish device quiescence; callers must not destroy real GPU resources on
  the strength of this simulation alone.
- Failure *before* a backend call (for example, bookkeeping allocation) causes
  no external submission. The moved ticket is safely cancelled by RAII.
- As in previous lessons, retained CPU readers can outlive collection.

Fast local RED/GREEN check:

```sh
cmake --preset gcc16
cmake --build --preset gcc16
ctest --preset gcc16 -R '^runtime\.submission-recovery$' --output-on-failure
```

After GREEN, run the existing complete GCC suite and focused runtime sanitizers;
there is no need to use `--clean-first` or rebuild four toolchains locally.
The full repository should register **25 CTest suites** after Lesson 12.

## Phase 2 / Lesson 13 — Device loss, quarantine, and backend generations

`DeviceRecoveryRuntime` adds a CPU-only recovery coordinator around Lesson 12.
Device loss **freezes** submission, publication, incident resolution, and
completion changes. Quarantined ownership stays alive even if an incident's
backend acceptance remains unknown. `collect()` returns zero while frozen.

- `BackendGeneration` identifies the backend that owns a submission timeline.
  A new generation starts fresh serials; stale completions and submissions
  must be rejected, even if their numeric serial is the same.
- Only a **trusted** `SafeTeardownAuthority` may confirm that old GPU resource
  use has ended for the *specific* frozen generation. That authority is a
  simulated external proof boundary, **not a Vulkan API implementation**.
- RED deliberately skips this proof and can destroy quarantined GPU owners
  prematurely. GREEN checks it before preparing and publishing a replacement.
- A failed proof, throwing authority, or failed new-resource preparation leaves
  the old generation frozen, with its incidents and resources intact.
- An accepted proof permits dropping only the coordinator's ownership; any
  remaining CPU `shared_ptr` reader still extends the snapshot lifetime.
- One controller serializes the wrapper's calls. Backend object identity, real
  Vulkan device-loss teardown guarantees, and physical GPU memory deallocation
  are the responsibility of a future backend adapter. In particular, the
  application must not destroy a quarantined real GPU controller before its
  safe teardown has been confirmed; this CPU simulator cannot enforce process
  lifetime against premature owner destruction.

Run the focused suite with:

```sh
ctest --preset gcc16 -R '^runtime\.device-recovery$' --output-on-failure
```

After GREEN, expect **26** repository CTest suites. For efficient validation,
run the full `gcc16` suite plus `gcc16-sanitize -R '^runtime\.'`; avoid
`--clean-first` unless a full rebuild is specifically needed.

## Phase 2 / Lesson 14 — Transactional backend reconstruction

`TransactionalBackendRuntime` extends device-loss recovery to a staged backend
object and the two snapshot resources. The `ReconstructionBuilder` creates an
unpublished backend, and `LeasedFactory` wraps its buffers with owning backend
leases. This keeps the backend object alive while an old CPU reader retains a
snapshot, even after a newer generation is published.

- Verify safe teardown for the **specific old generation** before constructing
  the replacement. The authority is consulted once per attempt.
- Prepare an entire new backend and both snapshot resources before committing.
  Backend creation, geometry allocation, and lighting allocation may all throw.
- If preparation fails, RAII releases the staged geometry and backend; the old
  generation, pending admissions and unresolved incidents remain quarantined.
- On success, commit via the existing coordinator, then swap the backend pointer
  without throwing. New submission serials are separate from old generations.
- A rejected/throwing authority, null backend, incorrect generation identity,
  or null resource prevents publication. A failed attempt can be retried.

The RED implementation commits the next generation using the *old* backend
before it validates the new backend's two resources. The GREEN implementation
uses the newly staged backend as the factory during the coordinator's
transactional rebuild. Expect the RED suite to fail intentionally.

**Safety boundary:** this is a CPU-only teaching model, not a Vulkan implementation.
A real GPU backend must prove safe teardown through documented guarantees.
The owner must remain alive during quarantine; a `shared_ptr` alone does not
prove an actual GPU is idle. The model assumes a serialized controller and
backend builders with no irreversible external effects before publication.

Run `ctest --preset gcc16 -R '^runtime\.backend-reconstruction$' --output-on-failure`.
After GREEN, the full repository registers **27 CTest suites**, including
**10 runtime suites**.

## Phase 2 / Lesson 15 — Activation failure is not ordinary RAII rollback

`ActivationRecoveryRuntime` prepares a backend and both resources before attempting
activation. A confirmed **not_started** outcome discards staging and leaves the old
generation quarantined. The RED version incorrectly commits a generation even
when activation never started. GREEN requires affirmative activation success.

- A failed build or either resource allocation happens before activation; RAII
  cleans up the staged resources, without changing generation identity.
- An activation exception is **unknown**, not confirmed failure. The wrapper
  retains the staged backend and resources, blocks retry, and requires external
  resolution before publishing or discarding them.
- A confirmed started activation commits the fully prepared new generation. If
  coordinator registration throws, the staged backend remains retained for a
  subsequent confirmed-started resolution/retry.
- Generation-specific safe teardown authority is required before preparation.
  An old CPU snapshot may keep its former backend alive via existing leases.
- This CPU-only model assumes one serialized controller and a trusted external
  resolution for activation. Real GPU activation may perform irreversible work,
  which cannot be undone merely by destroying a C++ object. Keep this controller
  alive during uncertainty; its destructor is NOT a real device teardown proof.
- A successfully running initial backend is an explicit constructor precondition.

Focused test: `ctest --preset gcc16 -R '^runtime\.activation-recovery$' --output-on-failure`.
After GREEN, the complete repository registers **28 CTest suites** and 11 runtime suites.

## Phase 2 / Lesson 16 — Idempotent recovery activation requests

`IdempotentActivationRuntime` wraps Lesson 15 with a **caller-supplied, nonzero
attempt ID**. A replay of the same ID and original input returns its previously
observed status without rebuilding a backend, contacting the teardown authority,
or calling `activate()` again. Different inputs with the same ID are rejected.

- **Pending**: unknown activation outcome; same-key retries report pending, while
  other keys are blocked. Resolve the original attempt with external evidence.
- **Committed**: replay returns the originally committed backend generation,
  even after newer generations have been committed.
- **Not started**: a confirmed no-start or failed preparation records a terminal
  identity; a real new attempt **must use a new ID**.
- A resolution of the same outcome is idempotent; contradictory resolutions fail.
  A failed commit during resolution preserves the pending identity for retry.
- Bookkeeping is reserved before invoking external recovery. These records grow
  for the lifetime of the coordinator; production code needs a bounded/durable
  history policy. This course guarantees **in-process at-most-once dispatch per
  attempt ID**, not exactly-once behavior across crashes or independent workers.
- The real backend must supply authoritative activation outcomes. Device loss or
  a thrown activation call does not prove that external work did not start.
- The owner must retain the controller while a possibly active backend remains
  pending. The wrapper is deliberately single-controller, not multiwriter-safe.

RED intentionally rejects same-ID replays while pending/committed and invokes
activation twice when a confirmed no-start request is retried under the same ID.
GREEN returns the recorded result and avoids duplicate external activation.

Run: `ctest --preset gcc16 -R '^runtime\.idempotent-activation$' --output-on-failure`.
The completed project registers **29 CTest suites** (12 runtime suites).

## Phase 2 / Lesson 17 — Durable activation journal and crash-safe replay

`DurableActivationJournal` adds an append-only **write-ahead intent record** to
Lesson 16's stable activation attempt IDs. This is a separate CPU-only
**activation dispatch boundary**, not a restored Vulkan/GPU runtime. Each record
contains the caller-supplied ID, requested backend generation, configuration,
and a checksum. Its `I`, `S`, and `N` records mean intent, confirmed started,
and confirmed not started, respectively.

- A new `I` record is written and `fsync()`'d **before** calling the backend.
  Creating the journal also requires an `fsync()` of its parent directory.
- If the activation result is known, append and `fsync()` its terminal `S` or
  `N` record. An exception or unknown result leaves the `I` entry pending.
- After restart, a matching pending request **does not call activate() again**.
  Reconcile its outcome with an independent trusted authority and use
  `confirm()`. Conflicting IDs or confirmations are refused.
- Injected failures before intent writing cannot call the backend. Failures
  before the terminal record leave the durable intent pending, not successful.
- An exclusive advisory Linux file lock prevents simultaneous writers. Torn,
  corrupt, or invalid state-transition records fail closed; there is deliberately
  no automatic truncation/repair. This lab has an explicit 4 MB log cap and no
  compaction, multi-process coordinator, cross-filesystem guarantees, or
  recovery from storage hardware violating `fsync()` guarantees.

**Scope:** a journal status of `started` is **not** proof that a new live GPU
backend was committed or that an old one can be destroyed. Real reconstruction
still requires authoritative teardown and reconciliation from Lessons 13–16.
`JournalActivationBackend` must correlate the stable ID with its external
activation; without authoritative backend deduplication, exactly-once effects
across machine failure cannot be promised. Keep the journal's directory on a
local filesystem with appropriate durability guarantees; do not use an
untrusted NFS mount as a substitute for a durable coordinator.

RED deliberately dispatches activation before persisting its intent and calls
activation again when a pending ID is replayed. It must **compile with `-Werror`**
and fail through deterministic assertions only. GREEN writes intent first and
returns recorded states without replaying side effects.

Focused test: `ctest --preset gcc16 -R '^runtime\.durable-journal$' --output-on-failure`.
After GREEN, the complete repository registers **30 CTest suites** (13 runtime suites).

## Phase 2 / Lesson 18 — Authoritative reconciliation after a crash

`AuthoritativeActivationReconciler` consumes Lesson 17's **durable intent** and
queries a trusted `ActivationStatusAuthority` by the recorded attempt ID and
backend generation. It models a CPU-only crash-recovery decision: it does **not**
reconstruct a Vulkan device, adopt live GPU objects, or certify teardown safety.

- A missing attempt ID is rejected without querying the authority. A terminal
  journal entry is replayed without querying or performing activation again.
- A pending attempt can transition to `started` or `not_started` **only** if the
  authority replies with the matching ID and generation and explicit evidence.
  This transition uses the journal's `fsync()`-backed `confirm()` method.
- `unavailable` status or an authority exception retains the pending intent.
  It never triggers automatic activation or authorizes another request ID.
- A stale, mismatched or invalid reply is rejected without changing the journal.
  A disk failure while persisting confirmation also leaves the attempt pending.
- Once authoritative negative acknowledgement is durably recorded, the journal
  permits a **new** attempt ID; it never reuses the original ID to issue a new
  activation. A started result means activation started, not that the backend's
  GPU resources can be reused or destroyed.

RED intentionally maps `unavailable` to `not_started`, demonstrating how a
transient query outage can incorrectly authorize duplicate external work.
GREEN remains pending until evidence arrives. Both stages must compile under
strict `-Werror`, and RED must fail only through deterministic assertions.

Run: `ctest --preset gcc16 -R '^runtime\.authoritative-reconciliation$' --output-on-failure`.
The completed project registers **31 CTest suites** (14 runtime suites).
