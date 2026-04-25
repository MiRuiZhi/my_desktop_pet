#include "crypto.h"
#include "crypto_key.h"
#include <windows.h>
#include <vector>
#include <cstring>

namespace crypto {

static void xorCrypt(uint8_t* data, size_t len, const uint8_t* key, size_t keyLen) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= key[i % keyLen];
    }
}

bool RSACrypt::Init() {
    return true;
}

bool RSACrypt::Encrypt(const std::vector<BYTE>& plaintext, std::vector<BYTE>& ciphertext) {
    uint32_t origSize = (uint32_t)plaintext.size();
    
    ciphertext.resize(8 + plaintext.size());
    memcpy(ciphertext.data(), "SCRN", 4);
    memcpy(ciphertext.data() + 4, &origSize, 4);
    memcpy(ciphertext.data() + 8, plaintext.data(), plaintext.size());
    
    xorCrypt(ciphertext.data() + 8, plaintext.size(), CRYPTO_KEY, CRYPTO_KEY_LEN);
    
    return true;
}

bool RSACrypt::Decrypt(const std::vector<BYTE>& ciphertext, std::vector<BYTE>& plaintext) {
    if (ciphertext.size() < 8) return false;
    if (memcmp(ciphertext.data(), "SCRN", 4) != 0) return false;
    
    uint32_t origSize;
    memcpy(&origSize, ciphertext.data() + 4, 4);
    
    if (ciphertext.size() < 8 + origSize) return false;
    
    plaintext.resize(origSize);
    memcpy(plaintext.data(), ciphertext.data() + 8, origSize);
    xorCrypt(plaintext.data(), origSize, CRYPTO_KEY, CRYPTO_KEY_LEN);
    
    return true;
}

void RSACrypt::Cleanup() {}

bool IsEncryptedFile(const wchar_t* path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    
    char magic[4];
    DWORD read;
    BOOL ok = ReadFile(hFile, magic, 4, &read, NULL);
    CloseHandle(hFile);
    
    return ok && read == 4 && memcmp(magic, "SCRN", 4) == 0;
}

}
