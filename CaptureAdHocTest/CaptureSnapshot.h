#pragma once

class CaptureSnapshot
{
public:
    static winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>
        TakeAsync(
            winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
            bool asStagingTexture = false,
            bool cursorEnabled = true);

private:
    CaptureSnapshot() = delete;
};