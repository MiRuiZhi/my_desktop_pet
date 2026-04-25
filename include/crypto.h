#pragma once
#include <windows.h>
#include <vector>
#include <string>

namespace crypto {

class RSACrypt {
public:
    bool Init();
    bool Encrypt(const std::vector<BYTE>& plaintext, std::vector<BYTE>& ciphertext);
    bool Decrypt(const std::vector<BYTE>& ciphertext, std::vector<BYTE>& plaintext);
    void Cleanup();
};

bool IsEncryptedFile(const wchar_t* path);

}
