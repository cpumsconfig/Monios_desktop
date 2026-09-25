# Storage reliability follow-up

The cache now buffers complete 4096-byte blocks and sends partial-block updates
to the supplied offset-aware backend. This avoids overwriting untouched bytes
with zeros when the cache has no loader. Each buffered block retains its writer
callback, so later flushes do not depend on a subsequently registered default.

Failed and short writes retain dirty state. Cache drop, disable and shrink
return -1 if dirty data cannot be flushed; configuration stays unchanged.
Invalidation leaves failed dirty blocks resident. Callers must inspect
`fs_cache_info()->dirty_count` before treating a flush as durable. This does not
yet propagate device errors through every shutdown path or provide SMP locking.

Permission keys currently have a 127-byte storage limit. Longer keys, empty/null
keys and database delimiters are rejected, and failed lookups deny access for
non-root processes. chmod/chown propagate lookup errors. Path canonicalization,
case aliases and symlink identity still require a separate permissions design.

Validation: `python tests/run_security_tests.py`,
`tests\build_all_tests.bat`, and `mingw32-make out/kernel.unsigned.exe`.
The cache regression uses a simulated disk to check untouched bytes, cache
coherence, write failures, short writes, retry, and configuration transitions.
These host checks do not replace hardware or QEMU power-loss testing.

Follow-up: permission get/set now normalize paths with the VFS path resolver
before lookup. Dot components, parent components and drive-letter case no longer
create different permission keys. File-name case and symlinks remain unresolved.

`file_sync_all()` and `file_unmount_all()` now return bool. Known VFS writeback
failure prevents unmount and the FAT clean marker; shutdown/reboot cancel before
storage driver teardown. The power-state API queues the same checked kernel
shutdown path instead of calling ACPI directly. Active application shutdown may
already have occurred when cancellation is reported. Filesystem-private sync and
hardware flush error reporting are still incomplete.

EXT follow-up: the inode scratch buffer now holds a complete 4 KiB block, and
8 KiB block geometries are rejected because the driver caches hold only 4 KiB.
ATA sector writes report timeout, ERR, device-fault and LBA28 range errors.
Failed block flushes retain dirty data; `extfs_sync()` returns bool, propagated
through VFS sync and shutdown checks. Failed eviction does not replace the dirty
victim. If a new mutation cannot be retained, a sticky rejection flag prevents
subsequent sync from claiming success even after older blocks are retried.

That rejection is deliberately conservative: individual legacy EXT mutation
APIs still need full error propagation/rollback, and read-error propagation
remains outstanding. Fault-injection host
tests cover ERR/DRQ combinations, device fault, retry, out-of-range LBA and failed
eviction. This is not a claim of transactional or power-loss-safe EXT writes.

Device flush follow-up: EXT PIO block writeback now waits for both BSY and DRQ
to clear, then issues ATA FLUSH CACHE (0xE7). A block stays dirty if either data
completion or flush fails. Tests simulate flush ERR, flush timeout, stuck DRQ,
recovery/retry and clean sync without redundant flushes. This deliberately uses
one flush per dirty block, including eviction, prioritizing recovery over write
throughput. Devices rejecting FLUSH CACHE cause sync to fail conservatively.
This change covers the legacy EXT PIO path only; it does not add flush support
to all storage controllers or make multi-block filesystem updates atomic.
