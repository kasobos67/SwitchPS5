#!/usr/bin/env python3
"""Exercise the native directory adapter with host getdents and malformed records."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include "native_directory.h"
#include <cassert>
#include <cstdio>
#include <sys/syscall.h>
static int fault, calls, last_fd;
extern "C" int sceKernelGetdents(int fd, char* bytes, int size) {
    last_fd = fd;
    assert(size >= 16384);
    if (fault == 1) return -1;
    if (fault == 2) { std::memset(bytes, 0, 32); return 32; }
    if (fault == 3) {
        if (calls++) return 0;
        std::memset(bytes, 0, 512);
        uint16_t length = 512;
        std::memcpy(bytes + offsetof(dirent, d_reclen), &length, sizeof(length));
        std::strcpy(bytes + offsetof(dirent, d_name), "padded");
        return 512;
    }
    return syscall(SYS_getdents64, fd, bytes, size);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path path{argv[1]};
    std::error_code error;
    auto entries = Eden::ReadNativeDirectory(path, error);
    assert(!error && entries.size() == 2);
    for (const auto& entry : entries) assert(entry.path().parent_path() == path);
    assert(fcntl(last_fd, F_GETFD) == -1 && errno == EBADF);
    for (const auto& entry : entries) assert(std::filesystem::exists(entry.status()));
    for (fault = 1; fault <= 2; ++fault) {
        entries = Eden::ReadNativeDirectory(path, error);
        assert(error && entries.empty());
        assert(fcntl(last_fd, F_GETFD) == -1 && errno == EBADF);
    }
    fault = 3;
    entries = Eden::ReadNativeDirectory(path, error);
    assert(!error && entries.size() == 1 && entries[0].path().filename() == "padded");
    entries = Eden::ReadNativeDirectory(path / "missing", error);
    assert(error && entries.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='eden-directory-') as folder:
    tmp = Path(folder)
    data = tmp / 'data'
    data.mkdir()
    (data / 'padded').write_text('fixture')
    (data / 'nested').mkdir()
    (tmp / 'check.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'headless'),
                    str(tmp / 'check.cpp'), '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check'), str(data)], check=True, timeout=10)
print('Native directory listing, padded/malformed records and descriptor cleanup PASS')
