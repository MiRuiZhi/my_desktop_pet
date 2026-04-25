#pragma once
#include <windows.h>
#include <vector>

namespace screenshot {

struct ImageData {
    std::vector<BYTE> pixels;
    int width;
    int height;
    int channels;
};

bool CaptureScreen(ImageData& out_image);
bool SavePNG(const ImageData& image, const wchar_t* filepath);

}
