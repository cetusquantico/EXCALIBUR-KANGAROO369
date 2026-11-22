// EXCALIBUR-KANGAROO369 — Phase 5 checkpoint persistence (see checkpoint.h).
#include "checkpoint.h"
#include "hash.h"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace kang {

static const char  MAGIC[4] = {'E','X','K','5'};
static const uint32_t VERSION = 1;

// ---- little-endian byte helpers ----
static void put_u64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}
static void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}
static void put_u256(std::vector<uint8_t>& b, const ec::U256& u) {
    for (int i = 0; i < 4; ++i) put_u64(b, u.v[i]);
}
struct Rd { const uint8_t* p; size_t n, i = 0; bool ok = true; };
static uint64_t get_u64(Rd& r) {
    if (r.i + 8 > r.n) { r.ok = false; return 0; }
    uint64_t v = 0; for (int k = 0; k < 8; ++k) v |= (uint64_t)r.p[r.i++] << (8 * k); return v;
}
static uint32_t get_u32(Rd& r) {
    if (r.i + 4 > r.n) { r.ok = false; return 0; }
    uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= (uint32_t)r.p[r.i++] << (8 * k); return v;
}
static ec::U256 get_u256(Rd& r) { ec::U256 u{{0,0,0,0}}; for (int i = 0; i < 4; ++i) u.v[i] = get_u64(r); return u; }
static uint8_t  get_u8(Rd& r) { if (r.i + 1 > r.n) { r.ok = false; return 0; } return r.p[r.i++]; }

static std::string dir_of(const std::string& path) {
    auto s = path.find_last_of('/');
    return (s == std::string::npos) ? std::string(".") : path.substr(0, s ? s : 1);
}

bool ckpt_save(const std::string& path, const Snapshot& s) {
    std::vector<uint8_t> b;
    b.insert(b.end(), MAGIC, MAGIC + 4);
    put_u32(b, VERSION);
    put_u32(b, (uint32_t)s.range);
    b.push_back(s.sym);
    put_u64(b, s.seed);
    put_u64(b, s.jumps);
    put_u256(b, s.target.x);
    put_u256(b, s.target.y);
    b.push_back(s.target.inf ? 1 : 0);
    put_u64(b, (uint64_t)s.dps.size());
    for (const auto& d : s.dps) { put_u256(b, d.x); b.push_back(d.type); put_u256(b, d.val); }

    std::vector<uint8_t> dg = hsh::sha256(b.data(), b.size());
    b.insert(b.end(), dg.begin(), dg.end());

    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < b.size()) {
        ssize_t w = write(fd, b.data() + off, b.size() - off);
        if (w <= 0) { close(fd); unlink(tmp.c_str()); return false; }
        off += (size_t)w;
    }
    if (fsync(fd) != 0) { close(fd); unlink(tmp.c_str()); return false; }
    close(fd);
    if (rename(tmp.c_str(), path.c_str()) != 0) { unlink(tmp.c_str()); return false; }
    std::string d = dir_of(path);
    int dfd = open(d.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) { fsync(dfd); close(dfd); }
    return true;
}

int ckpt_load(const std::string& path, Snapshot& s) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return -1;
    std::vector<uint8_t> b;
    uint8_t buf[65536];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0) { close(fd); return -1; }
        if (r == 0) break;
        b.insert(b.end(), buf, buf + r);
    }
    close(fd);
    if (b.size() < 4 + 32) return -2;

    // split body / trailing digest, verify integrity
    size_t body = b.size() - 32;
    std::vector<uint8_t> dg = hsh::sha256(b.data(), body);
    if (memcmp(dg.data(), b.data() + body, 32) != 0) return -3;

    Rd r{ b.data(), body };
    if (memcmp(r.p, MAGIC, 4) != 0) return -2;
    r.i = 4;
    if (get_u32(r) != VERSION) return -2;
    s.range = (int)get_u32(r);
    s.sym   = get_u8(r);
    s.seed  = get_u64(r);
    s.jumps = get_u64(r);
    s.target.x = get_u256(r);
    s.target.y = get_u256(r);
    s.target.inf = get_u8(r) != 0;
    uint64_t n = get_u64(r);
    if (!r.ok) return -2;
    s.dps.clear();
    s.dps.reserve(n);
    for (uint64_t k = 0; k < n; ++k) {
        DP d; d.x = get_u256(r); d.type = get_u8(r); d.val = get_u256(r);
        if (!r.ok) return -2;
        s.dps.push_back(d);
    }
    return r.ok ? 0 : -2;
}

} // namespace kang
