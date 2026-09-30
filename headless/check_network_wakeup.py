#!/usr/bin/env python3
"""Exercise the generated native wakeup code using host POSIX sockets."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('int interrupt_pipe_fd[2]')
end = source.index('sockaddr TranslateFromSockAddrIn', start)
code = r'''
#include <cstdio>
#include <stdexcept>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
using u8 = uint8_t;
using SOCKET = int;
#define ASSERT(x) do { if (!(x)) throw std::runtime_error("assert"); } while (0)
#define LOG_ERROR(...) throw std::runtime_error("unexpected acknowledge failure")
''' + source[start:end] + r'''
int main() {
    Initialize();
    AcknowledgeInterrupt(); // Empty receive must not block.
    pollfd p{GetInterruptSocket(), POLLIN, 0};
    ASSERT(poll(&p, 1, 0) == 0);
    for (int i = 0; i < 3; ++i) {
        InterruptSocketOperations();
        ASSERT(poll(&p, 1, 0) == 1 && (p.revents & POLLIN));
        AcknowledgeInterrupt();
        ASSERT(poll(&p, 1, 0) == 0);
    }
    char byte = 0;
    while (send(interrupt_pipe_fd[1], &byte, 1, MSG_DONTWAIT) == 1) {}
    ASSERT(errno == EAGAIN || errno == EWOULDBLOCK);
    InterruptSocketOperations(); // Full stream already signals poll; must not block/fail.
    ASSERT(poll(&p, 1, 0) == 1);
    while (recv(interrupt_pipe_fd[0], &byte, 1, MSG_DONTWAIT) == 1) {}
    ASSERT(poll(&p, 1, 0) == 0);
    close(interrupt_pipe_fd[1]);
    interrupt_pipe_fd[1] = -1;
    bool rejected = false;
    try { InterruptSocketOperations(); } catch (const std::runtime_error&) { rejected = true; }
    ASSERT(rejected); // Invalid descriptors must remain a hard failure.
    Finalize();
}
'''
with tempfile.TemporaryDirectory(prefix='eden-wakeup-') as folder:
    cpp, binary = Path(folder) / 'check.cpp', Path(folder) / 'check'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=10)
print('Native wakeup: initialization, poll/read cycles, full-queue coalescing and bad-fd rejection PASS')
