/*
 * MIT License
 *
 * Copyright (c) 2025 NewOro Corporation
 *
 * Permission is hereby granted, free of charge, to use, copy, modify, and distribute 
 * this software for any purpose with or without fee, provided that the above 
 * copyright notice appears in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#ifndef PIDNOTIFIER_H
#define PIDNOTIFIER_H

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>

const int kBufferSize = 32;

class SigpipeGuard {
 public:
  SigpipeGuard() noexcept {
    sa_.sa_handler = SIG_IGN;
    ::sigemptyset(&sa_.sa_mask);
    sa_.sa_flags = 0;
    ::sigaction(SIGPIPE, &sa_, &old_sa_);
  }
  ~SigpipeGuard() noexcept { ::sigaction(SIGPIPE, &old_sa_, nullptr); }
  SigpipeGuard(const SigpipeGuard&) = delete;
  SigpipeGuard& operator=(const SigpipeGuard&) = delete;

 private:
  struct sigaction sa_ {};
  struct sigaction old_sa_ {};
};

class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int descriptor) noexcept : fd_(descriptor) {}
  ~UniqueFd() { reset(); }

  UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  [[nodiscard]] int get() const noexcept { return fd_; }

  void reset(int new_fd = -1) noexcept {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = new_fd;
  }

 private:
  int fd_{-1};
};

class PidNotifier {
 public:
  explicit PidNotifier(std::string fifo_path)
      : fifo_path_(std::move(fifo_path)) {}

  [[nodiscard]] bool notify_now() const noexcept {
    const SigpipeGuard guard;

    const UniqueFd file_descriptor{
        ::open(fifo_path_.c_str(), O_WRONLY | O_NONBLOCK)};
    if (!file_descriptor.valid()) {
      return false;
    }

    std::array<char, kBufferSize> buf{};
    char* first = buf.data();
    char* last = first + buf.size();

    const pid_t pid = ::getpid();

    auto [ptr, error_code] = std::to_chars(first, last - 2, pid);
    if (error_code != std::errc{}) {
      return false;
    }
    *ptr++ = '\n';
    const auto len = static_cast<std::size_t>(ptr - first);

    std::size_t written = 0;
    while (written < len) {
      const ssize_t length =
          ::write(file_descriptor.get(), first + written, len - written);
      if (length > 0) {
        written += static_cast<std::size_t>(length);
        continue;
      }
      if (length == -1 && (errno == EAGAIN || errno == EINTR)) {
        continue;
      }
      return false;
    }
    return true;
  }

 private:
  std::string fifo_path_;
};

#endif  //PIDNOTIFIER_H
