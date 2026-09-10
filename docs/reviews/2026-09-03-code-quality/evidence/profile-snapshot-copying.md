# Profile and undo snapshot copying

Captured 2026-09-10 for MQ-03.

## Workload and thresholds

`sokoban_profile_tests --benchmark-profile-snapshots` builds a late-level
profile with 24 levels, 240 screen records, and 2,048 persisted undo actions.
Each action contains the full transition for one player, 64 movable blocks,
and 32 enemies. The executable replaces its global C++ allocation functions
to measure requested live and peak heap bytes across worker threads. The byte
figures exclude allocator metadata and allocations made directly through C
APIs.

The experiment reports the cost of one gameplay-snapshot copy, checkpoint
ownership transfer, profile serialization, deferred request submission, and
urgent request plus durability. An avoidable snapshot copy was considered
material at either 4 ms of caller time or 8 MiB of retained/transient payload.
The undo contract remained unchanged: every action, presentation timeline,
state transition, move count, and final state must survive save and restore.

## Baseline

The original profile path made three deep copies around a checkpoint:

1. `GameplaySession::snapshot` copied the live state and undo history.
2. `CampaignSession::writeCheckpoint` copied that result into the profile.
3. `AsyncSaveStore::requestSave` copied the live profile into worker-owned
   storage.

The third copy is required because the worker must not observe later mutation
of the live profile. Serialization then made another complete `PlayerProfile`
copy solely to normalize small progress and settings fields. Finally, the JSON
schema stored each action's `after` state again as the following action's
`before` state and emitted whitespace-formatted output.

One Release baseline run produced:

| Measurement | Baseline |
| --- | ---: |
| Snapshot copy | 6,341 us / 14,224,711 B peak and retained |
| Serialization | 3,638,688 us / 449,685,495 B peak |
| Serialized document | 111,358,765 B |
| Deferred request | 4,820 us / 14,236,654 B peak and retained |
| Urgent request return | 4,827 us |
| Urgent request through durability | 7,719,073 us / 845,274,510 B peak |

The snapshot copies exceeded both thresholds. The 106.2 MiB document and
806.1 MiB urgent-save peak also showed that duplicate persisted states, rather
than asynchronous scheduling, dominated the stress case.

## Changes

`CampaignSession::writeCheckpoint` now accepts a snapshot by value and moves it
into the selected checkpoint. The application's temporary snapshot therefore
transfers its undo storage into the profile with no allocation. Existing
lvalue callers retain normal copy semantics.

Serialization now normalizes a lightweight projection containing only scalar
metadata, level/screen records, and settings. It borrows the immutable active
and overworld checkpoints instead of copying either undo history. Profile JSON
is compact because save files are machine-owned and can contain large histories.

Profile format 28 replaces each duplicated action `before` state with one
`undoBaseState` at the session boundary. Every action still stores its `after`
state and all action metadata. Decoding reconstructs the exact in-memory chain
before gameplay validation. The 27-to-28 migration extracts the first legacy
`before` state and removes all redundant copies. Strict parsing rejects a
missing base, a null base for a non-empty history, and legacy properties in a
current document. A format-27 history with motion and animation presentation
data is required to round-trip exactly.

## Result

Three Release runs of the final implementation produced:

| Run | Snapshot copy | Checkpoint transfer | Serialization | Serialized bytes | Deferred request | Urgent return | Urgent durable | Serialization peak | Urgent peak |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5,967 us | 7 us / 0 B | 1,659,132 us | 14,547,384 B | 5,657 us | 4,197 us | 2,850,971 us | 183,618,604 B | 382,935,275 B |
| 2 | 4,963 us | 5 us / 0 B | 1,486,749 us | 14,547,384 B | 4,953 us | 3,049 us | 3,010,126 us | 183,618,604 B | 382,935,275 B |
| 3 | 5,613 us | 5 us / 0 B | 1,771,786 us | 14,547,384 B | 4,808 us | 3,177 us | 3,410,393 us | 183,618,604 B | 382,935,275 B |

The final document is 86.9 percent smaller than the baseline. Serialization
peak falls 59.2 percent and urgent-save peak falls 54.7 percent. Median
serialization time falls 54.4 percent, while median time through urgent
durability falls 61.0 percent. Checkpoint transfer retains the snapshot's
existing vector storage and allocates zero bytes.

One 13.6 MiB profile copy remains at asynchronous request submission. That is
the deliberate isolation boundary between the live profile and its worker.
Removing it would require shared immutable profile ownership or main-thread
serialization, neither of which improves this path without broadening lifetime
risk or caller latency. The request returns in 3.0-5.7 ms in this stress case;
serialization, validation, and disk replacement remain on the worker.

Verification:

- Player-profile suite, including current-schema and format-27 migration
  coverage, passed in Debug and Release.
- Save-slot lifecycle suite: 133 checks passed in Debug and Release.
- Full Debug and Release warning-as-error builds.
- Debug CTest registry: 80 of 80 passed.
- Release CTest registry: 80 of 80 passed.

MQ-03 is complete. The forward-looking roadmap begins with MQ-04.
