#include "engine/render/ImageData.hpp"

#include <wincodec.h>
#include <wrl/client.h>

#include <stdexcept>
#include <string>

namespace sokoban {

ImageData loadRgbaImage(const std::filesystem::path& path)
{
    using Microsoft::WRL::ComPtr;

    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Failed to initialize COM for image loading");
    }

    try {
        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(result)) {
            throw std::runtime_error("Failed to create WIC imaging factory");
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (FAILED(result)) {
            throw std::runtime_error("Failed to decode image: " + path.string());
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, &frame);
        if (FAILED(result)) {
            throw std::runtime_error("Failed to read image frame: " + path.string());
        }

        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(&converter);
        if (FAILED(result)) {
            throw std::runtime_error("Failed to create WIC format converter");
        }
        result = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(result)) {
            throw std::runtime_error("Failed to convert image to RGBA: " + path.string());
        }

        ImageData image;
        result = converter->GetSize(&image.width, &image.height);
        if (FAILED(result) || image.width == 0 || image.height == 0) {
            throw std::runtime_error("Invalid image dimensions: " + path.string());
        }
        const uint32_t stride = image.width * 4;
        image.rgba.resize(static_cast<size_t>(stride) * image.height);
        result = converter->CopyPixels(
            nullptr,
            stride,
            static_cast<uint32_t>(image.rgba.size()),
            reinterpret_cast<BYTE*>(image.rgba.data()));
        if (FAILED(result)) {
            throw std::runtime_error("Failed to copy image pixels: " + path.string());
        }

        if (uninitialize) {
            CoUninitialize();
        }
        return image;
    } catch (...) {
        if (uninitialize) {
            CoUninitialize();
        }
        throw;
    }
}

} // namespace sokoban
