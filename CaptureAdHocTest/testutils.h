#pragma once

template<typename T>
inline void check_color(T value, winrt::Windows::UI::Color const& expected)
{
	if (value != expected)
	{
		std::wstringstream stringStream;
		stringStream << L"Color comparison failed!";
		stringStream << std::endl;
		stringStream << L"\tValue: ( B: " << value.B << L", G: " << value.G << ", R: " << value.R << ", A: " << value.A << " )";
		stringStream << std::endl;
		stringStream << L"\tExpected: ( B: " << expected.B << L", G: " << expected.G << ", R: " << expected.R << ", A: " << expected.A << " )";
		stringStream << std::endl;
		throw winrt::hresult_error(E_FAIL, stringStream.str());
	}
}

class MappedTexture
{
public:
	struct BGRAPixel
	{
		BYTE B;
		BYTE G;
		BYTE R;
		BYTE A;

		bool operator==(const winrt::Windows::UI::Color& color) { return B == color.B && G == color.G && R == color.R && A == color.A; }
		bool operator!=(const winrt::Windows::UI::Color& color) { return !(*this == color); }

		winrt::Windows::UI::Color to_color() { return winrt::Windows::UI::Color{ A, R, G, B }; }
	};

	MappedTexture(winrt::com_ptr<ID3D11DeviceContext> d3dContext, winrt::com_ptr<ID3D11Texture2D> texture)
	{
		m_d3dContext = d3dContext;
		m_texture = texture;
		m_texture->GetDesc(&m_textureDesc);
		winrt::check_hresult(m_d3dContext->Map(m_texture.get(), 0, D3D11_MAP_READ, 0, &m_mappedData));
	}
	~MappedTexture()
	{
		m_d3dContext->Unmap(m_texture.get(), 0);
	}

	BGRAPixel ReadBGRAPixel(uint32_t x, uint32_t y)
	{
		if (x < m_textureDesc.Width && y < m_textureDesc.Height)
		{
			auto bytesPerPixel = 4;
			auto data = static_cast<BYTE*>(m_mappedData.pData);
			auto offset = (m_mappedData.RowPitch * y) + (x * bytesPerPixel);
			auto B = data[offset + 0];
			auto G = data[offset + 1];
			auto R = data[offset + 2];
			auto A = data[offset + 3];
			return BGRAPixel{ B, G, R, A };
		}
		else
		{
			throw winrt::hresult_out_of_bounds();
		}
	}

private:
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11Texture2D> m_texture;
	D3D11_MAPPED_SUBRESOURCE m_mappedData = {};
	D3D11_TEXTURE2D_DESC m_textureDesc = {};
};

void TestSurfaceAtPoint(
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device, 
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface, 
	winrt::Windows::UI::Color expectedColor,
	uint32_t x,
	uint32_t y)
{
	auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(device);
	winrt::com_ptr<ID3D11DeviceContext> d3dContext;
	d3dDevice->GetImmediateContext(d3dContext.put());

	auto frameTexture = robmikh::common::uwp::CopyD3DTexture(d3dDevice, GetDXGIInterfaceFromObject<ID3D11Texture2D>(surface), true);
	D3D11_TEXTURE2D_DESC desc = {};
	frameTexture->GetDesc(&desc);
	auto mapped = MappedTexture(d3dContext, frameTexture);
	check_color(mapped.ReadBGRAPixel(x, y), expectedColor);
}

void TestCenterOfSurface(
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface,
	winrt::Windows::UI::Color expectedColor)
{
	auto desc = surface.Description();
	return TestSurfaceAtPoint(device, surface, expectedColor, (uint32_t)desc.Width / 2, (uint32_t)desc.Height / 2);
}

// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setsystemcursor
enum class CursorType : DWORD
{
	Normal = 32512,
	Wait = 32514,
	AppStarting = 32650
};

struct CursorScope
{
	CursorScope(wil::shared_hcursor const& cursor, CursorType cursorType)
	{
		m_cursor.reset(CopyCursor(cursor.get()));
		m_type = cursorType;

		m_oldCursor.reset(CopyCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW((DWORD)m_type))));

		winrt::check_bool(SetSystemCursor(m_cursor.get(), (DWORD)m_type));
	}

	~CursorScope()
	{
		winrt::check_bool(SetSystemCursor(m_oldCursor.get(), (DWORD)m_type));
	}

private:
	wil::unique_hcursor m_cursor;
	wil::unique_hcursor m_oldCursor;
	CursorType m_type;
};

template <typename T>
T CreateWin32Struct()
{
	T thing = {};
	thing.cbSize = sizeof(T);
	return thing;
}