// EXCALIBUR-KANGAROO369 — hashing & Base58Check (for address/WIF output).
// Clean-room standard algorithms (SHA-256, RIPEMD-160, Base58Check).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hsh {

std::vector<uint8_t> sha256(const uint8_t* data, size_t len);
std::vector<uint8_t> ripemd160(const uint8_t* data, size_t len);
std::vector<uint8_t> hash160(const std::vector<uint8_t>& data);   // ripemd160(sha256(x))
std::string base58check(const std::vector<uint8_t>& payload);     // payload incl. version byte

// hex (compressed 02/03 or uncompressed 04) pubkey -> P2PKH mainnet address
std::string p2pkh_address(const std::string& pubkey_hex);
// 32-byte big-endian private key -> compressed WIF
std::string wif_compressed(const std::vector<uint8_t>& key32);

std::vector<uint8_t> from_hex(const std::string& hex);

} // namespace hsh
