# Drives the REPL through a pseudo-terminal, which is the only way to test
# the interactive path: banner, prompts, continuation prompt, history.
# Used by tests/cli.sh; skipped where python3 or pty is unavailable.
import fcntl
import os
import pty
import select
import struct
import sys
import termios
import time

def run(bin_path):
    pid, fd = pty.fork()
    if pid == 0:
        # A pseudo-terminal starts with a 0x0 window; linenoise would then ask
        # the terminal for the cursor position and wait for an answer forever.
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        os.environ["TERM"] = "xterm"
        os.execv(bin_path, [bin_path, "0.1"])
    out = b""
    pos = 0     # everything before `pos` was already matched by read_until

    def read_until(marker, timeout=5.0):
        # Waits for `marker` to appear after the previous match. Searching
        # from the start would accept an old prompt and send the next line
        # before linenoise is reading again; linenoise flushes typed-ahead
        # input when it switches the terminal mode, so that line would be lost.
        nonlocal out, pos
        deadline = time.time() + timeout
        while marker not in out[pos:]:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            ready, _, _ = select.select([fd], [], [], remaining)
            if not ready:
                break
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        at = out.find(marker, pos)
        if at < 0:
            return False
        pos = at + len(marker)
        return True

    assert read_until(b">>> "), out
    os.write(fd, b"print(7)\r")
    assert read_until(b"7\r\n"), out
    assert read_until(b">>> "), out
    # Up arrow recalls the previous line; Enter runs it again.
    os.write(fd, b"\x1b[A\r")
    assert read_until(b"7\r\n"), out
    assert read_until(b">>> "), out
    # A block shows the continuation prompt until a blank line.
    os.write(fd, b"if true:\r")
    assert read_until(b"... "), out
    os.write(fd, b"    print(8)\r")
    assert read_until(b"... "), out
    os.write(fd, b"\r")
    assert read_until(b"8\r\n"), out
    assert read_until(b">>> "), out
    # Ctrl-C in the middle of a block drops it and starts over.
    os.write(fd, b"if true:\r")
    assert read_until(b"... "), out
    os.write(fd, b"\x03")
    assert read_until(b">>> "), out
    os.write(fd, b"print(9)\r")
    assert read_until(b"9\r\n"), out
    assert read_until(b">>> "), out
    os.write(fd, b"\x04")           # Ctrl-D ends the session
    status = 0
    try:
        _, status = os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, (out, status)
    assert out.startswith(b"Arete 0.1 REPL"), out
    return out

if __name__ == "__main__":
    try:
        run(os.path.abspath(sys.argv[1]))
    except AssertionError as e:
        sys.stderr.write("FAIL repl-pty: unexpected transcript:\n" + repr(e.args[0]) + "\n")
        sys.exit(1)
    print("repl-pty: ok")
