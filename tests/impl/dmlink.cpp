// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <catch.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

#include "../../src/dmlink.h"

TEST_CASE("link engine echoes frames over loopback", "[dmlink]") {
  fux::link::engine server;
  fux::link::engine client;

  int accepted = -1;
  bool got = false;
  server.on_accept = [&](int fd) { accepted = fd; };
  server.on_frame = [&](int fd, fux::link::frame f) {
    server.queue(fd, {fux::link::heartbeat, {}});
    server.queue(fd, {fux::link::reply, std::move(f.payload)});
  };
  client.on_frame = [&](int, fux::link::frame f) {
    REQUIRE(f.type == fux::link::reply);
    REQUIRE(std::string(f.payload.begin(), f.payload.end()) == "ping");
    got = true;
  };

  int l = server.listen("127.0.0.1", 0);
  REQUIRE(l != -1);
  socklen_t al = sizeof(sockaddr_in);
  sockaddr_in sa;
  getsockname(l, reinterpret_cast<sockaddr *>(&sa), &al);
  int c = client.connect("127.0.0.1", ntohs(sa.sin_port));
  REQUIRE(c != -1);

  // both engines share this thread, so polls must not block: a
  // connect-timeout-sized slice is plenty for loopback
  timeval slice{0, 1000};
  for (int i = 0; i < 200 && (accepted == -1 || !got); i++) {
    server.poll(&slice);
    client.poll(&slice);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(accepted != -1);
  client.queue(c, {fux::link::request, {'p', 'i', 'n', 'g'}});
  for (int i = 0; i < 200 && !got; i++) {
    server.poll(&slice);
    client.poll(&slice);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(got);
}

TEST_CASE("link engine TLS echo and trust-on-first-use pins", "[dmlink]") {
  mkdir("tmp", 0777);
  std::string dir = std::string("tmp/dmtls-") + std::to_string(getpid());
  fux::link::security server_sec(dir + "/server", "server-cn");
  fux::link::security client_sec(dir + "/client", "client-cn");
  fux::link::security attacker_sec(dir + "/attacker", "attacker-cn");

  fux::link::engine server;
  fux::link::engine client;
  server.secure(&server_sec);
  client.secure(&client_sec);

  int accepted = -1;
  bool got = false;
  std::string client_fp_seen_by_server;
  std::string server_fp_seen_by_client;
  server.on_verify = [&](int, const std::string &fp, bool dialed) {
    if (!dialed) {
      client_fp_seen_by_server = fp;
    }
    return true;
  };
  client.on_verify = [&](int, const std::string &fp, bool dialed) {
    if (dialed) {
      server_fp_seen_by_client = fp;
    }
    return true;
  };
  server.on_accept = [&](int fd) { accepted = fd; };
  server.on_frame = [&](int fd, fux::link::frame f) {
    server.queue(fd, {fux::link::reply, std::move(f.payload)});
  };
  client.on_frame = [&](int, fux::link::frame f) {
    REQUIRE(f.type == fux::link::reply);
    REQUIRE(std::string(f.payload.begin(), f.payload.end()) == "ping");
    got = true;
  };

  int l = server.listen("127.0.0.1", 0);
  REQUIRE(l != -1);
  socklen_t al = sizeof(sockaddr_in);
  sockaddr_in sa;
  getsockname(l, reinterpret_cast<sockaddr *>(&sa), &al);
  int c = client.connect("127.0.0.1", ntohs(sa.sin_port));
  REQUIRE(c != -1);

  timeval slice{0, 1000};
  for (int i = 0; i < 400 && accepted == -1; i++) {
    server.poll(&slice);
    client.poll(&slice);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(accepted != -1);
  client.queue(c, {fux::link::request, {'p', 'i', 'n', 'g'}});
  for (int i = 0; i < 200 && !got; i++) {
    server.poll(&slice);
    client.poll(&slice);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(got);
  REQUIRE(!client_fp_seen_by_server.empty());
  REQUIRE(!server_fp_seen_by_client.empty());

  // trust-on-first-use: once the server pinned its client's
  // fingerprint, a different identity is refused
  int server_accepts = 0;
  server.on_accept = [&](int) { server_accepts++; };
  server.on_verify = [&](int, const std::string &fp, bool dialed) {
    return dialed || fp == client_fp_seen_by_server;
  };
  fux::link::engine attacker;
  attacker.secure(&attacker_sec);
  REQUIRE(attacker.connect("127.0.0.1", ntohs(sa.sin_port)) != -1);
  // the attacker finishes its own side of the handshake before the
  // server sees the final flight; what matters is the server refuses
  // to bring the link up and drops it
  for (int i = 0; i < 200; i++) {
    server.poll(&slice);
    attacker.poll(&slice);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(server_accepts == 0);
}
