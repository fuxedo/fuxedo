// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include "dmlink.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace fux::link {

namespace {
constexpr unsigned kWordBits = 64;
std::vector<char> encode(const frame &f) {
  std::vector<char> out(9 + f.payload.size());
  memcpy(out.data(), "FUX1", 4);
  out[4] = static_cast<char>(f.type);
  uint32_t len = htonl(static_cast<uint32_t>(f.payload.size()));
  memcpy(out.data() + 5, &len, 4);
  memcpy(out.data() + 9, f.payload.data(), f.payload.size());
  return out;
}
void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
void tune(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
  set_nonblock(fd);
}
int dial(const std::string &host, uint16_t port, bool &inprogress) {
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                  &res) != 0) {
    return -1;
  }
  int fd = -1;
  for (auto *ai = res; ai != nullptr; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd == -1) continue;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      inprogress = false;
      break;
    }
    if (errno == EINPROGRESS) {
      inprogress = true;
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}
}  // namespace

namespace {

std::string sanitize(const std::string &name) {
  std::string out = name;
  for (auto &c : out) {
    if (!std::isalnum(static_cast<unsigned char>(c))) {
      c = '_';
    }
  }
  return out;
}

void mkdir_p(const std::string &dir) {
  std::string cur;
  for (size_t i = 0; i < dir.size(); i++) {
    cur += dir[i];
    if (dir[i] == '/' && cur.size() > 1) {
      mkdir(cur.c_str(), 0700);
    }
  }
  if (mkdir(dir.c_str(), 0700) == -1 && errno != EEXIST) {
    throw std::runtime_error("cannot create " + dir);
  }
}

// A verify callback that accepts any chain: with self-signed identities
// there is no CA to check against, the real decision happens after the
// handshake when the application compares peer fingerprints.
int accept_any(int, X509_STORE_CTX *) { return 1; }

EVP_PKEY *generate_key() {
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
  if (pctx == nullptr) {
    throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
  }
  EVP_PKEY *key = nullptr;
  if (EVP_PKEY_keygen_init(pctx) <= 0 ||
      EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <=
          0 ||
      EVP_PKEY_keygen(pctx, &key) <= 0) {
    EVP_PKEY_CTX_free(pctx);
    throw std::runtime_error("EC P-256 key generation failed");
  }
  EVP_PKEY_CTX_free(pctx);
  return key;
}

X509 *self_sign(EVP_PKEY *key, const std::string &cn) {
  X509 *cert = X509_new();
  if (cert == nullptr) {
    throw std::runtime_error("X509_new failed");
  }
  X509_set_version(cert, 2);
  unsigned char serial[8];
  RAND_bytes(serial, sizeof(serial));
  ASN1_INTEGER_set_uint64(X509_get_serialNumber(cert),
                          *reinterpret_cast<uint64_t *>(serial));
  X509_NAME *name = X509_get_subject_name(cert);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char *>(
                                 cn.empty() ? "fuxedo" : cn.c_str()),
                             -1, -1, 0);
  X509_set_issuer_name(cert, name);
  X509_gmtime_adj(X509_getm_notBefore(cert), -24 * 3600);
  X509_gmtime_adj(X509_getm_notAfter(cert), 10L * 365 * 24 * 3600);
  X509_set_pubkey(cert, key);
  if (X509_sign(cert, key, EVP_sha256()) <= 0) {
    X509_free(cert);
    throw std::runtime_error("X509_sign failed");
  }
  return cert;
}

}  // namespace

