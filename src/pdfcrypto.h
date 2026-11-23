#pragma once
// What reading an encrypted PDF needs, and nothing more: MD5, SHA-256/384/512, RC4 and AES (128 and 256 bit, CBC).
// Many exam boards publish their specifications "encrypted" with an empty password: anyone can open them, but the
// text inside is stored scrambled, so a reader has to unscramble it.
#include <cstdint>
#include <string>

namespace pdfcrypto {
std::string md5(const std::string &data);
std::string sha256(const std::string &data);
std::string sha384(const std::string &data);
std::string sha512(const std::string &data);
std::string rc4(const std::string &key, const std::string &data);
// CBC with the first 16 bytes of data as the IV (as PDF stores it); removes PKCS#7 padding when `unpad`.
std::string aesDecryptCbc(const std::string &key, const std::string &data, bool unpad = true);
// CBC, no padding (used by PDF 2.0's password hash and to unwrap the file key)
std::string aesEncryptCbcNoPad(const std::string &key, const std::string &iv, const std::string &data);
std::string aesDecryptCbcNoPad(const std::string &key, const std::string &iv, const std::string &data);
}
