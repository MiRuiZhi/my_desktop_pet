import random
import os

def generate_key():
    return [random.randint(0, 255) for _ in range(32)]

if __name__ == "__main__":
    key = generate_key()
    
    header = """#pragma once
#include <cstdint>

namespace crypto {{

static const uint8_t CRYPTO_KEY[] = {{ {} }};
static const int CRYPTO_KEY_LEN = {};

}}
""".format(', '.join(f'0x{b:02x}' for b in key), len(key))
    
    with open("include/crypto_key.h", "w") as f:
        f.write(header)
    
    print("Key generated successfully!")
    print(f"Key: {' '.join(f'{b:02x}' for b in key)}")