security::security(const std::string &dir, const std::string &cn)
    : dir_(dir) {
  mkdir_p(dir_);
  std::string pins = dir_ + "/pins";
  mkdir_p(pins);

  std::string pem_path = dir_ + "/identity-" + sanitize(cn) + ".pem";
  EVP_PKEY *key = nullptr;
  X509 *cert = nullptr;
  std::ifstream existing(pem_path);
  if (existing) {
    existing.close();
    FILE *f = fopen(pem_path.c_str(), "r");
    if (f == nullptr || (key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr)) == nullptr ||
        (cert = PEM_read_X509(f, nullptr, nullptr, nullptr)) == nullptr) {
      if (f != nullptr) fclose(f);
      throw std::runtime_error("cannot read identity " + pem_path);
    }
    fclose(f);
  } else {
    key = generate_key();
    cert = self_sign(key, cn);
    std::string tmp = pem_path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (f == nullptr || PEM_write_PrivateKey(f, key, nullptr, nullptr, 0,
                                             nullptr, nullptr) <= 0 ||
        PEM_write_X509(f, cert) <= 0) {
      if (f != nullptr) fclose(f);
      EVP_PKEY_free(key);
      X509_free(cert);
      throw std::runtime_error("cannot write identity " + pem_path);
    }
    fclose(f);
    chmod(tmp.c_str(), 0600);
    if (rename(tmp.c_str(), pem_path.c_str()) == -1) {
      EVP_PKEY_free(key);
      X509_free(cert);
      throw std::runtime_error("cannot install identity " + pem_path);
    }
  }

  server_ = SSL_CTX_new(TLS_server_method());
  client_ = SSL_CTX_new(TLS_client_method());
  if (server_ == nullptr || client_ == nullptr ||
      SSL_CTX_use_certificate(server_, cert) <= 0 ||
      SSL_CTX_use_PrivateKey(server_, key) <= 0 ||
      SSL_CTX_check_private_key(server_) <= 0 ||
      SSL_CTX_use_certificate(client_, cert) <= 0 ||
      SSL_CTX_use_PrivateKey(client_, key) <= 0 ||
      SSL_CTX_check_private_key(client_) <= 0) {
    EVP_PKEY_free(key);
    X509_free(cert);
    throw std::runtime_error("cannot set up TLS contexts");
  }
  // request peer certificates; the chain itself is not validated here,
  // fingerprints are compared by the application instead
  SSL_CTX_set_verify(server_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                     accept_any);
  SSL_CTX_set_verify(client_, SSL_VERIFY_PEER, accept_any);
  EVP_PKEY_free(key);
  X509_free(cert);
}

security::~security() {
  SSL_CTX_free(server_);
  SSL_CTX_free(client_);
}

std::string security::fingerprint(SSL *ssl) {
  std::string out;
  X509 *peer = SSL_get1_peer_certificate(ssl);
  if (peer == nullptr) {
    return out;
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  if (X509_digest(peer, EVP_sha256(), md, &len) == 1) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < len; i++) {
      out += hex[md[i] >> 4];
      out += hex[md[i] & 0xf];
    }
  }
  X509_free(peer);
  return out;
}

bool security::pin_load(const std::string &name, std::string &fp) const {
  std::ifstream in(dir_ + "/pins/" + sanitize(name));
  if (!in) {
    return false;
  }
  in >> fp;
  return !fp.empty();
}

void security::pin_store(const std::string &name, const std::string &fp) const {
  std::string path = dir_ + "/pins/" + sanitize(name);
  std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << fp << "\n";
  }
  chmod(tmp.c_str(), 0600);
  rename(tmp.c_str(), path.c_str());
}

struct engine::conn {
  std::vector<char> in;      // plaintext accumulator until a full frame
  std::vector<char> out;     // pending plaintext bytes
  size_t out_off = 0;
  std::vector<char> raw_in;  // ciphertext read from the socket
  std::vector<char> raw_out; // ciphertext waiting for the socket
  size_t raw_off = 0;
  SSL *ssl = nullptr;
  BIO *rbio = nullptr;
  BIO *wbio = nullptr;
  bool connecting = false;   // TCP connect in progress
  bool handshaking = false;  // TLS handshake in progress
  bool dialed = false;       // outbound connection
};

engine::engine() {
  struct rlimit rl;
  getrlimit(RLIMIT_NOFILE, &rl);
  capacity_ = static_cast<int>(rl.rlim_cur);
  nbits_ = (capacity_ + kWordBits - 1) / kWordBits * kWordBits;
  conns_ = new conn[capacity_];
  // allocated as word arrays so the word-at-a-time walks below are
  // properly aligned; indexed as bytes everywhere else
  master_ = reinterpret_cast<unsigned char *>(
      new unsigned long[nbits_ / kWordBits]());
  ready_ = reinterpret_cast<unsigned char *>(
      new unsigned long[nbits_ / kWordBits]());
  wantwr_ = reinterpret_cast<unsigned char *>(
      new unsigned long[nbits_ / kWordBits]());
  wrout_ = reinterpret_cast<unsigned char *>(
      new unsigned long[nbits_ / kWordBits]());
}

