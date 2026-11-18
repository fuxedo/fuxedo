// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "ws.h"

#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <memory>

#include <atmi.h>
#include <userlog.h>

#include "misc.h"
#include "trx.h"
#include "wire.h"

namespace fux::ws {

namespace {

using steady = std::chrono::steady_clock;

bool split_nwaddr(const std::string &naddr, std::string &host, uint16_t &port) {
  // Oracle allows a comma-separated list and a // prefix; v1 takes the
  // first address
  auto s = naddr.substr(0, naddr.find(','));
  if (s.compare(0, 2, "//") == 0) {
    s = s.substr(2);
  }
  auto colon = s.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  host = s.substr(0, colon);
  auto p = std::stoul(s.substr(colon + 1));
  if (p == 0 || p > 65535) {
    return false;
  }
  port = static_cast<uint16_t>(p);
  return true;
}

}  // namespace

const char *ws_naddr() {
  auto *v = std::getenv("WSNADDR");
  return (v != nullptr && *v != '\0') ? v : nullptr;
}

wsconn &get_wsconn() {
  static thread_local std::unique_ptr<wsconn> conn;
  if (conn == nullptr) {
    conn = std::make_unique<wsconn>();
  }
  return *conn;
}

wsconn::wsconn() {
  std::string naddr = ws_naddr();
  if (!split_nwaddr(naddr, host_, port_)) {
    TPERROR(TPEINVAL, "cannot parse WSNADDR %s", naddr.c_str());
    throw std::runtime_error("bad WSNADDR");
  }
}

wsconn::~wsconn() {
  logout();
}

void wsconn::logout() {
  if (ssl_ != nullptr) {
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
  buffered_.clear();
}

long wsconn::next_blocktime() {
  if (blocktime_next_ != 0) {
    auto t = blocktime_next_;
    blocktime_next_ = 0;
    return t;
  }
  if (blocktime_all_ != 0) {
    return blocktime_all_;
  }
  return 6000;  // the platform default BLOCKTIME
}

// Waits for the socket to become ready for whatever SSL asked, up to the
// deadline; false on timeout or error.
bool wsconn::wait_ssl(int rc, long deadline_ms) {
  int err = SSL_get_error(ssl_, rc);
  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd_, &set);
    timeval tv;
    tv.tv_sec = deadline_ms / 1000;
    tv.tv_usec = (deadline_ms % 1000) * 1000;
    int n = 0;
    do {
      n = select(fd_ + 1, err == SSL_ERROR_WANT_READ ? &set : nullptr,
                 err == SSL_ERROR_WANT_WRITE ? &set : nullptr, nullptr, &tv);
    } while (n == -1 && errno == EINTR);
    return n > 0;
  }
  return false;
}

bool wsconn::send_frame(fux::link::frame_type type, std::vector<char> payload) {
  std::vector<char> wire;
  const char magic[4] = {'F', 'U', 'X', '1'};
  wire.insert(wire.end(), magic, magic + 4);
  wire.push_back(static_cast<char>(type));
  fux::wire::writer len(wire);
  len.u32(static_cast<uint32_t>(payload.size()));
  wire.insert(wire.end(), payload.begin(), payload.end());

  size_t off = 0;
  long deadline = 10000;
  while (off < wire.size()) {
    int n = SSL_write(ssl_, wire.data() + off,
                      static_cast<int>(wire.size() - off));
    if (n > 0) {
      off += static_cast<size_t>(n);
      continue;
    }
    if (!wait_ssl(n, deadline)) {
      TPERROR(TPETIME, "timed out sending to WSL");
      return false;
    }
  }
  return true;
}

bool wsconn::recv_frame(fux::link::frame_type *type, std::vector<char> *payload,
                        long deadline_ms) {
  auto deadline = steady::now() + std::chrono::milliseconds(deadline_ms);
  std::vector<char> buf(9);
  size_t off = 0;
  while (off < buf.size()) {
    int n = SSL_read(ssl_, buf.data() + off, static_cast<int>(buf.size() - off));
    if (n > 0) {
      off += static_cast<size_t>(n);
      continue;
    }
    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                              steady::now())
            .count();
    if (remaining <= 0 || !wait_ssl(n, static_cast<long>(remaining))) {
      TPERROR(TPETIME, "timed out waiting for WSL");
      return false;
    }
  }
  if (memcmp(buf.data(), "FUX1", 4) != 0) {
    TPERROR(TPESYSTEM, "bad frame from WSL");
    return false;
  }
  *type = static_cast<fux::link::frame_type>(static_cast<uint8_t>(buf[4]));
  std::vector<char> lenbytes(buf.begin() + 5, buf.end());
  fux::wire::reader r(lenbytes);
  uint32_t len = 0;
  if (!r.u32(len)) {
    return false;
  }
  payload->assign(len, '\0');
  off = 0;
  while (off < payload->size()) {
    int n = SSL_read(ssl_, payload->data() + off,
                     static_cast<int>(payload->size() - off));
    if (n > 0) {
      off += static_cast<size_t>(n);
      continue;
    }
    auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                              steady::now())
            .count();
    if (remaining <= 0 || !wait_ssl(n, static_cast<long>(remaining))) {
      TPERROR(TPETIME, "timed out waiting for WSL");
      return false;
    }
  }
  return true;
}

