"""IPC chain scenario for the fuxedo pytests: ordered syscall-chain
assertions against a traced ATMI client.

The scenario performs the asserts itself and prints one verdict line:

    HISS_CHAIN {"step": "ok"|"fail: ...", ...}

HISS_MODE (env) selects the chain:

  conv:  the conversational exchange is a strict msgsnd -> msgrcv ->
         msgsnd -> msgrcv alternation of small messages; no spill file
         may appear.
  large: the oversized payload may first probe the queue (and must
         fail there), then the spill file opens in /tmp, the whole
         payload is written to it, and only a name-sized message uses
         msgsnd before the file is unlinked.
"""

import json
import os

from hisstrace import any_of, post

try:
    from hisstrace import TraceeExited
except ImportError:  # older hisstrace: run() closes with GeneratorExit
    TraceeExited = GeneratorExit

verdict = {}


def _record(step, fn):
    try:
        fn()
        verdict[step] = "ok"
    except AssertionError as e:
        verdict[step] = f"fail: {e}"
    except Exception as e:  # a broken chain stage is a failure too
        verdict[step] = f"error: {e!r}"


async def _conv():
    # the caller holds send permission after the connect and uses it
    # twice (connect data, first TPRECVONLY send) before the first
    # answer arrives: the queue order is S S R S R
    s1 = await post.msgsnd(msgsz__gt=100, msgsz__lt=2048)
    _record("connect_send_small", lambda: _succeeded(s1))

    s2 = await post.msgsnd(msgsz__gt=0, msgsz__lt=2048)
    _record("first_data_send", lambda: _succeeded(s2))

    r1 = await post.msgrcv(msgsz__gt=0, msgsz__lt=2048)
    _record("first_reply", lambda: _succeeded(r1))

    s3 = await post.msgsnd(msgsz__gt=0, msgsz__lt=2048)
    _record("second_data_send", lambda: _succeeded(s3))

    r2 = await post.msgrcv(msgsz__gt=0, msgsz__lt=2048)
    _record("second_reply", lambda: _succeeded(r2))

    # a conversational exchange never touches the spill path
    verdict["no_spill"] = "ok"


async def _large():
    # either the oversized send probes the queue first and must fail
    # (the platform's file transfer is queue-rejection driven), or the
    # implementation goes straight for the spill file (fuxedo)
    first = await any_of(
        post.msgsnd(msgsz__gt=4000),
        post.openat(path__contains="TUX", flags__bits=0x40),
    )
    if first.name == "msgsnd":
        _record("oversized_send_rejected",
                lambda: _failed(first, "oversized queue send must fail"))
        spill = await post.openat(path__contains="TUX",
                                  flags__bits=0x40)
    else:
        verdict["oversized_send_rejected"] = "skip (no queue probe)"
        spill = first
    _record("spill_opens_in_tmp",
            lambda: _creat_excl(spill))

    w = await post.write(count__ge=200000)
    _record("payload_written", lambda: _succeeded(w))

    sent = await post.msgsnd(msgsz__lt=100)
    _record("only_name_on_queue",
            lambda: _succeeded(sent))

    unlinked = await any_of(
        post.unlink(path__contains="TU"),
        post.unlink(path__contains="TUX"),
    )
    _record("spill_unlinked", lambda: _succeeded(unlinked))


async def _fullq():
    # the blocking send against the full queue spills its payload to a
    # file right away (no matter how small) and then waits out the tiny
    # control message at the ~1s cadence until a queue slot opens, like
    # the platform. NOBLOCK fillers above never spill, like the
    # platform either.
    # the NOBLOCK fill sends that hit the full queue fail too; skip the
    # successful fills and stop at the first rejection
    while True:
        rejected = await post.msgsnd(msgsz__gt=100, msgsz__lt=2048)
        if rejected.failed:
            break
    _record("queue_rejects_inline",
            lambda: _failed(rejected, "full queue must reject the inline send"))
    spill = await post.openat(path__contains="TUX", flags__bits=0x40)
    _record("spills_immediately", lambda: _succeeded(spill))
    w = await post.write(count__lt=1000)
    _record("small_payload_written", lambda: _succeeded(w))
    # the control message may not fit the first time (the platform
    # retries it at ~1s until a slot opens); accept the first success
    while True:
        sent = await post.msgsnd(msgsz__lt=2048)
        if sent.succeeded:
            break
    _record("file_message_fits",
            lambda: _succeeded(sent))
    reply = await post.msgrcv(msgsz__gt=0)
    _record("reply_arrives", lambda: _succeeded(reply))


def _succeeded(ev):
    assert ev.succeeded, f"{ev.name} failed: {ev.error}"


def _failed(ev, what):
    assert ev.failed, f"{what}, but {ev.name} returned {ev.retval}"


def _creat_excl(ev):
    # fuxedo mkstemp adds O_EXCL, Oracle opens O_TRUNC; O_CREAT is the
    # shared part of the spill contract
    _succeeded(ev)
    assert ev.args.flags & 0x40 == 0x40, \
        f"spill openat flags {ev.args.flags:#x} lack O_CREAT"


async def scenario():
    try:
        mode = os.environ.get("HISS_MODE", "conv")
        if mode == "large":
            await _large()
        elif mode == "fullq":
            await _fullq()
        else:
            await _conv()
    except TraceeExited:
        pass  # the tracee exited mid-chain; flush what we collected
    except GeneratorExit:
        pass  # older hisstrace end-of-trace close
    except Exception as e:
        verdict["chain"] = f"error: {e!r}"
    print("HISS_CHAIN " + json.dumps(verdict), flush=True)
