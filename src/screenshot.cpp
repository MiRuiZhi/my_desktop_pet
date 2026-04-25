#include "screenshot.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace screenshot {

static uint32_t ByteSwap32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static uint32_t adler32(const unsigned char* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

static void zlib_compress_no_compress(const std::vector<unsigned char>& input, std::vector<unsigned char>& output) {
    output.push_back(0x78);
    output.push_back(0x01);
    
    size_t pos = 0;
    while (pos < input.size()) {
        size_t block_size = input.size() - pos;
        if (block_size > 65535) block_size = 65535;
        
        bool is_last = (pos + block_size >= input.size());
        output.push_back(is_last ? 0x01 : 0x00);
        output.push_back(block_size & 0xFF);
        output.push_back((block_size >> 8) & 0xFF);
        output.push_back((~block_size) & 0xFF);
        output.push_back(((~block_size) >> 8) & 0xFF);
        
        for (size_t i = 0; i < block_size; i++) {
            output.push_back(input[pos + i]);
        }
        pos += block_size;
    }
    
    uint32_t checksum = adler32(input.data(), input.size());
    output.push_back((checksum >> 24) & 0xFF);
    output.push_back((checksum >> 16) & 0xFF);
    output.push_back((checksum >> 8) & 0xFF);
    output.push_back(checksum & 0xFF);
}

bool CaptureScreen(ImageData& out_image) {
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return false;

    int width = GetDeviceCaps(hScreenDC, HORZRES);
    int height = GetDeviceCaps(hScreenDC, VERTRES);

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    if (!hBitmap) {
        DeleteDC(hMemoryDC);
        ReleaseDC(NULL, hScreenDC);
        return false;
    }

    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    out_image.width = width;
    out_image.height = height;
    out_image.channels = 3;
    out_image.pixels.resize(width * height * 3);

    GetDIBits(hMemoryDC, hBitmap, 0, height, out_image.pixels.data(), &bmi, DIB_RGB_COLORS);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    return true;
}

static void WritePNGChunk(FILE* f, const char* type, const unsigned char* data, unsigned int len) {
    unsigned int len_be = ByteSwap32(len);
    fwrite(&len_be, 4, 1, f);
    fwrite(type, 4, 1, f);
    if (data && len > 0) fwrite(data, len, 1, f);

    unsigned char* crc_data = (unsigned char*)malloc(len + 4);
    memcpy(crc_data, type, 4);
    if (data && len > 0) memcpy(crc_data + 4, data, len);

    unsigned int c = 0xFFFFFFFF;
    for (unsigned int i = 0; i < len + 4; i++) {
        c ^= crc_data[i];
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320 : 0);
    }
    unsigned int crc_be = ByteSwap32(c ^ 0xFFFFFFFF);
    fwrite(&crc_be, 4, 1, f);
    free(crc_data);
}

bool SavePNG(const ImageData& image, const wchar_t* filepath) {
    FILE* f = _wfopen(filepath, L"wb");
    if (!f) return false;

    unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 8, 1, f);

    unsigned char ihdr[13];
    *(uint32_t*)(ihdr + 0) = ByteSwap32((uint32_t)image.width);
    *(uint32_t*)(ihdr + 4) = ByteSwap32((uint32_t)image.height);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    WritePNGChunk(f, "IHDR", ihdr, 13);

    int row_bytes = image.width * 3;
    std::vector<unsigned char> raw_data;
    raw_data.reserve(image.height * (row_bytes + 1));

    for (int y = 0; y < image.height; y++) {
        raw_data.push_back(0);
        for (int x = 0; x < image.width; x++) {
            int idx = (y * image.width + x) * 3;
            raw_data.push_back(image.pixels[idx + 2]);
            raw_data.push_back(image.pixels[idx + 1]);
            raw_data.push_back(image.pixels[idx + 0]);
        }
    }

    std::vector<unsigned char> compressed;
    zlib_compress_no_compress(raw_data, compressed);
    WritePNGChunk(f, "IDAT", compressed.data(), (unsigned int)compressed.size());

    WritePNGChunk(f, "IEND", NULL, 0);

    fclose(f);
    return true;
}

}