int wsconn::login(const char *cltname, const char *usrname, const char *grpname,
                  const char *passwd) {
  if (ssl_ != nullptr) {
    fux::atmi::reset_tperrno();
    return 0;
  }

  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints,
                  &res) != 0) {
    TPERROR(TPENOENT, "cannot resolve %s", host_.c_str());
    return -1;
  }
  for (auto *ai = res; ai != nullptr; ai = ai->ai_next) {
    fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd_ == -1) {
      continue;
    }
    if (connect(fd_, ai->ai_addr, ai->ai_addrlen) == 0) {
      break;
    }
    close(fd_);
    fd_ = -1;
  }
  freeaddrinfo(res);
  if (fd_ == -1) {
    TPERROR(TPENOENT, "cannot connect to WSL at %s:%u", host_.c_str(), port_);
    return -1;
  }
  // the socket stays nonblocking: SSL_connect/read/write then return
  // WANT_READ/WANT_WRITE and the deadline loop drives them
  int flags = fcntl(fd_, F_GETFL, 0);
  fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

  // the identity key and the server pin cache live in a private
  // directory, FUXKEYS overrides like SSH's -i
  std::string dir = fux::util::getenv("FUXKEYS", "");
  if (dir.empty()) {
    dir = fux::util::getenv("HOME", ".") + std::string("/.fuxkeys");
  }
  try {
    static std::unique_ptr<fux::link::security> keys;
    if (keys == nullptr) {
      keys = std::make_unique<fux::link::security>(dir, "wsclient");
    }
    sec_ = keys.get();
  } catch (const std::exception &e) {
    TPERROR(TPESYSTEM, "cannot set up link security: %s", e.what());
    return -1;
  }

  ssl_ = SSL_new(sec_->client_ctx());
  if (ssl_ == nullptr || SSL_set_fd(ssl_, fd_) != 1) {
    TPERROR(TPESYSTEM, "cannot set up TLS");
    return -1;
  }
  long deadline = 10000;
  for (;;) {
    int rc = SSL_connect(ssl_);
    if (rc == 1) {
      break;
    }
    if (!wait_ssl(rc, deadline)) {
      TPERROR(TPETIME, "TLS handshake with WSL failed");
      return -1;
    }
  }

  // trust-on-first-use pin of the server identity
  std::string fp = fux::link::security::fingerprint(ssl_);
  std::string pin_name = "wsl-" + host_ + "-" + std::to_string(port_);
  std::string pinned;
  if (sec_->pin_load(pin_name, pinned)) {
    if (pinned != fp) {
      TPERROR(TPESYSTEM, "WSL fingerprint mismatch for %s:%u", host_.c_str(),
              port_);
      return -1;
    }
  } else {
    sec_->pin_store(pin_name, fp);
  }

  std::vector<char> payload;
  fux::wire::writer w(payload);
  w.str(cltname != nullptr ? cltname : "");
  w.str(usrname != nullptr ? usrname : "");
  w.str(grpname != nullptr ? grpname : "");
  w.str(passwd != nullptr ? passwd : "");
  if (!send_frame(fux::link::login, std::move(payload))) {
    return -1;
  }
  fux::link::frame_type type = fux::link::login;
  std::vector<char> reply;
  if (!recv_frame(&type, &reply, deadline) || type != fux::link::login) {
    return -1;
  }
  uint32_t rval = 0;
  fux::wire::reader r(reply);
  if (!r.u32(rval)) {
    TPERROR(TPESYSTEM, "bad login reply from WSL");
    return -1;
  }
  if (rval != 0) {
    TPERROR(static_cast<int>(rval), "WSL refused the login");
    return -1;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int wsconn::tpacall(const char *svc, char *data, long len, long flags) {
  if (ssl_ == nullptr && login(nullptr, nullptr, nullptr, nullptr) == -1) {
    return -1;
  }
  if (fux::tx::transactional()) {
    TPERROR(TPEPROTO, "transactions are not supported over workstation links");
    return -1;
  }
  if (data != nullptr && tptypes(data, nullptr, nullptr) == -1) {
    return -1;
  }
  uint32_t cd = 0;
  if (!(flags & TPNOREPLY)) {
    cd = next_cd_++;
    if (cd == 0) {
      cd = next_cd_++;
    }
  }

  std::vector<char> wire;
  if (data != nullptr) {
    wire.assign(static_cast<size_t>(len) + 1024, '\0');
    long olen = static_cast<long>(wire.size());
    if (tpexport(data, len, wire.data(), &olen, 0) == -1) {
      if (tperrno == TPELIMIT) {
        wire.resize(static_cast<size_t>(olen));
        olen = static_cast<long>(wire.size());
        if (tpexport(data, len, wire.data(), &olen, 0) == -1) {
          return -1;
        }
      } else {
        return -1;
      }
    }
    wire.resize(static_cast<size_t>(olen));
  }

  std::vector<char> payload;
  fux::wire::writer w(payload);
  w.str(svc != nullptr ? svc : "");
  w.u32(cd);
  w.u32(static_cast<uint32_t>(flags & (TPNOTRAN | TPNOREPLY)));
  w.bytes(wire.data(), wire.size());
  if (!send_frame(fux::link::request, std::move(payload))) {
    return -1;
  }
  fux::atmi::reset_tperrno();
  return static_cast<int>(cd);
}

int wsconn::tpgetrply(int *cd, char **data, long *len, long flags) {
  if (data == nullptr) {
    TPERROR(TPEINVAL, "data is null");
    return -1;
  }
  if (tptypes(*data, nullptr, nullptr) == -1) {
    return -1;
  }
  while (true) {
    uint32_t want = static_cast<uint32_t>(*cd);
    if (flags & TPGETANY) {
      if (!buffered_.empty()) {
        want = buffered_.begin()->first;
      }
    }
    auto it = buffered_.find(want);
    if (it != buffered_.end()) {
      std::vector<char> reply = std::move(it->second);
      buffered_.erase(it);
      fux::wire::reader r(reply);
      uint32_t rcvd, rval;
      uint64_t rcode;
      std::vector<char> payload;
      if (!r.u32(rcvd) || !r.u32(rval) || !r.u64(rcode) || !r.bytes(payload)) {
        TPERROR(TPESYSTEM, "bad reply from WSL");
        return -1;
      }
      *cd = static_cast<int>(rcvd);
      tpurcode = static_cast<long>(rcode);
      long ilen = static_cast<long>(payload.size()) - fux::mem::headersize();
      if (len != nullptr) {
        *len = ilen;
      }
      if (rval == TPMINVAL) {
        if (!payload.empty() &&
            tpimport(payload.data(), static_cast<long>(payload.size()), data,
                     &ilen, 0) == -1) {
          return -1;
        }
        fux::atmi::reset_tperrno();
        return 0;
      }
      if (!payload.empty()) {
        long ilen = 0;
        tpimport(payload.data(), static_cast<long>(payload.size()), data,
                 &ilen, 0);
      }
      TPERROR(static_cast<int>(rval), "Service failed with %d", rval);
      return -1;
    }

    if (flags & TPNOBLOCK) {
      TPERROR(TPEBLOCK, "No reply available");
      return -1;
    }
    long timeout = next_blocktime();
    fux::link::frame_type type;
    std::vector<char> reply;
    if (!recv_frame(&type, &reply, timeout) || type != fux::link::reply) {
      return -1;
    }
    fux::wire::reader r(reply);
    uint32_t rcvd = 0;
    if (!r.u32(rcvd)) {
      TPERROR(TPESYSTEM, "bad reply from WSL");
      return -1;
    }
    buffered_[rcvd] = std::move(reply);
  }
}

int wsconn::tpcancel(int cd) {
  buffered_.erase(static_cast<uint32_t>(cd));
  fux::atmi::reset_tperrno();
  return 0;
}

int wsconn::tpsblktime(int blktime, long flags) {
  long units = flags & TPBLK__MASK;
  long what = flags & ~TPBLK__MASK;
  if (blktime < 0 || (units != TPBLK_SECOND && units != TPBLK_MILLISECOND)) {
    TPERROR(TPEINVAL, "Invalid arguments passed to tpsblktime");
    return -1;
  }
  long timeout = units == TPBLK_SECOND ? blktime * 1000 : blktime;
  if (what == TPBLK_NEXT) {
    blocktime_next_ = timeout;
  } else if (what == TPBLK_ALL) {
    blocktime_all_ = timeout;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpsblktime(..., %ld)", flags);
    return -1;
  }
  fux::atmi::reset_tperrno();
  return 0;
}

int wsconn::tpgblktime(long flags) {
  long units = flags & TPBLK__MASK;
  long what = flags & ~TPBLK__MASK;
  long timeout;
  if (what == 0) {
    timeout = 0;
  } else if (what == TPBLK_NEXT) {
    timeout = blocktime_next_;
  } else if (what == TPBLK_ALL) {
    timeout = blocktime_all_;
  } else {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
    return -1;
  }
  if (units == TPBLK_SECOND) {
    timeout /= 1000;
  } else if (units != TPBLK_MILLISECOND) {
    TPERROR(TPEINVAL, "Invalid flags passed to tpgblktime(%ld)", flags);
    return -1;
  }
  fux::atmi::reset_tperrno();
  return static_cast<int>(timeout);
}

}  // namespace fux::ws