engine::~engine() {
  for (int i = 0; i < capacity_; i++) {
    if (master_[i / 8] & (1u << (i % 8))) close(i);
  }
  delete[] conns_;
  delete[] reinterpret_cast<unsigned long *>(master_);
  delete[] reinterpret_cast<unsigned long *>(ready_);
  delete[] reinterpret_cast<unsigned long *>(wantwr_);
  delete[] reinterpret_cast<unsigned long *>(wrout_);
}

void engine::arm(int fd) { master_[fd / 8] |= (1u << (fd % 8)); }
void engine::drop(int fd) {
  master_[fd / 8] &= ~(1u << (fd % 8));
  wantwr_[fd / 8] &= ~(1u << (fd % 8));
  if (conns_[fd].ssl != nullptr) {
    SSL_free(conns_[fd].ssl);
  }
  close(fd);
  conns_[fd] = conn();
  if (on_close) on_close(fd);
  while (maxfd_ >= 0 && !(master_[maxfd_ / 8] & (1u << (maxfd_ % 8)))) {
    maxfd_--;
  }
}

int engine::listen(const std::string &host, uint16_t port) {
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if (getaddrinfo(host.empty() ? nullptr : host.c_str(),
                  std::to_string(port).c_str(), &hints, &res) != 0) {
    return -1;
  }
  int fd = -1;
  for (auto *ai = res; ai != nullptr; ai = ai->ai_next) {
    int one = 1;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 &&
        ::listen(fd, 128) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd == -1) return -1;
  tune(fd);
  arm(fd);
  listeners_.push_back(fd);
  if (fd > maxfd_) maxfd_ = fd;
  return fd;
}

int engine::connect(const std::string &host, uint16_t port) {
  bool inprogress = false;
  int fd = dial(host, port, inprogress);
  if (fd == -1) return -1;
  tune(fd);
  conns_[fd].connecting = inprogress;
  conns_[fd].dialed = true;
  arm(fd);
  if (fd > maxfd_) maxfd_ = fd;
  if (sec_ != nullptr) {
    start_tls(fd, false);
    if (!inprogress) {
      pump_handshake(fd);  // completed synchronously
    }
  } else if (!inprogress && on_accept) {
    on_accept(fd);  // completed synchronously
  }
  return fd;
}

bool engine::queue(int fd, frame f) {
  if (fd < 0 || fd >= capacity_ ||
      !(master_[fd / 8] & (1u << (fd % 8)))) {
    return false;
  }
  auto bytes = encode(f);
  auto &c = conns_[fd];
  c.out.insert(c.out.end(), bytes.begin(), bytes.end());
  update_wantwr(fd);
  return true;
}

void engine::close(int conn) { ::close(conn); }

void engine::cut(int conn) {
  if (conn >= 0 && conn < capacity_ &&
      (master_[conn / 8] & (1u << (conn % 8)))) {
    drop(conn);
  }
}

