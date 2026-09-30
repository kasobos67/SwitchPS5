// SPDX-License-Identifier: GPL-3.0-or-later
// Log streams without storage stalls. On the console a small write to a file under /data takes
// ~25 ms or more (20 unbuffered lines: 474 ms), and the frontend writes stderr lines during game
// shutdown and flushes stdout from the GPU thread. Attach() points the stream's descriptor at a
// pipe; a background thread copies the pipe into the log file, so printing only waits for the
// pipe. Data still in the pipe when the process is killed is lost (milliseconds' worth).
#pragma once
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

namespace Eden {
class LogPipe {
public:
    LogPipe() = default;
    LogPipe(const LogPipe&) = delete;
    LogPipe& operator=(const LogPipe&) = delete;
    ~LogPipe() { Detach(); }

    // The stream must already write to its log file; on failure it keeps doing so.
    bool Attach(std::FILE* target) {
        if (stream) return false;
        std::fflush(target);
        const int stream_fd = fileno(target);
        int ends[2];
        // fcntl is a libkernel import; the SDK's static dup() is a direct syscall, which native
        // titles may not make.
        if (stream_fd < 0 || (file_fd = fcntl(stream_fd, F_DUPFD, 3)) < 0) return false;
        if (pipe(ends) != 0) {
            close(file_fd);
            file_fd = -1;
            return false;
        }
        if (dup2(ends[1], stream_fd) < 0) {
            close(ends[0]);
            close(ends[1]);
            close(file_fd);
            file_fd = -1;
            return false;
        }
        close(ends[1]);
        read_fd = ends[0];
        stream = target;
        worker = std::thread([this] { Drain(); });
        return true;
    }

    // Writes go straight to the file again once the pipe has been copied out.
    void Detach() {
        if (!stream) return;
        std::fflush(stream);
        // Replacing the pipe's only write end ends the copy after the remaining data.
        dup2(file_fd, fileno(stream));
        worker.join();
        close(read_fd);
        close(file_fd);
        read_fd = file_fd = -1;
        stream = nullptr;
    }

private:
    void Drain() {
        char buffer[16384];
        for (;;) {
            const ssize_t count = read(read_fd, buffer, sizeof(buffer));
            if (count == 0) return;
            if (count < 0) {
                if (errno == EINTR) continue;
                return;
            }
            for (ssize_t written = 0; written < count;) {
                const ssize_t result = write(file_fd, buffer + written, static_cast<size_t>(count - written));
                if (result < 0 && errno == EINTR) continue;
                if (result <= 0) break;
                written += result;
            }
        }
    }

    std::FILE* stream = nullptr;
    int read_fd = -1;
    int file_fd = -1;
    std::thread worker;
};
}
