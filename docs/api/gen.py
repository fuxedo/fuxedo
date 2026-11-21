#!/usr/bin/env python3
"""Generate the fuxedo API reference pages from one template + a
function table, embedding the complete, compilable example sources from
docs/api/examples with the documented region highlighted.

    python3 docs/api/gen.py

Regenerates docs/api/<func>/index.html for every function in FUNCTIONS
plus docs/api/index.html and the API part of docs/sitemap.xml. Content
is maintained in this file; code samples live in examples/ and are
built and run by docs/api/examples/Makefile (make -C docs/api/examples
check TUXDIR=<install>).
"""

import html
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent
REPO = ROOT.parent.parent

EXAMPLES = ROOT / "examples"

GROUPS = [
    ("XATMI: session", ["tpinit", "tpterm"]),
    ("XATMI: errors", ["tperrno", "tpstrerror"]),
    ("XATMI: calls", ["tpcall", "tpacall", "tpgetrply", "tpcancel",
                      "tpforward"]),
    ("XATMI: conversational", ["tpconnect", "tpsend", "tprecv",
                               "tpdiscon"]),
    ("XATMI: buffers", ["tpalloc", "tpfree", "tptypes", "tpexport",
                        "tpimport"]),
    ("XATMI: transactions", ["tpbegin", "tpgetlev", "tpsuspend",
                             "tpresume", "tpcommit", "tpabort"]),
    ("XATMI: queues", ["tpenqueue", "tpdequeue"]),
    ("XATMI: server", ["tpsvrinit", "tpsvrdone", "tpsvrthrinit",
                       "tpsvrthrdone", "tpreturn", "tpadvertise",
                       "tpadvertisex", "tpunadvertise"]),
    ("FML32", ["fml32", "Ferror32", "Fstrerror32", "Fadd32", "Fchg32",
               "Fget32", "Fdel32", "Fnext32",
               "Fprint32", "Ffprint32", "Fwrite32", "Fread32", "Fboolco32",
               "Fboolev32", "Fboolpr32", "Fmkfldid32", "Fneeded32",
               "Fsizeof32", "Flen32"]),
]