void engine::do_accept(int listener) {
  for (int n = 0; n < 64; n++) {  // amortize the select call
    int fd = accept(listener, nullptr, nullptr);
    if (fd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (fd == -1) return;
    tune(fd);
    arm(fd);
    conns_[fd] = conn();
    if (fd > maxfd_) maxfd_ = fd;
    if (sec_ != nullptr) {
      start_tls(fd, true);  // the handshake starts when data arrives
      update_wantwr(fd);
    } else if (on_accept) {
      on_accept(fd);
    }
  }
}

void engine::start_tls(int fd, bool server_side) {
  auto &c = conns_[fd];
  c.ssl = SSL_new(server_side ? sec_->server_ctx() : sec_->client_ctx());
  if (c.ssl == nullptr) {
    drop(fd);
    return;
  }
  c.rbio = BIO_new(BIO_s_mem());
  c.wbio = BIO_new(BIO_s_mem());
  if (c.rbio == nullptr || c.wbio == nullptr) {
    drop(fd);
    return;
  }
  SSL_set_bio(c.ssl, c.rbio, c.wbio);  // the SSL object owns the bios
  if (server_side) {
    SSL_set_accept_state(c.ssl);
  } else {
    SSL_set_connect_state(c.ssl);
  }
  c.handshaking = true;
}

void engine::drain_wbio(int fd) {
  auto &c = conns_[fd];
  char buf[16384];
  for (;;) {
    int n = BIO_read(c.wbio, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    c.raw_out.insert(c.raw_out.end(), buf, buf + n);
  }
}

bool engine::flush_raw(int fd) {
  auto &c = conns_[fd];
  while (c.raw_off < c.raw_out.size()) {
    ssize_t n = send(fd, c.raw_out.data() + c.raw_off,
                     c.raw_out.size() - c.raw_off, MSG_NOSIGNAL);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (n <= 0) {
      drop(fd);
      return false;
    }
    c.raw_off += static_cast<size_t>(n);
  }
  if (c.raw_off == c.raw_out.size()) {
    c.raw_out.clear();
    c.raw_off = 0;
  } else if (c.raw_off > 65536) {
    c.raw_out.erase(c.raw_out.begin(), c.raw_out.begin() + c.raw_off);
    c.raw_off = 0;
  }
  return true;
}

bool engine::pump_handshake(int fd) {
  auto &c = conns_[fd];
  for (;;) {
    int rc = SSL_do_handshake(c.ssl);
    drain_wbio(fd);
    if (rc == 1) {
      c.handshaking = false;
      if (on_verify) {
        std::string fp = security::fingerprint(c.ssl);
        if (!on_verify(fd, fp, c.dialed)) {
          drop(fd);
          return false;
        }
        if (!(master_[fd / 8] & (1u << (fd % 8)))) {
          return false;
        }
      }
      if (on_accept) {
        on_accept(fd);
      }
      update_wantwr(fd);
      return master_[fd / 8] & (1u << (fd % 8));
    }
    int err = SSL_get_error(c.ssl, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      update_wantwr(fd);
      return true;
    }
    drop(fd);
    return false;
  }
}

void engine::pump_in(int fd) {
  auto &c = conns_[fd];
  if (!c.raw_in.empty()) {
    int w = BIO_write(c.rbio, c.raw_in.data(), static_cast<int>(c.raw_in.size()));
    if (w <= 0) {
      if (!BIO_should_retry(c.rbio)) {
        drop(fd);
        return;
      }
    } else {
      c.raw_in.erase(c.raw_in.begin(), c.raw_in.begin() + w);
    }
  }
  if (c.handshaking) {
    if (!pump_handshake(fd) || c.handshaking) {
      return;
    }
  }
  char buf[16384];
  for (;;) {
    int n = SSL_read(c.ssl, buf, sizeof(buf));
    if (n > 0) {
      c.in.insert(c.in.end(), buf, buf + n);
      continue;
    }
    int err = SSL_get_error(c.ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      break;
    }
    drop(fd);  // orderly close or fatal error
    return;
  }
  drain_wbio(fd);
  parse_frames(fd);
}

void engine::pump_out(int fd) {
  auto &c = conns_[fd];
  if (c.handshaking) {
    pump_handshake(fd);
    return;
  }
  while (c.out_off < c.out.size()) {
    int n = SSL_write(c.ssl, c.out.data() + c.out_off,
                      static_cast<int>(c.out.size() - c.out_off));
    if (n > 0) {
      c.out_off += static_cast<size_t>(n);
      continue;
    }
    int err = SSL_get_error(c.ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
      break;
    }
    drop(fd);
    return;
  }
  if (c.out_off == c.out.size()) {
    c.out.clear();
    c.out_off = 0;
  }
  drain_wbio(fd);
}

void engine::update_wantwr(int fd) {
  auto &c = conns_[fd];
  bool pending = c.raw_off < c.raw_out.size() || c.out_off < c.out.size() ||
                 c.handshaking;
  if (pending) {
    wantwr_[fd / 8] |= (1u << (fd % 8));
  } else {
    wantwr_[fd / 8] &= ~(1u << (fd % 8));
  }
}

void engine::parse_frames(int fd) {
  auto &c = conns_[fd];
  for (;;) {
    if (c.in.size() < 9) break;
    if (memcmp(c.in.data(), "FUX1", 4) != 0) {
      drop(fd);
      return;
    }
    uint32_t len;
    memcpy(&len, c.in.data() + 5, 4);
    len = ntohl(len);
    if (c.in.size() < 9 + len) break;
    frame f{static_cast<frame_type>(static_cast<uint8_t>(c.in[4])),
            std::vector<char>(c.in.begin() + 9, c.in.begin() + 9 + len)};
    c.in.erase(c.in.begin(), c.in.begin() + 9 + len);
    if (f.type == heartbeat) continue;
    if (f.type == bye) {
      drop(fd);
      return;
    }
    if (on_frame) on_frame(fd, std::move(f));
    if (!(master_[fd / 8] & (1u << (fd % 8)))) return;  // dropped
  }
}

void engine::do_read(int fd) {
  char buf[8192];
  auto &c = conns_[fd];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    if (n <= 0) {
      drop(fd);
      return;
    }
    if (c.ssl != nullptr) {
      c.raw_in.insert(c.raw_in.end(), buf, buf + n);
    } else {
      c.in.insert(c.in.end(), buf, buf + n);
    }
  }
  if (c.ssl != nullptr) {
    pump_in(fd);
  } else {
    parse_frames(fd);
  }
}

void engine::do_write(int fd) {
  auto &c = conns_[fd];
  if (c.ssl != nullptr) {
    if (!flush_raw(fd)) {
      return;
    }
    pump_out(fd);
    if (!(master_[fd / 8] & (1u << (fd % 8)))) {
      return;
    }
    flush_raw(fd);
    update_wantwr(fd);
    return;
  }
  while (c.out_off < c.out.size()) {
    ssize_t n = send(fd, c.out.data() + c.out_off, c.out.size() - c.out_off,
                     MSG_NOSIGNAL);
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (n <= 0) {
      drop(fd);
      return;
    }
    c.out_off += static_cast<size_t>(n);
  }
  c.out.clear();
  c.out_off = 0;
  wantwr_[fd / 8] &= ~(1u << (fd % 8));
}

void engine::poll(struct timeval *timeout) {
  assert(maxfd_ < capacity_);
  // glibc's fortified select() caps user sets at sizeof(fd_set); the
  // gateway's fd counts stay far below FD_SETSIZE, so poll through real
  // fd_sets copied from the engine bitsets (the word-walk below still
  // uses the rlimit-sized arrays).
  fd_set rset, wset;
  FD_ZERO(&rset);
  FD_ZERO(&wset);
  size_t bytes = std::min(nbits_ / 8, sizeof(fd_set));
  memcpy(&rset, master_, bytes);
  memcpy(&wset, wantwr_, bytes);
  int nready = select(maxfd_ + 1, &rset, &wset, nullptr, timeout);
  if (nready <= 0) return;
  memcpy(ready_, &rset, bytes);
  memcpy(wrout_, &wset, bytes);
  int done = 0;
  auto *rwords = reinterpret_cast<unsigned long *>(ready_);
  for (unsigned i = 0;
       done < nready && i * kWordBits <= static_cast<unsigned>(maxfd_);
       i++) {
    unsigned long bits = rwords[i];
    if (bits == 0) continue;
    for (unsigned j = 0; done < nready && j < kWordBits; j++) {
      if (!(bits & (1ul << j))) continue;
      int fd = static_cast<int>(i * kWordBits + j);
      if (fd > maxfd_) break;
      done++;
      if (std::find(listeners_.begin(), listeners_.end(), fd) !=
          listeners_.end()) {
        do_accept(fd);
      } else {
        auto &c = conns_[fd];
        if (c.connecting) {
          int err = 0;
          socklen_t l = sizeof(err);
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
          c.connecting = false;
          if (err != 0) {
            drop(fd);
            continue;
          }
          if (c.ssl != nullptr) {
            if (!pump_handshake(fd)) {
              continue;
            }
          } else if (on_accept) {
            on_accept(fd);
          }
        }
        if (master_[fd / 8] & (1u << (fd % 8))) do_read(fd);
        if (master_[fd / 8] & (1u << (fd % 8)) &&
            (wrout_[fd / 8] & (1u << (fd % 8)))) {
          do_write(fd);
        }
      }
    }
  }
  // fds writable but not readable never appear in the read walk
  auto *wwords = reinterpret_cast<unsigned long *>(wrout_);
  for (unsigned i = 0;
       done < nready && i * kWordBits <= static_cast<unsigned>(maxfd_);
       i++) {
    unsigned long bits = wwords[i];
    if (bits == 0) continue;
    for (unsigned j = 0; done < nready && j < kWordBits; j++) {
      if (!(bits & (1ul << j))) continue;
      int fd = static_cast<int>(i * kWordBits + j);
      if (fd > maxfd_) break;
      if (ready_[fd / 8] & (1u << (fd % 8))) continue;  // already handled
      done++;
      if (master_[fd / 8] & (1u << (fd % 8))) do_write(fd);
    }
  }
}

}  // namespace fux::link
