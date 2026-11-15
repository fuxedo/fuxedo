"""Shared helpers for fuxedo pytests.

Run via the pytests/Makefile which sets TUXDIR, PATH, LD_LIBRARY_PATH and
PYTHONPATH (tuxedo-python module). Tests are plain python3 with asserts so
they can also run directly:

    cd pytests && python3 test_tmloadcf.py
"""

import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent
SCRATCH_ROOT = ROOT / "scratch"


def check_env():
    tuxdir = os.environ.get("TUXDIR")
    assert tuxdir, "TUXDIR is not set"
    return pathlib.Path(tuxdir)


def is_fuxedo(tuxdir=None):
    tuxdir = pathlib.Path(tuxdir or os.environ["TUXDIR"])
    lib = tuxdir / "lib"
    return any((lib / name).exists() for name in
               ("libfuxedo.so", "libfuxedo.so.0", "libfuxedo.dylib"))


class scratch:
    """Per-test scratch directory, removed on exit."""

    def __init__(self, name):
        self.dir = SCRATCH_ROOT / f"{name}-{os.getpid()}"

    def __enter__(self):
        shutil.rmtree(self.dir, ignore_errors=True)
        self.dir.mkdir(parents=True)
        return self.dir

    def __exit__(self, *exc):
        shutil.rmtree(self.dir, ignore_errors=True)
        return False


def run(args, input=None, expect=0, cwd=None, env=None):
    full_env = dict(os.environ)
    if env:
        full_env.update({k: str(v) for k, v in env.items()})
    proc = subprocess.run(
        [str(a) for a in args],
        input=input,
        capture_output=True,
        text=True,
        cwd=cwd,
        env=full_env,
    )
    if expect is not None:
        # a process exiting -1 shows up as 255 in subprocess
        failed = proc.returncode == 0 if expect < 0 else proc.returncode != expect
        if failed:
            raise AssertionError(
                f"{args[0]} exited {proc.returncode}, expected {expect}\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return proc


def main(globs):
    tests = sorted(
        (name, fn) for name, fn in globs.items()
        if name.startswith("test_") and callable(fn))
    assert tests, "no test_ functions found"
    for name, fn in tests:
        fn()
        print(f"{name}: OK")
    print(f"{globs['__file__']}: {len(tests)} tests passed")
