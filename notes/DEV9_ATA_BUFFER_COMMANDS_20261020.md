# ATA diagnostic buffer commands

READ BUFFER E4h and WRITE BUFFER E8h now implement T13/1153D rev18
§8.22 and §8.44. Both transfer exactly one 512-byte sector and ignore media
address/count inputs. An accepted WRITE BUFFER is returned by READ BUFFER without
enqueuing or writing any guest-image sectors. IDENTIFY words82/85 bits12/13 now
describe the implemented commands.

The DATA port uses the existing 16-bit selection, DRQ and direction checks.
WRITE BUFFER polls its initial DRQ and interrupts once at completion; READ BUFFER
interrupts when data is ready. A dedicated completion callback bypasses all media
LBA progression and write queue handling. This avoids treating a diagnostic write
as a sector write or leaking an old DMA offset into it.

Reset, close and host-error cancellation clear the callback before releasing its
buffer. An incomplete WRITE BUFFER is not valid diagnostic data. READ BUFFER
without a completed predecessor aborts; reset invalidates the diagnostic contents.
Retaining completed contents across other ordinary commands is a benign extension
outside the draft's required immediately-preceding-WRITE prerequisite.

Three prospective integration cases use actual ATA DATA words and disposable
files: exact roundtrip/IRQ/length with an invalid media address and count zero;
partial/reset/retry; masked IRQ, wrong direction and unsupported width. They compare
the complete backing image and capability bits. They were written before the
production implementation, but remain uncompiled and unexecuted under the user's
no-compilation instruction. Source checks do not prove a runtime regression pass.
