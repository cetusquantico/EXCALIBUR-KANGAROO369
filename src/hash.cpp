// Clean-room SHA-256, RIPEMD-160, Base58Check.
#include "hash.h"
#include <cstring>

namespace hsh {

// ---------------- SHA-256 ----------------
static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::vector<uint8_t> sha256(const uint8_t* data, size_t len) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};

    std::vector<uint8_t> msg(data, data + len);
    uint64_t bitlen = (uint64_t)len * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back((uint8_t)(bitlen >> (i * 8)));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (msg[off+4*i]<<24)|(msg[off+4*i+1]<<16)|(msg[off+4*i+2]<<8)|(msg[off+4*i+3]);
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15]>>3);
            uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
            uint32_t ch = (e&f)^(~e&g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
            uint32_t maj = (a&b)^(a&c)^(b&c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::vector<uint8_t> out(32);
    for (int i = 0; i < 8; ++i) { out[4*i]=h[i]>>24; out[4*i+1]=h[i]>>16; out[4*i+2]=h[i]>>8; out[4*i+3]=h[i]; }
    return out;
}

// ---------------- RIPEMD-160 ----------------
static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

std::vector<uint8_t> ripemd160(const uint8_t* data, size_t len) {
    static const int rL[80] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
        3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12, 1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
        4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13 };
    static const int rR[80] = {
        5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12, 6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
        15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13, 8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
        12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11 };
    static const int sL[80] = {
        11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8, 7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
        11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5, 11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
        9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6 };
    static const int sR[80] = {
        8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6, 9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
        9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5, 15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
        8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11 };
    static const uint32_t KL[5] = {0x00000000,0x5a827999,0x6ed9eba1,0x8f1bbcdc,0xa953fd4e};
    static const uint32_t KR[5] = {0x50a28be6,0x5c4dd124,0x6d703ef3,0x7a6d76e9,0x00000000};

    uint32_t h[5] = {0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    std::vector<uint8_t> msg(data, data + len);
    uint64_t bitlen = (uint64_t)len * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 0; i < 8; ++i) msg.push_back((uint8_t)(bitlen >> (i * 8)));

    auto f = [](int j, uint32_t x, uint32_t y, uint32_t z) -> uint32_t {
        if (j < 16) return x ^ y ^ z;
        if (j < 32) return (x & y) | (~x & z);
        if (j < 48) return (x | ~y) ^ z;
        if (j < 64) return (x & z) | (y & ~z);
        return x ^ (y | ~z);
    };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t X[16];
        for (int i = 0; i < 16; ++i)
            X[i] = msg[off+4*i] | (msg[off+4*i+1]<<8) | (msg[off+4*i+2]<<16) | ((uint32_t)msg[off+4*i+3]<<24);
        uint32_t al=h[0],bl=h[1],cl=h[2],dl=h[3],el=h[4];
        uint32_t ar=h[0],br=h[1],cr=h[2],dr=h[3],er=h[4];
        for (int j = 0; j < 80; ++j) {
            uint32_t t = rol(al + f(j,bl,cl,dl) + X[rL[j]] + KL[j/16], sL[j]) + el;
            al=el; el=dl; dl=rol(cl,10); cl=bl; bl=t;
            t = rol(ar + f(79-j,br,cr,dr) + X[rR[j]] + KR[j/16], sR[j]) + er;
            ar=er; er=dr; dr=rol(cr,10); cr=br; br=t;
        }
        uint32_t t = h[1] + cl + dr;
        h[1]=h[2]+dl+er; h[2]=h[3]+el+ar; h[3]=h[4]+al+br; h[4]=h[0]+bl+cr; h[0]=t;
    }
    std::vector<uint8_t> out(20);
    for (int i = 0; i < 5; ++i) { out[4*i]=h[i]; out[4*i+1]=h[i]>>8; out[4*i+2]=h[i]>>16; out[4*i+3]=h[i]>>24; }
    return out;
}

std::vector<uint8_t> hash160(const std::vector<uint8_t>& data) {
    auto s = sha256(data.data(), data.size());
    return ripemd160(s.data(), s.size());
}

// ---------------- Base58Check ----------------
std::string base58check(const std::vector<uint8_t>& payload) {
    static const char* A = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::vector<uint8_t> d = payload;
    auto c1 = sha256(d.data(), d.size());
    auto c2 = sha256(c1.data(), c1.size());
    d.insert(d.end(), c2.begin(), c2.begin() + 4);
    int zeros = 0; while (zeros < (int)d.size() && d[zeros] == 0) ++zeros;
    std::vector<uint8_t> b;             // big-number base58 via repeated division
    for (uint8_t byte : d) {
        int carry = byte;
        for (size_t i = 0; i < b.size(); ++i) { int x = b[i] * 256 + carry; b[i] = x % 58; carry = x / 58; }
        while (carry) { b.push_back(carry % 58); carry /= 58; }
    }
    std::string out(zeros, '1');
    for (auto it = b.rbegin(); it != b.rend(); ++it) out += A[*it];
    return out;
}

std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    auto nib = [](char c)->int { return (c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:0; };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back((uint8_t)((nib(hex[i])<<4)|nib(hex[i+1])));
    return out;
}

std::string p2pkh_address(const std::string& pubkey_hex) {
    auto pub = from_hex(pubkey_hex);
    auto h = hash160(pub);
    std::vector<uint8_t> payload; payload.push_back(0x00);
    payload.insert(payload.end(), h.begin(), h.end());
    return base58check(payload);
}

std::string wif_compressed(const std::vector<uint8_t>& key32) {
    std::vector<uint8_t> payload; payload.push_back(0x80);
    payload.insert(payload.end(), key32.begin(), key32.end());
    payload.push_back(0x01);
    return base58check(payload);
}

} // namespace hsh