# Each entry: title, synopsis, body sections (HTML), example (file,
# region), see-also. Keep tables to codes a real call can produce.
FUNCTIONS = {
    "tpcall": {
        "title": "synchronous request/response service call",
        "group": "XATMI: calls",
        "synopsis": "int tpcall(char *svc, char *idata, long ilen,\n"
                    "           char **odata, long *olen, long flags);",
        "description": """
<p>tpcall() sends the request buffer <code>idata</code> to the service
named by <code>svc</code> and waits for the reply. It is exactly
<code>tpacall()</code> followed by <code>tpgetrply()</code> on the
returned call descriptor, so argument handling, flags, and errors are
the ones documented for those calls.</p>
<p>On return, <code>*odata</code> points to the reply buffer and
<code>*olen</code> holds the reply length. The caller allocates
<code>*odata</code> with <code>tpalloc()</code>; the library may
reallocate it in place, so keep only the returned pointer.</p>
<p>If the service finishes with <code>tpreturn(TPFAIL, code, ...)</code>,
tpcall() returns -1 with <code>tperrno</code> set to TPESVCFAIL, the
reply data is delivered in <code>*odata</code> as usual, and
<code>code</code> is available in <code>tpurcode</code>.</p>""",
        "args": [
            ("svc", "Name of the advertised service. Empty or NULL names "
                    "fail with TPEINVAL; a name longer than the service "
                    "name limit fails as well."),
            ("idata, ilen", "Request buffer and length. For "
                            "self-describing types (STRING, FML32, VIEW32) "
                            "pass the buffer with ilen 0; for CARRAY pass "
                            "the exact byte count. NULL idata with ilen 0 "
                            "sends an empty request."),
            ("odata, olen", "Address of the reply buffer pointer and reply "
                            "length. The buffer must come from tpalloc(); "
                            "it is freed and replaced if the library "
                            "resizes it."),
            ("flags", "See below."),
        ],
        "flags": [
            ("TPNOTRAN", "The call does not join the current transaction, "
                         "even if one is active."),
            ("TPNOCHANGE", "Force the reply buffer type and subtype to "
                           "match the request; otherwise a type mismatch "
                           "fails with TPEOTYPE."),
            ("TPBLOCK", "Default. If the service request queue is full, "
                        "wait for space."),
            ("TPNOTIME", "Ignore the call timeout while waiting for the "
                         "reply; wait indefinitely."),
            ("TPSIGRSTRT", "Restart an interrupted wait instead of "
                           "returning TPGOTSIG when a caught signal "
                           "arrives mid-call."),
            ("TPNOCOPY", "Accepted for compatibility; only meaningful "
                         "on shared-memory transports, a no-op here."),
        ],
        "flags_note": "Any other flag fails with TPEINVAL. In "
                      "particular TPNOREPLY belongs to tpacall(), not "
                      "tpcall() - tpcall always waits for the reply.",
        "errors": [
            ("TPEINVAL", "svc is NULL or empty, flags are invalid, or the "
                         "arguments are inconsistent (for example a NULL "
                         "idata with a nonzero ilen)."),
            ("TPENOENT", "No server advertises svc in this application. "
                         "With no application attached at all, the "
                         "platform may answer TPEPROTO, TPELIMIT, or "
                         "TPESYSTEM instead."),
            ("TPETIME", "The reply did not arrive within the call "
                        "timeout."),
            ("TPESVCFAIL", "The service ran and returned TPFAIL; the "
                           "reply data is still returned and tpurcode "
                           "carries the service's code."),
            ("TPESVCERR", "The service terminated without returning (for "
                          "example the server process died mid-call)."),
            ("TPEOTYPE", "Reply type differs from the request type while "
                         "TPNOCHANGE is set."),
            ("TPETRAN", "The call runs under a transaction routed to a "
                        "server group that has no transaction manager; "
                        "retry with TPNOTRAN."),
            ("TPGOTSIG", "A signal interrupted the wait and TPSIGRSTRT was "
                         "not set."),
            ("TPEOS, TPESYSTEM", "Operating system or platform internal "
                                 "failure."),
        ],
        "notes": """
<p><strong>tperrno.</strong> A successful call does not reset
<code>tperrno</code>. Read it only when the return value is -1; this
matches Oracle Tuxedo, where applications routinely rely on the value
surviving successful calls.</p>
<p><strong>Timeout.</strong> A late reply that arrives after TPETIME is
discarded by the client library on a later call; a warning may land in
the application log (ULOG).</p>""",
        "versions": None,
        "example": ("tpcall.c", None),
        "see_also": ["tpacall", "tpgetrply", "tpbegin", "Fchg32"],
    },
    "tpacall": {
        "title": "asynchronous service call (send half)",
        "group": "XATMI: calls",
        "synopsis": "int tpacall(char *svc, char *data, long len, "
                    "long flags);",
        "description": """
<p>tpacall() sends a request to a service without waiting for the reply.
It returns a call descriptor; the reply is picked up later with
<code>tpgetrply()</code> (or abandoned with <code>tpcancel()</code>).
With TPNOREPLY no descriptor is returned and no reply is expected.</p>
<p>Argument handling matches tpcall(): self-describing buffers pass
len 0, CARRAY passes the byte count, and the buffer may not be touched
until the reply has been received (the library reads it at send time;
it is copied into the message).</p>""",
        "args": [
            ("svc", "Advertised service name; same validation as "
                    "tpcall()."),
            ("data, len", "Request buffer and length, as for tpcall()."),
            ("flags", "See below."),
        ],
        "flags": [
            ("TPNOREPLY", "No reply is expected; tpacall() returns 0 on "
                          "success. Forbidden inside a transaction "
                          "(TPEPROTO)."),
            ("TPNOTRAN", "The call does not join the current "
                         "transaction."),
            ("TPNOBLOCK", "Fail the send with TPEBLOCK immediately if the "
                          "service queue is full, instead of waiting for "
                          "space."),
            ("TPNOTIME", "No timeout while waiting for the queue; the "
                         "reply wait is governed by the flags handed to "
                         "tpgetrply()."),
            ("TPSIGRSTRT", "Restart interrupted waits instead of failing "
                           "with TPGOTSIG."),
        ],
        "flags_note": "The tpacall flag word is its own: TPNOCHANGE and "
                      "TPNOCOPY are rejected here even though tpcall() "
                      "accepts them, and TPNOREPLY is accepted here even "
                      "though tpcall() rejects it.",
        "errors": [
            ("TPEINVAL", "Bad service name, flags, or arguments."),
            ("TPENOENT", "The service is not advertised."),
            ("TPEPROTO", "TPNOREPLY was requested in transaction mode."),
            ("TPEBLOCK", "TPNOBLOCK was set and the request queue is "
                         "full."),
            ("TPELIMIT", "No free call descriptors; replies must be "
                         "collected to release them."),
            ("TPESYSTEM, TPEOS", "Platform or operating system failure."),
        ],
        "notes": """
<p><strong>Send behavior on a full queue.</strong> Without TPNOBLOCK the
send waits for queue space at a one-second retry cadence; payloads that
would exceed the kernel per-message limit, and blocking payloads above
500 bytes, spill to a /tmp file while only a small control message
waits for space. With TPNOBLOCK the send fails with TPEBLOCK instead,
without spilling.</p>""",
        "versions": None,
        "example": ("acall.c", "tpacall"),
        "see_also": ["tpcall", "tpgetrply", "tpcancel"],
    },
    "tpgetrply": {
        "title": "collect the reply of an outstanding call",
        "group": "XATMI: calls",
        "synopsis": "int tpgetrply(int *cd, char **data, long *len, "
                    "long flags);",
        "description": """
<p>tpgetrply() waits for the reply of a call started with
<code>tpacall()</code>. Without TPGETANY it waits for the descriptor
held in <code>*cd</code>; with TPGETANY it returns the first reply that
arrives and stores its descriptor in <code>*cd</code>.</p>
<p><code>*data</code> must point at a buffer allocated with tpalloc();
the library may reallocate it to fit the reply, and <code>*len</code>
receives the reply length.</p>""",
        "args": [
            ("cd", "Address of the call descriptor. Replaced with the "
                   "replied descriptor when TPGETANY is set."),
            ("data, len", "Reply buffer pointer and returned length."),
            ("flags", "See below."),
        ],
        "flags": [
            ("TPGETANY", "Return the next reply for any outstanding "
                         "call."),
            ("TPNOBLOCK", "Return TPEBLOCK immediately when no reply is "
                          "ready instead of waiting."),
            ("TPNOTIME", "Wait without a deadline; the reaper never "
                         "fires for this wait."),
            ("TPSIGRSTRT", "Restart interrupted waits."),
            ("TPNOCHANGE", "Force the reply buffer type and subtype to "
                           "match the buffer passed in."),
        ],
        "flags_note": "Reply-side flags only: TPNOREPLY, TPNOTRAN, and "
                      "the rest of the call-side word fail with "
                      "TPEINVAL.",
        "errors": [
            ("TPEBADDESC", "*cd is not an outstanding call descriptor."),
            ("TPEPROTO", "TPGETANY was requested with no outstanding "
                         "calls at all."),
            ("TPETIME", "The reply did not arrive within the call "
                        "timeout. The descriptor stays outstanding and "
                        "can be waited on again; a reply arriving after "
                        "the timeout is discarded on a later call."),
            ("TPEBLOCK", "TPNOBLOCK was set and no reply is ready."),
            ("TPEINVAL", "data is NULL or *data is not a typed buffer."),
            ("TPGOTSIG", "A signal interrupted the wait and TPSIGRSTRT "
                         "was not set."),
            ("TPESYSTEM, TPEOS", "Platform or operating system failure."),
        ],
        "notes": """
<p><strong>Scatter/gather.</strong> The usual high-throughput shape is
a fixed window of tpacall()s with tpgetrply(TPGETANY): whenever a reply
arrives, issue the next request so the window stays full.</p>""",
        "versions": None,
        "example": ("acall.c", "tpgetrply"),
        "see_also": ["tpacall", "tpcall", "tpcancel"],
    },
    "tpcancel": {
        "title": "abandon an outstanding call",
        "group": "XATMI: calls",
        "synopsis": "int tpcancel(int cd);",
        "description": """
<p>tpcancel() abandons an outstanding call descriptor: the caller will
never collect its reply. The descriptor is released immediately; if the
service reply arrives later it is discarded and the application log may
note it.</p>""",
        "args": [("cd", "A call descriptor returned by tpacall() that has "
                         "not been replied to or cancelled yet.")],
        "flags": None,
        "errors": [
            ("TPEBADDESC", "cd is not an outstanding call descriptor."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("cancel.c", "tpcancel"),
        "see_also": ["tpacall", "tpgetrply"],
    },
    "tpforward": {
        "title": "hand the request to another service (server only)",
        "group": "XATMI: calls",
        "synopsis": "void tpforward(char *svc, char *data, long len, "
                    "long flags);",
        "description": """
<p>tpforward() is called from a service and hands the current request
to another service. It does not return: the forwarding service
terminates, and the reply produced by the target service goes to the
original caller with the forwarding service's return code semantics
cleared. The caller cannot tell the request was forwarded.</p>
<p>The target service name must be advertised; otherwise the caller
receives an error reply (TPENOENT) instead of a normal result.</p>""",
        "args": [
            ("svc", "The service that will produce the reply."),
            ("data, len", "The (possibly rewritten) request. As usual, "
                          "len 0 means the buffer length for "
                          "self-describing types."),
            ("flags", "Currently no forward-specific flags; pass 0."),
        ],
        "flags": None,
        "errors": [
            ("TPENOENT", "svc is not advertised; reported to the caller "
                         "as an error reply."),
        ],
        "notes": """
<p><strong>Buffers.</strong> The buffer handed to tpforward() follows
the tpreturn() ownership rules: the system copies it out and frees it,
so never tpfree() it after the call (it does not return anyway). The
original request buffer (the TPSVCINFO <code>data</code>) is reclaimed
by the system as with any service exit, so it needs no cleanup either;
passing a different buffer here is exactly the case the platform
handles without leaks.</p>
<p><strong>Transactions.</strong> A forward keeps the request inside the
caller's transaction; the target service joins it like any participant.
tpforward, not tpcall, is the right way to chain services within one
transactional call.</p>
<p><strong>C++.</strong> tpforward() never returns, and the service
frame is discarded without running destructors (the same mechanics as
<code>tpreturn()</code>): objects with non-trivial destructors declared
in the service body leak or stay locked. Free resources before the
forward, or keep the forward as the last statement after plain
C-style cleanup.</p>""",
        "versions": None,
        "example": ("apisvr.c", "tpforward"),
        "see_also": ["tpcall", "tpreturn"],
    },
    "tpconnect": {
        "title": "open a conversation with a service",
        "group": "XATMI: conversational",
        "synopsis": "int tpconnect(char *svc, char *data, long len, "
                    "long flags);",
        "description": """
<p>tpconnect() opens a conversation: a bidirectional, sequenced
exchange with one service invocation, carried without the requester
blocking in a service call. The service runs with TPCONV set in
svcinfo-&gt;flags and reads the connect buffer from
<code>svcinfo-&gt;data</code>; the two sides then trade messages with
tpsend() and tprecv() until one of them ends it.</p>
<p>The conversation is half-duplex at any moment: exactly one side
holds send permission, and handing it over is part of the protocol
(TPRECVONLY on tpsend(), TPEV_SENDONLY events on tprecv()).</p>""",
        "args": [
            ("svc", "Advertised service name; validated like tpcall()."),
            ("data, len", "Optional connect data, delivered to the "
                          "service as svcinfo-&gt;data."),
            ("flags", "An explicit direction is mandatory, see below."),
        ],
        "flags": [
            ("TPSENDONLY", "The caller sends first."),
            ("TPRECVONLY", "The caller waits first."),
        ],
        "flags_note": "Exactly one of the two mode bits must be set. "
                      "Passing 0, both bits together, or any timing or "
                      "call flag (TPNOBLOCK, TPNOTRAN, ...) fails with "
                      "TPEINVAL - a direction is always spelled out.",
        "errors": [
            ("TPEINVAL", "Invalid flag word or arguments."),
            ("TPENOENT", "No server advertises svc."),
            ("TPETIME", "No server accepted the connect within the "
                        "timeout."),
            ("TPESYSTEM, TPEOS", "Platform or operating system "
                                 "failure."),
        ],
        "notes": """
<p><strong>Ending it.</strong> Either side may end the conversation:
tpreturn() from the service, or tpdiscon() from the caller. The peer
learns of the end through TPEV_DISCONIMM, TPEV_SVCSUCC, or TPEV_SVCFAIL
in revent, or a failed tpsend()/tprecv().</p>""",
        "versions": None,
        "example": ("conv.c", "tpconnect"),
        "see_also": ["tpsend", "tprecv", "tpdiscon"],
    },
    "tpsend": {
        "title": "send one message on a conversation",
        "group": "XATMI: conversational",
        "synopsis": "int tpsend(int cd, char *data, long len, long "
                    "flags, long *revent);",
        "description": """
<p>tpsend() delivers one message on a conversation opened with
tpconnect(). The caller must hold send permission; sending without it
fails with TPEPROTO. Each call carries at most one direction change,
expressed with TPRECVONLY, after which the peer's tprecv() reports
TPEV_SENDONLY.</p>""",
        "args": [
            ("cd", "Conversation descriptor from tpconnect()."),
            ("data, len", "Message buffer; len 0 uses the buffer length "
                          "for self-describing types."),
            ("flags", "See below."),
            ("revent", "Receives event bits when the call fails with "
                       "TPEEVENT."),
        ],
        "flags": [
            ("TPRECVONLY", "Send this message and hand send permission "
                           "to the peer."),
            ("TPNOBLOCK", "Fail with TPEBLOCK instead of waiting if the "
                          "peer's queue is full."),
            ("TPNOTIME", "Wait without a deadline for queue space."),
            ("TPSIGRSTRT", "Restart interrupted waits."),
        ],
        "flags_note": "Only the receive-side mode flip is accepted; "
                      "TPSENDONLY makes no sense on a send and fails "
                      "with TPEINVAL.",
        "errors": [
            ("TPEBADDESC", "cd is not an open conversation."),
            ("TPEPROTO", "The caller does not hold send permission."),
            ("TPEEVENT", "A protocol event arrived; check *revent "
                         "(TPEV_DISCONIMM, TPEV_SVCFAIL, ...)."),
            ("TPEBLOCK", "TPNOBLOCK was set and the peer's queue is "
                         "full."),
            ("TPESYSTEM, TPEOS", "Platform or operating system "
                                 "failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("conv.c", "tpsend"),
        "see_also": ["tpconnect", "tprecv", "tpdiscon"],
    },
    "tprecv": {
        "title": "receive one message or event on a conversation",
        "group": "XATMI: conversational",
        "synopsis": "int tprecv(int cd, char **data, long *len, long "
                    "flags, long *event);",
        "description": """
<p>tprecv() waits for the next message on a conversation and stores it
in <code>*data</code> (a tpalloc() buffer, resized as needed). When
what arrives is not data but a protocol event, the call fails with
TPEEVENT and <code>*event</code> says which: TPEV_SENDONLY (the peer
handed send permission over), TPEV_DISCONIMM (the peer disconnected),
TPEV_SVCSUCC / TPEV_SVCFAIL (the service returned).</p>""",
        "args": [
            ("cd", "Conversation descriptor from tpconnect()."),
            ("data, len", "Buffer pointer and returned message "
                          "length."),
            ("flags", "See below."),
            ("event", "Receives event bits when the call fails with "
                      "TPEEVENT."),
        ],
        "flags": [
            ("TPNOBLOCK", "Fail with TPEBLOCK immediately when no "
                          "message is ready."),
            ("TPNOTIME", "Wait without a deadline."),
            ("TPSIGRSTRT", "Restart interrupted waits."),
            ("TPNOCHANGE", "Force the incoming buffer type to match the "
                           "buffer passed in."),
        ],
        "errors": [
            ("TPEBADDESC", "cd is not an open conversation."),
            ("TPEEVENT", "An event arrived; check *event."),
            ("TPEBLOCK", "TPNOBLOCK was set and nothing is ready."),
            ("TPETIME", "The wait exceeded the conversation timeout."),
            ("TPESYSTEM, TPEOS", "Platform or operating system "
                                 "failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("conv.c", "tprecv"),
        "see_also": ["tpconnect", "tpsend", "tpdiscon"],
    },
    "tpdiscon": {
        "title": "tear a conversation down immediately",
        "group": "XATMI: conversational",
        "synopsis": "int tpdiscon(int cd);",
        "description": """
<p>tpdiscon() ends a conversation from the caller side, abruptly: the
service's next (or in-flight) tprecv() or tpsend() fails with
TPEV_DISCONIMM and no return data is delivered. Use it when the caller
loses interest; a graceful end is an empty final message or the
service's tpreturn(), which map to TPEV_SVCSUCC.</p>""",
        "args": [("cd", "An open conversation descriptor from "
                        "tpconnect().")],
        "flags": None,
        "errors": [
            ("TPEBADDESC", "cd is not an open conversation."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("conv.c", "tpdiscon"),
        "see_also": ["tpconnect", "tpsend", "tprecv"],
    },
    "tperrno": {
        "title": "the ATMI error indicator",
        "group": "XATMI: errors",
        "synopsis": "int tperrno;  // a macro wrapping thread-local "
                    "storage",
        "description": """
<p>Every ATMI call reports failure through tperrno: on a -1 or NULL
return it holds one of the TPE* error codes (TPEINVAL, TPENOENT,
TPETIME, TPESVCFAIL, ... documented per function). A successful call
does <strong>not</strong> reset it, so the value is meaningful only
immediately after a failure; each thread holds its own. Render the
code with <code>tpstrerror()</code>. The FML32 calls use their own
indicator, <code>Ferror32</code>.</p>""",
        "args": [],
        "flags": None,
        "errors": [],
        "notes": """
<p>Code that checks tperrno unconditionally after every call is a bug
in the making: on success it reports the outcome of an earlier,
unrelated failure. The convention is <code>if (call() == -1) switch
(tperrno)</code>, nothing more.</p>""",
        "versions": None,
        "example": ("session.c", "tperrno"),
        "see_also": ["tpstrerror", "Ferror32"],
    },
    "tpstrerror": {
        "title": "render a tperrno code",
        "group": "XATMI: errors",
        "synopsis": "char *tpstrerror(int err);",
        "description": """
<p>tpstrerror() returns the message text for a tperrno error code.
The pointer refers to a static buffer that later calls may overwrite,
and the call itself does not touch tperrno, so it is safe to use
inside an error path built around the current value.</p>""",
        "args": [("err", "The error code, typically tperrno.")],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("session.c", "tpstrerror"),
        "see_also": ["tperrno"],
    },
    "tpinit": {
        "title": "join an application",
        "group": "XATMI: session",
        "synopsis": "int tpinit(TPINIT *tpinfo);",
        "description": """
<p>tpinit() attaches the process to an application and must succeed
before any other ATMI call (tpalloc() and the tperrno family excepted).
Passing NULL joins with defaults; a TPINIT structure carries the
client name, username, password, application password, and the group
name requests are scoped to, plus client identification data when the
application requires it.</p>""",
        "args": [
            ("tpinfo", "NULL for defaults, or a TPINIT with usrname, "
                       "cltname, passwd, grpname, flags, and the "
                       "datalen/data identification payload."),
        ],
        "flags": [
            ("TPMUTUALAUTH, TPABSOLUTE...", "Security-related flags from "
                                             "the header; rarely needed "
                                             "with SECURITY NONE."),
        ],
        "errors": [
            ("TPEINVAL", "A TPINIT field is inconsistent (version, name "
                         "lengths, datalen without data)."),
            ("TPEPERM", "Authentication failed."),
            ("TPELIMIT", "The application is full (MAXACCESSERS)."),
            ("TPESYSTEM, TPEOS", "Platform or operating system failure."),
        ],
        "notes": """
<p><strong>Servers.</strong> Server processes do not call tpinit(); the
main delivered by buildserver() handles the attach and calls
tpsvrinit() instead.</p>""",
        "versions": None,
        "example": ("session.c", "tpinit"),
        "see_also": ["tpterm", "tpcall"],
    },
    "tpterm": {
        "title": "leave an application",
        "group": "XATMI: session",
        "synopsis": "int tpterm(void);",
        "description": """
<p>tpterm() detaches the process from the application, releasing the
access slot and any outstanding resources. Call it when the process is
done with the application; exiting without tpterm() leaves the
attachment to be reaped by the bulletin board.</p>""",
        "args": [],
        "flags": None,
        "errors": [
            ("TPESYSTEM", "Platform failure while detaching."),
        ],
        "notes": None,
        "versions": None,
        "example": ("session.c", "tpterm"),
        "see_also": ["tpinit"],
    },
    "tpalloc": {
        "title": "allocate a typed buffer",
        "group": "XATMI: buffers",
        "synopsis": "char *tpalloc(char *type, char *subtype, long size);",
        "description": """
<p>tpalloc() allocates a typed buffer. type is one of STRING, CARRAY,
FML32, or VIEW32; subtype names the view for VIEW32 and is NULL
otherwise. All ATMI calls take tpalloc'd buffers, and every buffer
passed across the API must come from tpalloc().</p>
<p>The size meaning depends on the type: bytes for CARRAY (the exact
length sent), the maximum string length including the terminator for
STRING, the initial fielded-buffer size for FML32 (it grows as needed),
and ignored for VIEW32 (the view definition decides).</p>""",
        "args": [
            ("type", "STRING, CARRAY, FML32, or VIEW32."),
            ("subtype", "View name for VIEW32; NULL otherwise."),
            ("size", "Initial size in the type's units; 0 where the "
                     "definition decides."),
        ],
        "flags": None,
        "errors": [
            ("TPEINVAL", "Unknown type or subtype."),
            ("TPESYSTEM, TPEOS", "Platform or operating system failure, "
                                 "including allocation failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("buffers.c", "tpalloc"),
        "see_also": ["tpfree", "tptypes", "Fchg32"],
    },
    "tpfree": {
        "title": "release a typed buffer",
        "group": "XATMI: buffers",
        "synopsis": "void tpfree(char *ptr);",
        "description": """
<p>tpfree() releases a buffer allocated by tpalloc() (or reallocated by
the library during a call). Passing NULL is a harmless no-op. A buffer
pointer passed to tpfree() must not be used afterwards, and buffers
must be freed exactly once.</p>""",
        "args": [("ptr", "A buffer from tpalloc(), or NULL.")],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("buffers.c", "tpfree"),
        "see_also": ["tpalloc", "tptypes"],
    },
    "tptypes": {
        "title": "inspect a typed buffer",
        "group": "XATMI: buffers",
        "synopsis": "long tptypes(char *ptr, char *type, char *subtype);",
        "description": """
<p>tptypes() reports the type, subtype, and length of a tpalloc'd
buffer. type and subtype receive NUL-terminated names (pass NULL to
skip either); the return value is the buffer length in the type's
units, or -1 with tperrno set on error.</p>""",
        "args": [
            ("ptr", "A buffer from tpalloc()."),
            ("type, subtype", "Output buffers of at least 8 and 16 "
                              "characters, or NULL."),
        ],
        "flags": None,
        "errors": [
            ("TPEINVAL", "ptr is not a typed buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("buffers.c", "tptypes"),
        "see_also": ["tpalloc", "tpfree"],
    },
    "tpbegin": {
        "title": "start a transaction",
        "group": "XATMI: transactions",
        "synopsis": "int tpbegin(unsigned long timeout, long flags);",
        "description": """
<p>tpbegin() puts the caller in transaction mode. Calls made while in
transaction mode join the transaction (unless TPNOTRAN opts them
out), and the transaction ends with <code>tpcommit()</code> or
<code>tpabort()</code>. timeout is the limit in seconds; 0 selects
the platform default. A transaction with no resource managers commits
trivially, which is why demarcation can be exercised without a
database.</p>""",
        "args": [
            ("timeout", "Seconds before the transaction is rolled back; "
                        "0 means the platform default."),
            ("flags", "0; TPNOBLOCK/TPNOTIME/TPSIGRSTRT govern internal "
                      "waits."),
        ],
        "flags": [
            ("TPNOBLOCK, TPNOTIME, TPSIGRSTRT", "Wait disposition for "
                                                "internal operations."),
        ],
        "errors": [
            ("TPEINVAL", "Invalid timeout or flags."),
            ("TPEPROTO", "The caller is already in transaction mode."),
            ("TPESYSTEM, TPEOS", "Platform failure."),
        ],
        "notes": """
<p><strong>Return value.</strong> Returns 1 on success, -1 on
error, on the platform and in fuxedo. Check <code>== -1</code>, not
truthiness: <code>if (tpbegin(...))</code> treats a successful begin
as failure.</p>""",
        "versions": None,
        "example": ("txdemo.c", "tpbegin"),
        "see_also": ["tpcommit", "tpabort", "tpgetlev"],
    },
    "tpgetlev": {
        "title": "are we in transaction mode?",
        "group": "XATMI: transactions",
        "synopsis": "int tpgetlev(void);",
        "description": """
<p>tpgetlev() reports whether the caller is in transaction mode:
TP_IN_TRANSACTION (1) or TP_NOT_IN_TRANSACTION (0). Cheap enough to
call freely when code paths branch on the transaction state.</p>""",
        "args": [],
        "flags": None,
        "errors": [
            ("TPESYSTEM", "Platform failure answering the query."),
        ],
        "notes": None,
        "versions": None,
        "example": ("txdemo.c", "tpgetlev"),
        "see_also": ["tpbegin", "tpsuspend"],
    },
    "tpsuspend": {
        "title": "stash the current transaction",
        "group": "XATMI: transactions",
        "synopsis": "int tpsuspend(TPTRANID *tranid, long flags);",
        "description": """
<p>tpsuspend() saves the caller's transaction into a TPTRANID and
leaves transaction mode, so another thread can <code>tpresume()</code>
it, or the same thread can do non-transactional work first. The
transaction keeps running against its timeout while suspended.</p>""",
        "args": [
            ("tranid", "Output for the transaction identifier; pass it "
                       "to tpresume."),
            ("flags", "0."),
        ],
        "flags": None,
        "errors": [
            ("TPEPROTO", "Not in transaction mode, or an invalid "
                         "suspend state."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("txdemo.c", "tpsuspend"),
        "see_also": ["tpresume", "tpbegin"],
    },
    "tpresume": {
        "title": "re-enter a stashed transaction",
        "group": "XATMI: transactions",
        "synopsis": "int tpresume(TPTRANID *tranid, long flags);",
        "description": """
<p>tpresume() re-enters the transaction previously saved by
<code>tpsuspend()</code>, in this thread. A stashed transaction must
be resumed exactly once; abandoning it leaks the transaction until
its timeout rolls it back.</p>""",
        "args": [
            ("tranid", "The identifier filled by tpsuspend."),
            ("flags", "0."),
        ],
        "flags": None,
        "errors": [
            ("TPEPROTO", "Already in transaction mode, or the identifier "
                         "is not a suspended transaction."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("txdemo.c", "tpresume"),
        "see_also": ["tpsuspend", "tpcommit"],
    },
    "tpcommit": {
        "title": "commit the current transaction",
        "group": "XATMI: transactions",
        "synopsis": "int tpcommit(long flags);",
        "description": """
<p>tpcommit() ends the caller's transaction with a commit. With no
resource managers this is bookkeeping; with participants it runs
two-phase commit, and the error table below shows how the exotic
outcomes surface (TPEHAZARD, TPEHEURISTIC). After the call the caller
is no longer in transaction mode, and a tpreturn(TPSUCCESS) inside a
service commits implicitly the same way.</p>""",
        "args": [("flags", "0, or TPNOBLOCK/TPNOTIME/TPSIGRSTRT for the "
                           "wait disposition.")],
        "flags": [
            ("TPNOBLOCK, TPNOTIME, TPSIGRSTRT", "Wait disposition for "
                                                "the commit."),
        ],
        "errors": [
            ("TPEABORT", "The transaction was rolled back instead; "
                         "reported when commit could not complete."),
            ("TPEHAZARD", "Commit outcome is uncertain; manual "
                          "reconciliation may be needed."),
            ("TPEHEURISTIC", "A participant resolved heuristically."),
            ("TPEPROTO", "Not in transaction mode."),
            ("TPETIME", "The commit wait timed out."),
            ("TPESYSTEM, TPEOS", "Platform failure."),
        ],
        "notes": """
<p><strong>Return value.</strong> Returns 1 on success, -1 on
error, on the platform and in fuxedo. Check <code>== -1</code>, not
truthiness: <code>if (tpbegin(...))</code> treats a successful begin
as failure.</p>""",
        "versions": None,
        "example": ("txdemo.c", "tpcommit"),
        "see_also": ["tpabort", "tpbegin"],
    },
    "tpabort": {
        "title": "roll the current transaction back",
        "group": "XATMI: transactions",
        "synopsis": "int tpabort(long flags);",
        "description": """
<p>tpabort() rolls the caller's transaction back and leaves
transaction mode. All participating work is undone; the transaction
timeout rolls back automatically when the deadline passes.</p>""",
        "args": [("flags", "0.")],
        "flags": None,
        "errors": [
            ("TPEPROTO", "Not in transaction mode."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": """
<p><strong>Return value.</strong> Returns 1 on success, -1 on
error, on the platform and in fuxedo. Check <code>== -1</code>, not
truthiness: <code>if (tpbegin(...))</code> treats a successful begin
as failure.</p>""",
        "versions": None,
        "example": ("txdemo.c", "tpabort"),
        "see_also": ["tpcommit", "tpbegin"],
    },
    "tpenqueue": {
        "title": "append a message to a /Q queue",
        "group": "XATMI: queues",
        "synopsis": "int tpenqueue(char *qspace, char *qname, TPQCTL *ctl,\n"
                    "              char *data, long len, long flags);",
        "description": """
<p>tpenqueue() appends the buffer <code>data</code> to the queue
<code>qname</code> in queue space <code>qspace</code>. Queue spaces live
in the device named by the <code>QMCONFIG</code> environment variable;
when the application runs a TMQUEUE server for the space, the request
is routed through it, otherwise the caller talks to the queue files
directly.</p>
<p>The <code>TPQCTL</code> structure carries optional delivery
properties; a zeroed structure means a plain, persistent message. The
<code>flags</code> argument itself must be 0.</p>
<p>Inside a transaction the enqueue is staged with the transaction and
is undone when the transaction aborts, so a message either appears
exactly once or not at all.</p>""",
        "args": [
            ("qspace, qname", "Queue space and queue names, both required. "
                              "An unknown queue fails with TPENOENT."),
            ("ctl", "TPQCTL control structure. Zero all of it for plain "
                    "delivery; see the flags below."),
            ("data, len", "The message buffer; len 0 for self-describing "
                          "types (STRING, FML32, VIEW32) like in tpcall()."),
            ("flags", "Must be 0. Anything else fails with TPEINVAL."),
        ],
        "flags": [
            ("TPQREPLYQ", "Set ctl.replyqueue to a queue name; the reply "
                          "to this message is expected there."),
            ("TPQFAILUREQ", "Set ctl.failurequeue for messages that fail "
                            "delivery."),
            ("TPQTIME_ABS, TPQTIME_REL", "Set ctl.deq_time to the absolute "
                                         "epoch time, or seconds from now, "
                                         "when the message becomes "
                                         "visible. Until then tpdequeue() "
                                         "skips it; the MIB T_APPQMSG "
                                         "class still lists it as "
                                         "TIME-state."),
            ("TPQPRIORITY", "Set ctl.priority (0-100, default 50) for "
                            "priority-ordered queues."),
            ("TPQCORRID", "Set ctl.corrid, a caller-chosen correlation "
                          "id, to find the message again with "
                          "TPQGETBYCORRID on dequeue."),
            ("TPQMSGID", "On return, ctl.msgid identifies the stored "
                         "message."),
        ],
        "errors": [
            ("TPEINVAL", "NULL arguments, nonzero flags, or ctl field "
                         "combinations that do not parse."),
            ("TPENOENT", "The queue space or queue does not exist."),
            ("TPEDIAGNOSTIC", "Queue manager rejection; ctl.diagnostic "
                              "carries a QME_* code such as QMEBADQUEUE "
                              "or QMEENOMSG."),
            ("TPETRAN", "Queue operations were requested inside a "
                        "transaction for a resource manager that cannot "
                        "stage them."),
            ("TPETIME, TPESYSTEM, TPEOS", "Timeouts and internal "
                                          "failures."),
        ],
        "notes": """
<p><strong>Visibility.</strong> Messages with a future deq_time are not
returned by tpdequeue() until their time arrives; an expired message
moves to its failure queue automatically. The T_APPQMSG MIB class lists
every stored message, future ones included.</p>""",
        "versions": None,
        "example": ("enqueue.c", "tpenqueue"),
        "see_also": ["tpdequeue", "tpcall", "tpbegin"],
    },
    "tpdequeue": {
        "title": "take the next message off a /Q queue",
        "group": "XATMI: queues",
        "synopsis": "int tpdequeue(char *qspace, char *qname, TPQCTL *ctl,\n"
                    "              char **data, long *len, long flags);",
        "description": """
<p>tpdequeue() removes the next visible message from <code>qname</code>
and returns it in <code>*data</code>. <code>*data</code> must point at
a tpalloc'd buffer; the library may replace it with a larger one if the
message does not fit.</p>
<p>Messages whose deq_time lies in the future are skipped, so a queue
holding only future messages behaves as empty. Waiting is governed by
TPQWAIT in <code>ctl-&gt;flags</code>, with an optional deadline from
TPQTIME_ABS/TPQTIME_REL; the <code>flags</code> argument itself accepts
TPNOTIME (wait without a deadline) and the selectors below.</p>""",
        "args": [
            ("qspace, qname", "Queue space and queue names, both required. "
                              "An unknown queue fails with TPENOENT."),
            ("ctl", "TPQCTL control structure; zeroed for a plain "
                    "dequeue."),
            ("data, len", "Address of the buffer pointer and a length. "
                          "The buffer must come from tpalloc()."),
            ("flags", "0, TPNOTIME, TPQPEEK, or a selector."),
        ],
        "flags": [
            ("TPQWAIT", "ctl.flags: wait for a message instead of failing "
                        "with TPETIME on an empty queue."),
            ("TPQTIME_ABS, TPQTIME_REL", "ctl.flags: with TPQWAIT, set "
                                         "ctl.deq_time as the wait "
                                         "deadline (absolute epoch or "
                                         "seconds)."),
            ("TPNOTIME", "flags: with TPQWAIT, wait without a deadline."),
            ("TPQGETBYMSGID, TPQGETBYCORRID", "ctl.flags with ctl.msgid or "
                                              "ctl.corrid set: take the "
                                              "specific message instead "
                                              "of the first one."),
            ("TPQPEEK", "Read the message without removing it."),
        ],
        "errors": [
            ("TPEINVAL", "NULL arguments, *data not tpalloc'd, or invalid "
                         "flag combinations."),
            ("TPENOENT", "The queue space or queue does not exist."),
            ("TPETIME", "No visible message (and no wait, or the wait "
                        "deadline passed)."),
            ("TPEDIAGNOSTIC", "Queue manager rejection; ctl.diagnostic "
                              "carries a QME_* code."),
            ("TPETRAN, TPESYSTEM, TPEOS", "Transactional misuse and "
                                          "internal failures."),
        ],
        "notes": """
<p><strong>Reply routing.</strong> A message enqueued with TPQREPLYQ
names the queue where the service reply belongs; a TMQFORWARD-driven
service chain reads the reply queue from the dequeued message.</p>""",
        "versions": None,
        "example": ("dequeue.c", "tpdequeue"),
        "see_also": ["tpenqueue", "tpalloc", "tpcall"],
    },
    "tpexport": {
        "title": "render a typed buffer in portable form",
        "group": "XATMI: buffers",
        "synopsis": "int tpexport(char *ibuf, long ilen, char *ostr,\n"
                    "             long *olen, long flags);",
        "description": """
<p>tpexport() renders a typed buffer into a machine-independent form
suitable for files, pipes, or byte streams; <code>tpimport()</code>
rebuilds the buffer from that form. *olen carries the size of ostr on
entry and receives the written length. The export preserves the type,
so the rebuilt buffer needs no tpalloc from the caller.</p>""",
        "args": [
            ("ibuf, ilen", "Source buffer and length; ilen 0 for "
                           "self-describing types."),
            ("ostr, olen", "Output storage; *olen is its size on entry "
                           "and the written length on return."),
            ("flags", "0, or TPEX_STRING for the text form."),
        ],
        "flags": [
            ("TPEX_STRING", "Export in the readable text form instead "
                            "of the binary form."),
            ("TPEX_XDR64LONG", "64-bit long encoding (22.x headers "
                               "only)."),
        ],
        "errors": [
            ("TPEINVAL", "Bad buffer, sizes, or flags."),
            ("TPEOS, TPESYSTEM", "Platform failure, including a short "
                                 "ostr."),
        ],
        "notes": None,
        "versions": "Unknown flag bits are rejected with TPEINVAL on "
                    "12.2 and accepted on 22.1. TPEX_XDR64LONG only "
                    "exists in the 22.x headers.",
        "example": ("buffers.c", "tpexport"),
        "see_also": ["tpimport", "tpalloc"],
    },
    "tpimport": {
        "title": "rebuild a typed buffer from its portable form",
        "group": "XATMI: buffers",
        "synopsis": "int tpimport(char *istr, long ilen,\n"
                    "             char **obuf, long *olen, long flags);",
        "description": """
<p>tpimport() rebuilds a buffer stored by <code>tpexport()</code>.
*obuf addresses the target buffer pointer and *olen its size on
entry; on return the buffer pointer and the rebuilt length are
updated. When the target is too small, 22.x grows it with tprealloc
and updates *obuf, so the caller should always use the returned
pointer and release it with tpfree().</p>""",
        "args": [
            ("istr, ilen", "The exported form and its length."),
            ("obuf, olen", "Address of the target buffer pointer; "
                           "*olen carries the buffer size on entry."),
            ("flags", "0, or TPEX_STRING when the source uses the text "
                      "form."),
        ],
        "flags": [
            ("TPEX_STRING", "Read the text form."),
        ],
        "errors": [
            ("TPEINVAL", "Bad input, sizes, or flags."),
            ("TPEOS, TPESYSTEM", "Platform failure, including malformed "
                                 "input (22.x reports TPESYSTEM)."),
        ],
        "notes": None,
        "versions": "Four pinned differences. A NULL *obuf fails with "
                    "TPEINVAL on 12.2; 22.x allocates the buffer (free "
                    "it with tpfree). Malformed input reports TPEPROTO "
                    "on 12.2 and TPESYSTEM on 22.x. An undersized "
                    "target is grown via tprealloc on 22.x, while 12.2 "
                    "fails or, with a binary FML export, writes past "
                    "the buffer; size the target generously on 12.2. "
                    "Unknown flag bits are rejected on 12.2 and "
                    "accepted on 22.x. fuxedo follows the 22.x "
                    "behavior throughout.",
        "example": ("buffers.c", "tpimport"),
        "see_also": ["tpexport", "tpalloc"],
    },

    # --- server lifecycle -------------------------------------------------
    "tpsvrinit": {
        "title": "server boot hook",
        "group": "XATMI: server",
        "synopsis": "int tpsvrinit(int argc, char **argv);",
        "description": """
<p>tpsvrinit() is the server boot hook, called by the server main after
the attach and before any request is read. It advertises the services
the server implements (with <code>tpadvertise()</code>), opens
application resources, and returns 0 to accept work or -1 to abort the
boot. Servers built with buildserver() get a default tpsvrinit that
advertises the <code>-s</code> names from CLOPT; write your own the
moment you need runtime decisions.</p>""",
        "args": [
            ("argc, argv", "The server command line: CLOPT arguments and "
                           "any application-specific options."),
        ],
        "flags": None,
        "errors": [
            ("TPESYSTEM, TPEOS", "Advertising or resource setup failed "
                                 "inside the hook."),
        ],
        "notes": """
<p>Returning -1 makes the server exit and, with RESTART=Y, the bulletin
board reincarnates it per MAXGEN, so a persistent boot failure shows up
as repeated shutdown/restart cycles in the log.</p>""",
        "versions": None,
        "example": ("advsvr.c", "tpsvrinit"),
        "see_also": ["tpadvertise", "tpsvrdone", "tpreturn"],
    },
    "tpsvrdone": {
        "title": "server shutdown hook",
        "group": "XATMI: server",
        "synopsis": "void tpsvrdone(void);",
        "description": """
<p>tpsvrdone() is the server shutdown hook, called once at orderly
shutdown after the last request has finished. Withdraw advertisements,
close files and connections, and log the exit here. There is no error
return; the server exits after the hook.</p>""",
        "args": [],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("advsvr.c", "tpsvrdone"),
        "see_also": ["tpsvrinit", "tpunadvertise"],
    },
    "tpsvrthrinit": {
        "title": "dispatch-thread start hook",
        "group": "XATMI: server",
        "synopsis": "int tpsvrthrinit(int argc, char **argv);",
        "description": """
<p>tpsvrthrinit() is called once per dispatch thread when the server
boots with MINDISPATCHTHREADS above zero, before the thread takes
requests. Allocate per-thread state here; return -1 to refuse the
thread, which then exits and is not replaced.</p>""",
        "args": [
            ("argc, argv", "The server command line, as for "
                           "tpsvrinit()."),
        ],
        "flags": None,
        "errors": [],
        "notes": """
<p>With MINDISPATCHTHREADS unset the server is single-threaded and this
hook is not called.</p>""",
        "versions": None,
        "example": ("advsvr.c", "tpsvrthrinit"),
        "see_also": ["tpsvrthrdone", "tpsvrinit"],
    },
    "tpsvrthrdone": {
        "title": "dispatch-thread exit hook",
        "group": "XATMI: server",
        "synopsis": "void tpsvrthrdone(void);",
        "description": """
<p>tpsvrthrdone() is called when a dispatch thread exits, paired with
<code>tpsvrthrinit()</code>. Release the per-thread state allocated
there.</p>""",
        "args": [],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("advsvr.c", "tpsvrthrdone"),
        "see_also": ["tpsvrthrinit"],
    },
    "tpreturn": {
        "title": "finish a service call (server only)",
        "group": "XATMI: server",
        "synopsis": "void tpreturn(int rval, long rcode, char *data, "
                    "long len, long flags);",
        "description": """
<p>tpreturn() ends the current service call and never returns to the
service. With rval TPSUCCESS the data is delivered as the reply; with
TPFAIL the caller sees TPESVCFAIL and rcode arrives in
<code>tpurcode</code> either way. TPEXIT additionally takes the server
down after the reply, which is how a service asks for a restart.</p>""",
        "args": [
            ("rval", "TPSUCCESS, TPFAIL, or TPEXIT."),
            ("rcode", "Application code; surfaced as tpurcode on the "
                      "client."),
            ("data, len", "Reply buffer and length; len 0 means the "
                          "buffer length for self-describing types."),
            ("flags", "0, or TPSOFTTIMEOUT to mark the reply as a soft "
                      "timeout result."),
        ],
        "flags": [
            ("TPSOFTTIMEOUT", "The reply carries a soft-timeout "
                              "indication (service deadline "
                              "facilities)."),
        ],
        "errors": [],
        "notes": """
<p>A service that falls off the end without calling tpreturn() is an
error path; always end services with tpreturn() or
<code>tpforward()</code>.</p>
<p><strong>Buffers.</strong> Two buffers are involved, and ownership
differs:</p>
<ul>
<li>The <em>request</em> buffer (the <code>data</code> member of the
TPSVCINFO structure) belongs to the system. It stays valid only until
tpreturn() is called; the system reclaims it afterwards whether you
reply with the same buffer, a different one, or none. You do not need
to tpfree() it. On both platforms an early tpfree() before returning
a different buffer is tolerated, so existing code that frees it is
safe (verified: no leak when left alone, no double-free when freed
early).</li>
<li>Growing the request buffer with <code>tprealloc()</code> keeps it
system-owned: the ownership follows the possibly-moved buffer, the
system still reclaims it afterwards, returning the grown buffer as
the reply works, and freeing the reallocated buffer early is equally
safe (verified: flat server memory over 20,000 calls with 2 MB
reallocations, no crash over 5,000 early frees).</li>
<li>The <em>reply</em> buffer you pass in <code>data</code> is copied
out and then freed by the system. Never tpfree() it yourself; that is
a double free. The same applies to buffers handed to
<code>tpforward()</code>.</li>
</ul>
<p><strong>C++.</strong> tpreturn() does not return: the service frame
is torn down without running destructors, so RAII guards,
<code>std::string</code>s and locks declared in the service body never
clean up (a frequent source of leaks and deadlocks). Release resources
explicitly before the call, keep short-lived objects in inner scopes
that close earlier, or end the service with C-style cleanup and a
single tpreturn at the bottom.</p>""",
        "versions": None,
        "example": ("advsvr.c", "tpreturn"),
        "see_also": ["tpforward", "tpsvrinit"],
    },
    "tpadvertise": {
        "title": "advertise a service at runtime (server only)",
        "group": "XATMI: server",
        "synopsis": "int tpadvertise(char *svcname,\n"
                    "                  void (*func)(TPSVCINFO *));",
        "description": """
<p>tpadvertise() binds a service name to a service function so requests
start arriving at this server. It is the runtime equivalent of the
buildserver() <code>-s</code> table and the usual content of
<code>tpsvrinit()</code>; it can also be called later to switch a name
to a different implementation.</p>""",
        "args": [
            ("svcname", "The service name to advertise."),
            ("func", "The function that handles it, with the "
                     "TPSVCINFO signature."),
        ],
        "flags": None,
        "errors": [
            ("TPELIMIT", "The service table is full (MAXSERVICES)."),
            ("TPEINVAL", "svcname or func is invalid."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": None,
        "versions": None,
        "example": ("advsvr.c", "tpadvertise"),
        "see_also": ["tpunadvertise", "tpadvertisex", "tpsvrinit"],
    },
    "tpadvertisex": {
        "title": "advertise a service with placement flags",
        "group": "XATMI: server",
        "synopsis": "int tpadvertisex(char *svcname,\n"
                    "                   void (*func)(TPSVCINFO *),\n"
                    "                   long flags);",
        "description": """
<p>tpadvertisex() is tpadvertise() with placement control, and the
flags are the point of the call. flags=0 behaves exactly like
<code>tpadvertise()</code>: the name binds to the function on the
server's main request queue, replacing the binding when the name is
already advertised on this server with the same function. The other
combinations move the advertisement or guard it:</p>""",
        "args": [
            ("svcname", "The service name."),
            ("func", "The handler function, declared with buildserver "
                     "-s."),
            ("flags", "0, TPSINGLETON, TPSECONDARYRQ, or both ORed."),
        ],
        "flags": [
            ("0", "Plain advertisement on the main request queue, like "
                  "tpadvertise()."),
            ("TPSINGLETON", "Advertise on the main queue and require the "
                            "name to be unique in the application: when "
                            "another server already advertises it, the "
                            "call fails with TPENOSINGLETON."),
            ("TPSECONDARYRQ", "Advertise on the server's secondary "
                              "request queue, which requires "
                              "SECONDARYRQ=Y in the server "
                              "configuration; otherwise the call fails "
                              "with TPENOSECONDARYRQ. Multiple servers "
                              "may advertise the same name this way."),
            ("TPSINGLETON | TPSECONDARYRQ", "Both: the secondary-queue "
                                            "placement plus the "
                                            "uniqueness check, so either "
                                            "TPENOSECONDARYRQ or "
                                            "TPENOSINGLETON can fail the "
                                            "call."),
        ],
        "errors": [
            ("TPENOSINGLETON", "TPSINGLETON was set and another server "
                               "already advertises the name."),
            ("TPENOSECONDARYRQ", "TPSECONDARYRQ was set and the server "
                                 "is not booted with SECONDARYRQ=Y."),
            ("TPEMATCH", "The name is advertised on this server with a "
                         "different function."),
            ("TPELIMIT", "The service table is full."),
            ("TPEINVAL", "Invalid name, function, or flags."),
            ("TPESYSTEM", "Platform failure."),
        ],
        "notes": """
<p>Services on the secondary queue show up in the MIB with
<code>TA_RQADDR=UNKNOWN</code>, which distinguishes them from
main-queue advertisements. The four flag combinations were mapped out
in <a href="https://aivarsk.com/2019/10/27/tpadvertisex-solved/">the
tpadvertisex post on the author's blog</a>; Oracle's own documentation
of the flags is thin.</p>""",
        "versions": None,
        "example": ("advsvr.c", "tpadvertisex"),
        "see_also": ["tpadvertise", "tpunadvertise"],
    },

    "tpunadvertise": {
        "title": "withdraw a service advertisement (server only)",
        "group": "XATMI: server",
        "synopsis": "int tpunadvertise(char *svcname);",
        "description": """
<p>tpunadvertise() removes this server's advertisement of a name. New
requests for the name go elsewhere or fail with TPENOENT when no other
server advertises it; requests already queued are still delivered.</p>""",
        "args": [("svcname", "The name to withdraw.")],
        "flags": None,
        "errors": [
            ("TPENOENT", "The name was not advertised by this server."),
            ("TPEINVAL", "Invalid name."),
        ],
        "notes": None,
        "versions": None,
        "example": ("advsvr.c", "tpunadvertise"),
        "see_also": ["tpadvertise", "tpsvrdone"],
    },

    # --- FML32 -------------------------------------------------------------
    "fml32": {
        "title": "fielded buffers: the model",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "// the FML32 buffer type and its calls\n"
                    "#include <fml32.h>",
        "description": """
<p>FML32 is the self-describing buffer type of the API: a buffer is a
collection of typed fields, each identified by a 32-bit field id and
able to occur multiple times. A field id packs a type and a number;
<code>Fldtype32()</code> and <code>Fldno32()</code> decode it and
<code>Fmkfldid32()</code> builds one. Field names come from field
tables compiled with <code>mkfldhdr32</code>.</p>
<p>Every call in this group takes a <code>FBFR32 *</code>, returns 0 or
a count on success and -1 on error, and reports the failure in
<code>Ferror32</code> (render it with <code>Fstrerror32()</code>).
Occurrence counts (<code>FLDOCC32</code>) are zero-based; -1 means all
occurrences where a call accepts it.</p>
<p>In ATMI programs the buffers come from <code>tpalloc("FML32", ...)</code>
and are released with <code>tpfree()</code>. Outside ATMI, libfml32
allocates with <code>Falloc32()</code>/<code>Ffree32()</code> and
resizes with <code>Fresize32()</code>.</p>
<p>Buffer internals such as field indexes may differ between
implementations; the API semantics and the Ferror32 contract are
identical. Projection and join calls are documented separately.</p>""",
        "args": [],
        "flags": None,
        "errors": [
            ("FEINVAL", "Invalid field id, occurrence, or argument."),
            ("FNOSPACE", "The buffer has no room; size the addition "
                         "with Fneeded32() and resize."),
            ("FNOTFLD", "The pointer is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", None),
        "see_also": ["Fadd32", "Fchg32", "Fboolev32", "Fnext32"],
    },
    "Ferror32": {
        "title": "the FML32 error indicator",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Ferror32;  // a macro wrapping thread-local "
                    "storage",
        "description": """
<p>Every FML32 call reports failure through Ferror32: on a -1 or NULL
return it holds one of the F* error codes (FEINVAL, FNOSPACE,
FNOTPRES, FSYNTAX, ...). Like <code>tperrno</code> it is not reset by
successful calls, so it is meaningful only right after a failure;
each thread holds its own value. Render the code with
<code>Fstrerror32()</code>.</p>""",
        "args": [],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Ferror32"),
        "see_also": ["Fstrerror32", "fml32"],
    },
    "Fstrerror32": {
        "title": "render an FML32 error code",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "char *Fstrerror32(int err);",
        "description": """
<p>Fstrerror32() returns the message text for an FML32 error code,
usually <code>Ferror32</code>. The pointer refers to a static buffer
that later calls may overwrite; copy it if it must survive.</p>""",
        "args": [("err", "The error code, typically Ferror32.")],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fstrerror32"),
        "see_also": ["Ferror32"],
    },
    "Fadd32": {
        "title": "append a field occurrence",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fadd32(FBFR32 *fbfr, FLDID32 fieldid,\n"
                    "           char *value, FLDLEN32 len);",
        "description": """
<p>Fadd32() appends a new occurrence of a field. For STRING fields len 0
stores strlen(value)+1; for other types len must match the field type
(0 for fixed-size numerics). The same append can be written with
<code>Fchg32()</code> and occurrence -1, which is the style used when
the code later edits the same field.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid", "The field to add."),
            ("value, len", "Value pointer and length."),
        ],
        "flags": None,
        "errors": [
            ("FEINVAL", "Bad field id or length."),
            ("FNOSPACE", "No room; see Fneeded32()."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fadd32"),
        "see_also": ["Fchg32", "Fdel32", "Fnext32"],
    },
    "Fchg32": {
        "title": "change a field occurrence (the Swiss Army knife)",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fchg32(FBFR32 *fbfr, FLDID32 fieldid,\n"
                    "           FLDOCC32 oc, char *value, FLDLEN32 len);",
        "description": """
<p>Fchg32() replaces the value of one occurrence, and with three edges
it covers the write half of the whole API. First, with occurrence -1 it
behaves exactly like <code>Fadd32()</code> and appends. Second,
changing an occurrence beyond the current count pads the intermediate
occurrences with default values (0 for numeric fields). Third, passing
NULL as the value deletes that occurrence, like
<code>Fdel32()</code>.</p>
<p>Read together: Fchg32(buf, id, -1, ...) appends, Fchg32(buf, id, n,
...) sets, Fchg32(buf, id, n, NULL, ...) deletes, so most programs
need nothing else for field writes.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid", "The field to change."),
            ("oc", "Zero-based occurrence; -1 appends."),
            ("value, len", "New value; NULL deletes the occurrence."),
        ],
        "flags": None,
        "errors": [
            ("FEINVAL", "Bad field id or length."),
            ("FNOSPACE", "No room for a padded append; see "
                         "Fneeded32()."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": """
<p>These three behaviors are demonstrated with runnable programs in
<a href="https://aivarsk.com/2021/06/25/fchg32/">the Fchg32 post on
the author's blog</a>.</p>""",
        "versions": None,
        "example": ("fmldemo.c", "Fchg32"),
        "see_also": ["Fadd32", "Fget32", "Fdel32"],
    },
    "Fget32": {
        "title": "read a field occurrence",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fget32(FBFR32 *fbfr, FLDID32 fieldid,\n"
                    "           FLDOCC32 oc, char *loc, FLDLEN32 "
                    "maxlen);",
        "description": """
<p>Fget32() copies the value of one occurrence into the caller's
storage. loc must hold the field's value (for STRING, maxlen bytes);
fixed-size fields ignore maxlen. Returns -1 with Ferror32 FNOTPRES
when the occurrence does not exist.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid, oc", "Field and zero-based occurrence."),
            ("loc, maxlen", "Output storage and its size."),
        ],
        "flags": None,
        "errors": [
            ("FNOTPRES", "The field or occurrence is not present."),
            ("FEINVAL", "Bad field id."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fget32"),
        "see_also": ["Fnext32", "Flen32", "Fchg32"],
    },
    "Fdel32": {
        "title": "delete field occurrences",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fdel32(FBFR32 *fbfr, FLDID32 fieldid, "
                    "FLDOCC32 oc);",
        "description": """
<p>Fdel32() removes one occurrence of a field, or every occurrence when
oc is -1. Deleting with <code>Fchg32(..., NULL, ...)</code> is
equivalent for a single occurrence.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid", "The field to delete from."),
            ("oc", "Occurrence, or -1 for all."),
        ],
        "flags": None,
        "errors": [
            ("FNOTPRES", "The field or occurrence is not present."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fdel32"),
        "see_also": ["Fchg32", "Fadd32"],
    },
    "Fnext32": {
        "title": "iterate over all fields",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fnext32(FBFR32 *fbfr, FLDID32 *fieldid,\n"
                    "            FLDOCC32 *oc, char *value, FLDLEN32 "
                    "*len);",
        "description": """
<p>Fnext32() walks the buffer in field order. Start with
<code>*fieldid = FIRSTFLDID</code> and <code>*oc = 0</code>; each call
reports the next field and occurrence. The return value is 1 while a
field was returned, 0 when the walk is done, and -1 on error
(Ferror32), so loops terminate on <code>!= 1</code>. value and len may
be NULL to enumerate without copying; len is in/out and must be reset
to the buffer size before each call.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid, oc", "In/out walk position."),
            ("value, len", "Optional output for the value; len must "
                           "hold value's size on input."),
        ],
        "flags": None,
        "errors": [
            ("FNOSPACE", "value is smaller than the field; *len "
                         "receives the needed size."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fnext32"),
        "see_also": ["Fget32", "Fprint32"],
    },
    "Fprint32": {
        "title": "print a buffer to stdout",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fprint32(FBFR32 *fbfr);",
        "description": """
<p>Fprint32() prints the buffer in the readable text form, one field
per line, to stdout. <code>Ffprint32()</code> writes the same form to
any FILE; <code>Fwrite32()</code> is the binary counterpart for
round-tripping.</p>""",
        "args": [("fbfr", "The FML32 buffer.")],
        "flags": None,
        "errors": [
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fprint32"),
        "see_also": ["Ffprint32", "Fwrite32", "Fread32"],
    },
    "Ffprint32": {
        "title": "print a buffer to a FILE",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Ffprint32(FBFR32 *fbfr, FILE *iop);",
        "description": """
<p>Ffprint32() is Fprint32() with the destination as a FILE*, for
writing the readable text form to a log or a file.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("iop", "Destination stream, opened for writing."),
        ],
        "flags": None,
        "errors": [
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Ffprint32"),
        "see_also": ["Fprint32", "Fboolpr32"],
    },
    "Fwrite32": {
        "title": "write a buffer to a FILE (binary)",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fwrite32(FBFR32 *fbfr, FILE *iop);",
        "description": """
<p>Fwrite32() writes the buffer in the binary transfer form to a FILE.
The result is read back with <code>Fread32()</code>, giving a simple
persistence and interprocess format for fielded buffers.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("iop", "Destination stream."),
        ],
        "flags": None,
        "errors": [
            ("FNOSPACE", "Write failed."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fwrite32"),
        "see_also": ["Fread32", "Ffprint32"],
    },
    "Fread32": {
        "title": "read a buffer from a FILE (binary)",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fread32(FBFR32 *fbfr, FILE *iop);",
        "description": """
<p>Fread32() loads a buffer stored by <code>Fwrite32()</code>. The
target buffer must have room for the stored content; a short buffer
fails with FNOSPACE.</p>""",
        "args": [
            ("fbfr", "The target FML32 buffer."),
            ("iop", "Source stream."),
        ],
        "flags": None,
        "errors": [
            ("FNOSPACE", "fbfr is too small."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fread32"),
        "see_also": ["Fwrite32", "Fresize32"],
    },
    "Fboolco32": {
        "title": "compile a boolean expression",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "char *Fboolco32(char *expression);",
        "description": """
<p>Fboolco32() compiles a boolean expression into an evaluation tree
that <code>Fboolev32()</code> and <code>Ffloatev32()</code> evaluate
and <code>Fboolpr32()</code> prints. Field names in the expression are
resolved through the loaded field tables (FLDTBLDIR32/FIELDTBLS32) at
compile time. The returned tree is allocated with malloc() and
released with free(); a NULL return reports the failure in Ferror32.</p>
<p><strong>The compiled tree is a length-prefixed byte array.</strong>
The first two bytes hold the least and most significant byte of an
unsigned 16-bit length of the entire array, including the two size
bytes themselves. To copy a compiled tree, read that 16-bit value and
copy exactly that many bytes; the copy evaluates identically
(verified on Oracle 12.2.2 and 22.1, where the prefix is unchanged
and trees for the same expression are byte-identical across versions,
and on fuxedo, which implements the same prefix contract). The tree
encoding itself is platform-private: a tree compiled by Oracle
Tuxedo is not guaranteed to evaluate under fuxedo and vice versa, so
ship the expression text and recompile rather than the compiled
tree. The 16-bit size also bounds the tree at 65535 bytes.</p>""",
        "args": [
            ("expression", "The expression text, for example "
                           "\"AMOUNT > 200 &amp;&amp; FIRSTNAME == "
                           "'Ada'\". Comparisons, &amp;&amp;, ||, !, "
                           "arithmetic and parentheses are supported."),
        ],
        "flags": None,
        "errors": [
            ("FSYNTAX", "The expression does not parse."),
            ("FBADNAME", "A field name is unknown."),
            ("FEBADOP", "A field has a type the expression language "
                        "does not support."),
        ],
        "notes": None,
        "versions": "The expression language itself differs between "
                    "12.2 and 22.1. Adjacent unary operators (!!x, ~~x, "
                    "- -x) are syntax errors on 22.1 and compile on "
                    "12.2. Nested ~ evaluates once on 22.1 ( ~(~1) == "
                    "-2 ) but as a proper involution on 12.2 ( == 1 ). "
                    "String escapes: 12.2 decodes \\6E-style hex, "
                    "22.1 does not. Double-to-string comparisons "
                    "format with %f on 12.2 (\"1.000000\") and %g on "
                    "22.1 (\"1\"), so '1.000000' == 1.0 is true on "
                    "12.2 and false on 22.1.",
        "example": ("fmldemo.c", "Fboolco32"),
        "see_also": ["Fboolev32", "Fboolpr32"],
    },
    "Fboolev32": {
        "title": "evaluate a boolean expression over a buffer",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "int Fboolev32(FBFR32 *fbfr, char *tree);",
        "description": """
<p>Fboolev32() evaluates a boolean expression against the buffer and
returns 1 when it matches, 0 when it does not, -1 on a malformed
expression. Expressions compare fields with =, !=, &gt;, &lt;, &amp;,
| and parentheses, for example <code>"AMOUNT > 200 &amp;&amp;
FIRSTNAME == 'Ada'"</code>. <code>Fboolpr32()</code> prints the parsed
tree.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("tree", "The boolean expression text."),
        ],
        "flags": None,
        "errors": [
            ("FSYNTAX", "The expression does not parse."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": "On NULL fbfr or tree, 12.2 sets Ferror32 to "
                    "FNOTFLD while 22.1 leaves it unset or sets "
                    "FALIGNERR. A missing field in arithmetic counts "
                    "as 0 on 12.2 (1+missing == 1); 22.1 yields 0 for "
                    "the whole expression. Expression-language "
                    "differences are listed on the Fboolco32 page.",
        "example": ("fmldemo.c", "Fboolev32"),
        "see_also": ["Fboolpr32", "Fget32"],
    },
    "Fboolpr32": {
        "title": "print a parsed boolean expression tree",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "void Fboolpr32(char *tree, FILE *iop);",
        "description": """
<p>Fboolpr32() parses the same expression language as
<code>Fboolev32()</code> and prints the tree to a FILE, which is how
you debug a filter that does not do what you meant.</p>""",
        "args": [
            ("tree", "The boolean expression text."),
            ("iop", "Destination stream."),
        ],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fboolpr32"),
        "see_also": ["Fboolev32"],
    },
    "Fmkfldid32": {
        "title": "build a field id from a type and a number",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "FLDID32 Fmkfldid32(int type, FLDID32 num);",
        "description": """
<p>Fmkfldid32() packs a field type and number into a field id.
<code>Fldtype32()</code> and <code>Fldno32()</code> unpack it again.
Field ids from mkfldhdr32-generated headers are already packed; this is
for code that builds ids dynamically.</p>""",
        "args": [
            ("type", "A FLD_* type constant."),
            ("num", "The field number."),
        ],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", None),
        "see_also": ["Fadd32", "fml32"],
    },
    "Fneeded32": {
        "title": "size an addition before making it",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "long Fneeded32(FLDOCC32 count, FLDLEN32 len);",
        "description": """
<p>Fneeded32() returns the buffer bytes a new field with the given
occurrence count and value length needs, so callers can grow the
buffer (or size a tpalloc) before Fadd32/Fchg32 fails with FNOSPACE.</p>""",
        "args": [
            ("count", "Occurrence count to make room for."),
            ("len", "Value length per occurrence."),
        ],
        "flags": None,
        "errors": [],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fsizeof32"),
        "see_also": ["Fsizeof32", "Fadd32"],
    },
    "Fsizeof32": {
        "title": "current buffer size",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "long Fsizeof32(FBFR32 *fbfr);",
        "description": """
<p>Fsizeof32() returns the number of bytes the buffer currently uses.
Compare with Fneeded32() to decide when a resize is due.</p>""",
        "args": [("fbfr", "The FML32 buffer.")],
        "flags": None,
        "errors": [
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Fsizeof32"),
        "see_also": ["Fneeded32", "Flen32"],
    },
    "Flen32": {
        "title": "stored length of one occurrence",
        "group": "FML32",
        "header": "fml32.h",
        "synopsis": "long Flen32(FBFR32 *fbfr, FLDID32 fieldid, "
                    "FLDOCC32 oc);",
        "description": """
<p>Flen32() returns the stored length of one field occurrence: the
string length including the terminator for STRING, the type size for
fixed types. -1 with Ferror32 FNOTPRES when the occurrence is absent.</p>""",
        "args": [
            ("fbfr", "The FML32 buffer."),
            ("fieldid, oc", "Field and occurrence."),
        ],
        "flags": None,
        "errors": [
            ("FNOTPRES", "The field or occurrence is not present."),
            ("FNOTFLD", "fbfr is not an FML32 buffer."),
        ],
        "notes": None,
        "versions": None,
        "example": ("fmldemo.c", "Flen32"),
        "see_also": ["Fget32", "Fsizeof32"],
    },
}

TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{name}() - {title} | Fuxedo API reference</title>
<meta name="description" content="{meta}">
<meta name="keywords" content="{name}, {name} example, Oracle Tuxedo {name}, Tuxedo C API, Fuxedo, XATMI{kw_extra}">
<link rel="canonical" href="https://fuxedo.io/api/{name}/">
<meta property="og:type" content="article">
<meta property="og:url" content="https://fuxedo.io/api/{name}/">
<meta property="og:title" content="{name}() - {title}">
<meta property="og:description" content="{meta}">
<meta name="twitter:card" content="summary">
<meta name="twitter:title" content="{name}() - {title}">
<meta name="twitter:description" content="{meta}">
<script type="application/ld+json">
{{
  "@context": "https://schema.org",
  "@graph": [
    {{
      "@type": "TechArticle",
      "headline": "{name}() - {title}",
      "description": "{meta}",
      "url": "https://fuxedo.io/api/{name}/",
      "isPartOf": {{"@type": "WebSite", "name": "Fuxedo API reference", "url": "https://fuxedo.io/api/"}},
      "programmingLanguage": "C",
      "license": "https://opensource.org/licenses/MIT"
    }},
    {{
      "@type": "BreadcrumbList",
      "itemListElement": [
        {{"@type": "ListItem", "position": 1, "name": "Fuxedo", "item": "https://fuxedo.io/"}},
        {{"@type": "ListItem", "position": 2, "name": "API reference", "item": "https://fuxedo.io/api/"}},
        {{"@type": "ListItem", "position": 3, "name": "{name}"}}
      ]
    }}
  ]
}}
</script>
<link rel="stylesheet" href="/styles.css">
<link rel="stylesheet" href="/api/api.css">
</head>
<body>
<div class="page">
<h1><img src="/fuxedo.svg" alt="Fuxedo logo">{name}()</h1>
<p class="tagline">{title}</p>

<div class="api-layout">
<nav class="api-nav">
{nav}
</nav>

<div class="api-main">

<h2>Synopsis</h2>
<pre><code>#include &lt;{header}&gt;

{synopsis}</code></pre>

<h2>Description</h2>
{description}

{args_section}
{flags_section}
{errors_section}
{notes_section}
{versions_section}
<h2>Example</h2>
<p class="example-file">Full source: <code>docs/api/examples/{example_file}</code>
(build and run with <code>make -C docs/api/examples check
TUXDIR=&lt;install&gt;</code>); {example_note}.</p>
<pre><code>{example_code}</code></pre>

<p>See also: {see_also}.</p>

<p class="compat-line">fuxedo: supported.
Cross-checked against Oracle Tuxedo 12.2.2 and 22.1
with the API hardening suite.</p>

</div>
</div>

<footer>
<p>Part of the <a href="/api/">Fuxedo API reference</a>.
<a href="https://github.com/fuxedo/fuxedo">Fuxedo</a> is the open source
alternative to Oracle Tuxedo: same API, same commands, same System V IPC
mechanics.</p>
</footer>
</div>
</body>
</html>
"""


def section(title, rows, head):
    if not rows:
        return ""
    body = "".join(f"<tr><td>{a}</td><td>{b}</td></tr>\n" for a, b in rows)
    return (f"<h2>{title}</h2>\n<table>\n<tr><th>{head}</th>"
            f"<th>Description</th></tr>\n{body}</table>\n")


def load_example(fname, region):
    src = (EXAMPLES / fname).read_text().splitlines()

    # marker lines never reach the HTML. For a single-purpose page
    # (region None) they are simply stripped; for a shared file the
    # named region's lines are highlighted instead. Regions may nest;
    # each region's span is its own marker pair.
    out = []
    stack = []
    spans = {}
    for line in src:
        stripped = line.strip()
        if stripped.startswith("// doc:region "):
            stack.append((stripped[len("// doc:region "):], len(out)))
            continue
        if stripped == "// doc:end":
            if not stack:
                raise SystemExit(f"unbalanced // doc:end in {fname}")
            name, start = stack.pop()
            spans[name] = (start, len(out))
            continue
        out.append(html.escape(line))
    if stack:
        raise SystemExit(f"region {stack[-1][0]} not closed in {fname}")
    if region is None:
        return "\n".join(out)
    if region not in spans:
        raise SystemExit(f"region {region} not found in {fname}")
    start, end = spans[region]
    if start == end:
        raise SystemExit(f"region {region} is empty in {fname}")
    out[start] = '<span class="hl">' + out[start]
    out[end - 1] = out[end - 1] + '</span>'
    return "\n".join(out)


def nav_html(current):
    parts = []
    for group, names in GROUPS:
        links = "\n".join(
            f'<li><a{" class=\"here\"" if n == current else ""} '
            f'href="/api/{n}/">{n}</a></li>' for n in names)
        parts.append(f"<h2>{group}</h2>\n<ul>\n{links}\n</ul>")
    return "\n".join(parts)


def meta_for(name, entry):
    return (f"{name}() - {entry['title']}. Reference for Oracle Tuxedo "
            f"and Fuxedo: arguments, flags, error codes with tperrno, "
            f"complete C example. Cross-checked against Tuxedo 12.2 "
            f"and 22.1.")


def render(name, entry):
    args = section("Arguments", entry.get("args"), "Argument")
    flags = ""
    if entry.get("flags"):
        flags = section("Flags", entry["flags"], "Flag")
        if entry.get("flags_note"):
            flags += f"<p>{entry['flags_note']}</p>\n"
    errors = section("Errors (tperrno)", entry.get("errors"), "Code")
    notes = ""
    if entry.get("notes"):
        notes = f"<h2>Notes</h2>\n{entry['notes']}\n"
    see = ", ".join(
        f'<a href="/api/{s}/">{s}</a>' if s in FUNCTIONS else s
        for s in entry["see_also"])
    versions_section = ""
    if entry.get("versions"):
        versions_section = ("<h2>12.2 vs 22.1</h2>\n<p>"
                            + entry["versions"] + "</p>\n")
    ex_file, ex_region = entry["example"]
    example_note = ("the lines documenting %s() are highlighted" % name
                    if ex_region else
                    "the whole program demonstrates %s()" % name)
    return TEMPLATE.format(
        name=name, title=entry["title"], meta=meta_for(name, entry),
        kw_extra="," + ",".join(entry["see_also"]),
        synopsis=html.escape(entry["synopsis"]),
        description=entry["description"], args_section=args,
        flags_section=flags, errors_section=errors, notes_section=notes,
        versions=entry["versions"],
        header=entry.get("header", "atmi.h"),
        versions_section=versions_section,
        example_file=ex_file, example_note=example_note,
        example_code=load_example(ex_file, ex_region),
        see_also=see, nav=nav_html(name))


def write_index():
    rows = []
    for group, names in GROUPS:
        for n in names:
            e = FUNCTIONS[n]
            rows.append(f'<tr><td><a href="/api/{n}/">{n}</a></td>'
                        f"<td>{e['title']}.</td></tr>")
    diffs = "\n".join(
        f'<li><a href="/api/{n}/">{n}</a>: {FUNCTIONS[n]["versions"]}'
        for group, names in GROUPS for n in names
        if FUNCTIONS[n].get("versions"))
    page = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>XATMI and FML32 API reference for Oracle Tuxedo and Fuxedo</title>
<meta name="description" content="C API reference for the X/Open XATMI calls (tpinit, tpcall, tpacall, tpgetrply, tpbegin, tpcommit) and FML32 (Fadd32, Fchg32), written for Oracle Tuxedo and Fuxedo. Error codes, tperrno semantics, flag tables, complete compilable C examples.">
<meta name="keywords" content="XATMI API reference, Tuxedo API, tpcall, tpacall, tpgetrply, tpinit, tpalloc, FML32, tperrno, Oracle Tuxedo C API, Fuxedo documentation">
<link rel="canonical" href="https://fuxedo.io/api/">
<meta property="og:type" content="website">
<meta property="og:url" content="https://fuxedo.io/api/">
<meta property="og:title" content="XATMI and FML32 API reference">
<meta property="og:description" content="Reference for the Tuxedo C API: tp* calls, flags, error codes with tperrno, complete C examples. Applies to Oracle Tuxedo and Fuxedo.">
<meta name="twitter:card" content="summary">
<meta name="twitter:title" content="XATMI and FML32 API reference">
<meta name="twitter:description" content="The Tuxedo C API tersely: tp* calls, flags, error codes, complete C examples. For Oracle Tuxedo and Fuxedo.">
<script type="application/ld+json">
{{
  "@context": "https://schema.org",
  "@type": "CollectionPage",
  "name": "Fuxedo API reference",
  "description": "C API reference for the X/Open XATMI calls, for Oracle Tuxedo and Fuxedo.",
  "url": "https://fuxedo.io/api/",
  "isPartOf": {{"@type": "WebSite", "name": "Fuxedo", "url": "https://fuxedo.io/"}},
  "hasPart": [
    {", ".join('{"@type": "TechArticle", "headline": "%s() - %s", "url": "https://fuxedo.io/api/%s/"}' % (n, FUNCTIONS[n]["title"].replace('"', "'"), n) for names in [g[1] for g in GROUPS] for n in names)}
  ]
}}
</script>
<link rel="stylesheet" href="/styles.css">
<link rel="stylesheet" href="/api/api.css">
</head>
<body>
<div class="page">

<h1><img src="/fuxedo.svg" alt="Fuxedo logo">API reference</h1>
<p class="tagline">the Tuxedo C API, tersely</p>

<div class="api-layout">
<nav class="api-nav">
{nav_html(None)}
</nav>

<div class="api-main">

<p>This is the reference for the C API shared by Oracle Tuxedo and
<a href="https://github.com/fuxedo/fuxedo">Fuxedo</a>: the X/Open XATMI
calls (<code>tp*</code>) and the FML32 fielded buffer calls
(<code>F*32</code>). It is written for people who already program against
the API and want exact flags, error codes, and edge behavior without
page-filler.</p>

<p>Every page embeds a <strong>complete, compilable</strong> program from
<code>docs/api/examples</code>, with the documented call highlighted;
the whole app builds and runs with <code>make -C docs/api/examples
check TUXDIR=&lt;install&gt;</code> against either Tuxedo or Fuxedo.</p>

<p>Conventions:</p>
<ul>
<li>Error tables list the <code>tperrno</code> code and the condition
that sets it. Only codes a real call can produce are listed.</li>
<li>A successful call does not reset <code>tperrno</code>; the value
is meaningful only after -1. This matches Oracle Tuxedo behavior that
production code depends on.</li>
<li>Where Oracle Tuxedo 12.2 and 22.1 differ, the difference is stated
on the function's page; pages without such a note were cross-checked on
both and behave the same.</li>
<li>Every page carries a compatibility line saying whether fuxedo
implements the call and how it was verified.</li>
</ul>

<h2>Known 12.2 vs 22.1 differences</h2>
<p>Most of the API behaves identically on Oracle Tuxedo 12.2 and 22.1.
The functions below carry pinned behavioral differences; each page
spells out what differs. fuxedo defaults to the 12.x behavior.</p>
{diffs}
<h2>Index</h2>
<table>
<tr><th>Function</th><th>What it does</th></tr>
{chr(10).join(rows)}
</table>

</div>
</div>

<footer>
<p><a href="/">Fuxedo</a> is the open source alternative to Oracle
Tuxedo: same API, same commands, same System V IPC mechanics. Migration
is a recompile away.</p>
</footer>
</div>
</body>
</html>
"""
    (ROOT / "index.html").write_text(page)


def write_sitemap():
    static = ["", "what-is-tuxedo/", "quickstart/", "migration/",
              "training/", "status/", "faq/", "api/"]
    urls = [f"https://fuxedo.io/{p}" for p in static]
    urls += [f"https://fuxedo.io/api/{n}/"
             for names in [g[1] for g in GROUPS] for n in names]
    body = "\n".join(f"  <url><loc>{u}</loc></url>" for u in urls)
    (ROOT.parent / "sitemap.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        f"{body}\n</urlset>\n")


def validate_jsonld():
    # every embedded JSON-LD block must parse; a broken one means Google
    # drops the page's structured data silently
    for p in sorted(ROOT.parent.rglob("*.html")):
        for m in re.finditer(
                r'<script type="application/ld\+json">(.*?)</script>',
                p.read_text(), re.S):
            try:
                json.loads(m.group(1))
            except Exception as e:
                raise SystemExit(f"invalid JSON-LD in {p}: {e}")


def main():
    for name, entry in FUNCTIONS.items():
        d = ROOT / name
        d.mkdir(exist_ok=True)
        (d / "index.html").write_text(render(name, entry))
        print(f"api/{name}/")
    write_index()
    write_sitemap()
    validate_jsonld()
    print("api/index.html, sitemap.xml")


if __name__ == "__main__":
    main()
