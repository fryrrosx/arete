# Drives the REPL through a pseudo-terminal, which is the only way to test
# the interactive path: banner, prompts, continuation prompt, history.
# Used by tests/cli.sh (not on Windows). It runs with HOME set to a fresh
# temporary directory, so the developer's own ~/.arete_history is never
# read or written; where python3 cannot allocate a pty it reports itself
# as skipped.
import os
import select
import shutil
import sys
import tempfile
import time

try:
    import fcntl
    import pty
    import struct
    import termios
except ImportError as e:
    print("repl-pty: skipped (%s)" % e)
    sys.exit(0)

HISTORY_LINES = 1000


class Session:
    def __init__(self, bin_path, home):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            # A pseudo-terminal starts with a 0x0 window; linenoise would then
            # ask the terminal for the cursor position and wait forever.
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
            os.environ["TERM"] = "xterm"
            os.environ["HOME"] = home
            os.execv(bin_path, [bin_path, "0.1"])
        self.out = b""
        self.pos = 0     # everything before `pos` was already matched

    def read_until(self, marker, timeout=5.0):
        # Waits for `marker` to appear after the previous match. Searching
        # from the start would accept an old prompt and send the next line
        # before linenoise is reading again; linenoise flushes typed-ahead
        # input when it switches the terminal mode, so that line would be lost.
        deadline = time.time() + timeout
        while marker not in self.out[self.pos:]:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            ready, _, _ = select.select([self.fd], [], [], remaining)
            if not ready:
                break
            try:
                chunk = os.read(self.fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            self.out += chunk
        at = self.out.find(marker, self.pos)
        if at < 0:
            return False
        self.pos = at + len(marker)
        return True

    def send(self, data):
        os.write(self.fd, data)

    def expect(self, marker):
        assert self.read_until(marker), self.out

    def finish(self, exit_code=0):
        # Ctrl-D at an empty prompt ends the session; the exit code is 1
        # when some input of the session failed (SPEC section 2.1).
        self.send(b"\x04")
        status = 0
        try:
            _, status = os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass
        self.read_until(b"\x00", timeout=0.2)   # drain what is left
        assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == exit_code, (self.out, status)
        return self.out


def first_session(bin_path, home):
    s = Session(bin_path, home)
    s.expect(b">>> ")
    s.send(b"print(7)\r")
    s.expect(b"7\r\n")
    s.expect(b">>> ")
    # Up arrow recalls the previous line; Enter runs it again.
    s.send(b"\x1b[A\r")
    s.expect(b"7\r\n")
    s.expect(b">>> ")
    # A block shows the continuation prompt until a blank line.
    s.send(b"if true:\r")
    s.expect(b"... ")
    s.send(b"    print(8)\r")
    s.expect(b"... ")
    s.send(b"\r")
    s.expect(b"8\r\n")
    s.expect(b">>> ")
    # An open bracket shows the continuation prompt too.
    s.send(b"print((1 +\r")
    s.expect(b"... ")
    s.send(b"2))\r")
    s.expect(b"3\r\n")
    s.expect(b">>> ")
    # A blank line right after an opener runs it: the missing body is an error.
    s.send(b"if true:\r")
    s.expect(b"... ")
    s.send(b"\r")
    s.expect(b"expected an indented block")
    s.expect(b">>> ")
    # Ctrl-C in the middle of a block, and on a partial first line, drops it.
    s.send(b"if true:\r")
    s.expect(b"... ")
    s.send(b"\x03")
    s.expect(b">>> ")
    s.send(b"print(1\x03")
    s.expect(b">>> ")
    s.send(b"print(9)\r")
    s.expect(b"9\r\n")
    s.expect(b">>> ")
    out = s.finish(exit_code=1)       # the opener without a body was an error
    assert out.startswith(b"Arete 0.1 REPL"), out
    # Exactly one line end after the final prompt: linenoise prints it, the
    # REPL must not add another.
    assert out.endswith(b">>> \x1b[?2004l\r\n") or out.endswith(b">>> \r\n"), out[-40:]
    return out


def second_session(bin_path, home):
    # The history file was written; the new session loads it, so the first
    # up arrow recalls the last line of the previous session.
    s = Session(bin_path, home)
    s.expect(b">>> ")
    s.send(b"\x1b[A\r")
    s.expect(b"9\r\n")
    s.expect(b">>> ")
    s.finish()


def history_cap(bin_path, home):
    # More lines than the cap on disk: after a session the file holds
    # exactly HISTORY_LINES lines, the newest last.
    path = os.path.join(home, ".arete_history")
    with open(path, "w") as f:
        for i in range(HISTORY_LINES + 5):
            f.write("print(%d)\n" % i)
    s = Session(bin_path, home)
    s.expect(b">>> ")
    s.send(b"print(0)\r")
    s.expect(b"0\r\n")
    s.expect(b">>> ")
    s.finish()
    with open(path) as f:
        lines = f.read().splitlines()
    assert len(lines) == HISTORY_LINES, len(lines)
    assert lines[-1] == "print(0)", lines[-1]


def run(bin_path):
    home = tempfile.mkdtemp(prefix="arete-pty-")
    try:
        first_session(bin_path, home)
        with open(os.path.join(home, ".arete_history")) as f:
            assert f.read().splitlines()[-1] == "print(9)"
        second_session(bin_path, home)
        history_cap(bin_path, home)
    finally:
        shutil.rmtree(home, ignore_errors=True)


if __name__ == "__main__":
    try:
        run(os.path.abspath(sys.argv[1]))
    except AssertionError as e:
        sys.stderr.write("FAIL repl-pty: unexpected transcript:\n" + repr(e.args[0]) + "\n")
        sys.exit(1)
    except OSError as e:
        print("repl-pty: skipped (%s)" % e)
        sys.exit(0)
    print("repl-pty: ok")
