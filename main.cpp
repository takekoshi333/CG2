#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <dxcapi.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <chrono>
#include "VertexData.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include "Material.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/DirectXTex/DirectXTex.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

//ConvertStringの宣言
std::wstring ConvertString(const std::string& str);
std::string ConvertString(const std::wstring& std);

void Log(const std::string& message);
void Log(std::ostream& os, const std::string& message);

IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler);

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);
ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height);
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

Matrix4x4 MakeIdentityMatrix4x4();
Matrix4x4 MakeScaleMatrix(Vector3 scale);
Matrix4x4 MakeAffineMatrix(Vector3 scale, Vector3 theta, Vector3 translate);
Matrix4x4 MakeRotateZMatrix(float rotateZ);
Matrix4x4 MakeTranslateMatrix(Vector3 translate);
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);
Matrix4x4 MakePerspectiveFovMatirx(float fovY, float aspectRaito, float nearClip, float farClip);
Matrix4x4 Inverse(Matrix4x4 matrix);
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
float Determinant3x3(Matrix4x4 matrix, int row, int col);
DirectX::ScratchImage LoadTexture(const std::string& filePath);
void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが壊された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Windousアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	//===============================================
	// ログの初期化
	//===============================================
	// ログのディレクトリを用意
	std::filesystem::create_directory("Logs");
	// 現在時刻を取得(UTC)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> nowSeconds =
		std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };
	// formatで年月日_時分秒に変換
	std::string dataString = std::format("{:%Y%m%d_%H%M%S}", localTime);
	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dataString + ".log";
	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

	Log(logStream, "ログの初期化完了");

	//===============================================
	// COMの初期化
	//===============================================
	CoInitializeEx(0, COINIT_MULTITHREADED);

	Log(logStream, "COMの初期化完了");

	//===============================================
	// ウィンドウクラスを登録
	//===============================================
	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録
	RegisterClass(&wc);

	// クライアント領域サイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// DebugLayer
#ifdef _DEBUG
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// debugLayerを有効化
		debugController->EnableDebugLayer();
		// GPU側でもチェック
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	Log(logStream, "ウィンドウを生成");

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,
		L"CG2",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		wrc.right - wrc.left,
		wrc.bottom - wrc.top,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr
	);

	//===============================================
	// DXGIファクトリーの生成
	//===============================================
	Log(logStream, "DXGIファクトリーの生成");

	IDXGIFactory7* dxgiFactory = nullptr;
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	assert(SUCCEEDED(hr));

	//===============================================
	// 使用するアダプターを取得
	//===============================================
	Log(logStream, "使用するアダプターを取得開始");

	// 使用するアダプターの変数
	IDXGIAdapter4* useAdapter = nullptr;
	// 良い順に読み込む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(
		i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; i++) {
		// アダプターの情報を取得
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			// 採用したアダプタの情報をログに出力
			Log(ConvertString(std::format(L"USE Adapter:{}\n", adapterDesc.Description)));
			break;
		}
		// ソフトウェアアダプタの場合nullptr
		useAdapter = nullptr;
	}

	// 適切なアダプタが見つからない場合
	assert(useAdapter != nullptr);

	// D3D12Deviceの生成
	ID3D12Device* device = nullptr;
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); i++) {
		// 採用したアダプターデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// ログを出力してbreak
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスの生成がうまくいかなかった場合
	assert(device != nullptr);
	// 初期化完了ログを出力
	Log("Complete creat D3D12Device!!!\n");
	Log(logStream, "使用するアダプターを取得完了");

	// エラー、警告の際の停止
#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバいエラー時
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制
		infoQueue->PushStorageFilter(&filter);

		// 解放
		infoQueue->Release();
	}
#endif 

	//===============================================
	// コマンドキューの生成
	//===============================================
	Log(logStream, "コマンドキューを生成");

	// コマンドキューを生成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成がうまくいかなかった場合
	assert(SUCCEEDED(hr));

	//===============================================
	// コマンドアロケータの生成
	//===============================================
	Log(logStream, "コマンドアロケータを生成");

	// コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかった場合
	assert(SUCCEEDED(hr));

	//===============================================
	// コマンドリストの生成
	//===============================================
	Log(logStream, "コマンドリストを生成");

	// コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかった場合
	assert(SUCCEEDED(hr));

	//===============================================
	// スワップチェーンの生成
	//===============================================
	Log(logStream, "スワップチェーンを生成");

	// スワップチェーンを生成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));

	//===============================================
	// ディスクリプタヒープの生成
	//===============================================
	Log(logStream, "ディスクリプタヒープの生成");

	// ディスクリプタヒープの生成
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// SwapChainからResourceを引っ張ってくる
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// 取得できなければ起動しない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// DescriptorSizeを取得
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	//===============================================
	// RTVを設定
	//===============================================
	Log(logStream, "RTVを設定");

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	// RTVの数に応じてディスクリプタを用意する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 0);  // インデックス0で取得
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, 1);  // インデックス1で取得
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	//===============================================
	// Fenceを設定
	//===============================================
	Log(logStream, "Fenceを設定");

	// Fence
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	//===============================================
	// Eventを設定
	//===============================================
	Log(logStream, "Eventを設定");

	// Event
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

	//===============================================
	// dxcCompilerを初期化
	//===============================================
	Log(logStream, "dxcCompilerを初期化");

	// dxcCompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	//===============================================
	// includeの設定
	//===============================================
	Log(logStream, "includeの設定");

	// includeの設定
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	//===============================================
	// RootSignatureの生成
	//===============================================
	Log(logStream, "RootSignatureの生成");

	// RootSignatureの生成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	//===============================================
	// descriptorRangeの設定
	//===============================================
	Log(logStream, "descriptorRangeを設定");
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameterを作成(複数可)
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	//===============================================
	// samplerの設定
	//===============================================
	Log(logStream, "samplerを設定");
	D3D12_STATIC_SAMPLER_DESC staticSampler[1] = {};
	staticSampler[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler[0].ShaderRegister = 0;
	staticSampler[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSampler;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSampler);

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}
	// バイナリをもとに生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature)
	);
	assert(SUCCEEDED(hr));

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// RasterizerDtateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// Shaderをコンパイル
	IDxcBlob* vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils, dxcCompiler, includeHandler
	);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(
		L"object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils, dxcCompiler, includeHandler
	);
	assert(pixelShaderBlob != nullptr);

	//===============================================
	// depthStencilResource生成
	//===============================================
	Log(logStream, "depthStencilResource生成");
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	//===============================================
	// dsvDescriptorHeap生成
	//===============================================
	Log(logStream, "dsvDescriptorHeap生成");
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// dsv設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	// dsvheapの先頭に作る
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	//===============================================
	// depthStencilStateの設定
	//===============================================
	Log(logStream, "depthStencilStateを設定");
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOを生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature;
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジのタイプ(三角形)
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// depthStencilStateの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	// 実際に生成
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//===============================================
	// vertexResourceを作成
	//===============================================
	Log(logStream, "vertexResourceを作成");
	const uint32_t kSubdivision = 16;
	const float kLonEvery = 180.0f * 2.0f / float(kSubdivision);
	const float kLatEvery = 180.0f / float(kSubdivision);
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * (kSubdivision * kSubdivision * 6));

	// 頂点バッファビューを作成
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭データから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * (kSubdivision * kSubdivision * 6);
	// 1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);
	
	// 頂点データ
	VertexData* vertexData = nullptr;
	// アドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = (latIndex * kLatEvery - 90.0f) * (float(M_PI) / 180.0f);
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;
			float lon = lonIndex * kLonEvery * (float(M_PI) / 180.0f);

			// 頂点1
			vertexData[start].position.x = cosf(lat) * cosf(lon);
			vertexData[start].position.y = sinf(lat);
			vertexData[start].position.z = cosf(lat) * sinf(lon);
			vertexData[start].position.w = 1.0f;
			vertexData[start].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start].normal.x = vertexData[start].position.x;
			vertexData[start].normal.y = vertexData[start].position.y;
			vertexData[start].normal.z = vertexData[start].position.z;

			// 頂点2
			vertexData[start + 1].position.x = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * cosf(lon);
			vertexData[start + 1].position.y = sinf(lat + kLatEvery * (float(M_PI) / 180.0f));
			vertexData[start + 1].position.z = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * sinf(lon);
			vertexData[start + 1].position.w = 1.0f;
			vertexData[start + 1].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start + 1].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 1].normal.x = vertexData[start + 1].position.x;
			vertexData[start + 1].normal.y = vertexData[start + 1].position.y;
			vertexData[start + 1].normal.z = vertexData[start + 1].position.z;

			// 頂点3
			vertexData[start + 2].position.x = cosf(lat) * cosf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 2].position.y = sinf(lat);
			vertexData[start + 2].position.z = cosf(lat) * sinf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 2].position.w = 1.0f;
			vertexData[start + 2].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 2].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start + 2].normal.x = vertexData[start + 2].position.x;
			vertexData[start + 2].normal.y = vertexData[start + 2].position.y;
			vertexData[start + 2].normal.z = vertexData[start + 2].position.z;

			// 頂点4
			vertexData[start + 3].position.x = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * cosf(lon);
			vertexData[start + 3].position.y = sinf(lat + kLatEvery * (float(M_PI) / 180.0f));
			vertexData[start + 3].position.z = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * sinf(lon);
			vertexData[start + 3].position.w = 1.0f;
			vertexData[start + 3].texcoord.x = float(lonIndex) / float(kSubdivision);
			vertexData[start + 3].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 3].normal.x = vertexData[start + 3].position.x;
			vertexData[start + 3].normal.y = vertexData[start + 3].position.y;
			vertexData[start + 3].normal.z = vertexData[start + 3].position.z;

			// 頂点5
			vertexData[start + 4].position.x = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * cosf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 4].position.y = sinf(lat + kLatEvery * (float(M_PI) / 180.0f));
			vertexData[start + 4].position.z = cosf(lat + kLatEvery * (float(M_PI) / 180.0f)) * sinf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 4].position.w = 1.0f;
			vertexData[start + 4].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 4].texcoord.y = 1.0f - float(latIndex + 1) / float(kSubdivision);
			vertexData[start + 4].normal.x = vertexData[start + 4].position.x;
			vertexData[start + 4].normal.y = vertexData[start + 4].position.y;
			vertexData[start + 4].normal.z = vertexData[start + 4].position.z;

			// 頂点6
			vertexData[start + 5].position.x = cosf(lat) * cosf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 5].position.y = sinf(lat);
			vertexData[start + 5].position.z = cosf(lat) * sinf(lon + kLonEvery * (float(M_PI) / 180.0f));
			vertexData[start + 5].position.w = 1.0f;
			vertexData[start + 5].texcoord.x = float(lonIndex + 1) / float(kSubdivision);
			vertexData[start + 5].texcoord.y = 1.0f - float(latIndex) / float(kSubdivision);
			vertexData[start + 5].normal.x = vertexData[start + 5].position.x;
			vertexData[start + 5].normal.y = vertexData[start + 5].position.y;
			vertexData[start + 5].normal.z = vertexData[start + 5].position.z;
		}
	}

	// マテリアル用のリソース
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* materialData = nullptr;
	// 書き込むアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	// 単位行列で初期化
	materialData->uvTransform = MakeIdentityMatrix4x4();
	// 白色を書き込む
	materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	// lighting
	materialData->enableLighting = true;
	// lightingの切り替え
	bool isEnableLighting = true;

	// Sprite用のマテリアルリソース
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));
	Material* materialDataSprite = nullptr;
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	materialDataSprite->uvTransform = MakeIdentityMatrix4x4();
	materialDataSprite->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialDataSprite->enableLighting = false;

	// directionalLightResource
	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData->intensity = 1.0f;

	// transformationMatrix
	ID3D12Resource* transformationMatrixResource = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* transformationMatrixData = nullptr;
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
	transformationMatrixData->WVP = MakeIdentityMatrix4x4();
	transformationMatrixData->world = MakeIdentityMatrix4x4();

	//===============================================
	// vertexResourceSpriteの設定
	//===============================================
	Log(logStream, "vertexResourceSpriteを設定");
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 6);

	// VertexBufferrView
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点データ
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	// 1枚目
	// 左下
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };
	// 左上
	vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };
	// 右下
	vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };
	// 2枚目
	// 左上
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };
	// 右上
	vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[4].normal = { 0.0f, 0.0f, -1.0f };
	// 右下
	vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[5].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[5].normal = { 0.0f, 0.0f, -1.0f };

	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;
	indexDataSprite[1] = 1;
	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 4;
	indexDataSprite[5] = 2;

	// sprite用TransformationMatrix用リソース
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	transformationMatrixDataSprite->WVP = MakeIdentityMatrix4x4();
	transformationMatrixDataSprite->world = MakeIdentityMatrix4x4();

	// Transformの変数
	Transform transfrom{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f,0.0f} };
	Transform camaraTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };
	Transform transformSprite{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, 0.0f} };
	Transform uvTransformSprite = {
		{1.0f, 1.0f, 1.0f},
		{0.0f, 0.0f, 0.0f},
		{0.0f, 0.0f, 0.0f}
	};

	// ビューポート
	D3D12_VIEWPORT viewport{};
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	//===============================================
	// ImGuiの初期化
	//===============================================
	Log(logStream, "ImGuiを初期化");
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(
		device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

	//===============================================
	// Textureを読んで転送
	//===============================================
	Log(logStream, "Textureを読んで転送");
	// uvChecker
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);
	UploadTextureData(textureResource, mipImages);

	// monsterBall
	DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	ID3D12Resource* textureResource2 = CreateTextureResource(device, metadata2);
	UploadTextureData(textureResource2, mipImages2);

	// spriteの描画を有効
	bool isDrawSprite = true;

	//===============================================
	// ShaderResourceViewの作成
	//===============================================
	Log(logStream, "ShaderResourceViewを作成");
	// srvの設定
	// uvChacker
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// descriptorHeapの場所
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 1);

	// srvの作成
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

	// monsterBall
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// descriptorHeapの場所
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	// srvの作成
	device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

	// srvの切り替え
	bool useMonsterBall = true;

	//===============================================
	//  ウィンドウを表示
	//===============================================
	// 出力ウィンドウへの文字出力
	OutputDebugStringA("Hello DirectX!\n");

	Log(logStream, "ウィンドウを表示開始");

	// ウィンドウを表示
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};
	// ウィンドウのxボタンを押されるまでループ
	while (msg.message != WM_QUIT) {
		// windowにメッセージが来てたら最優先で処理させる
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// ゲームの処理

			//======================================
			// ImGui
			//======================================
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::Begin("Setting");
			if (ImGui::TreeNode("Camera Setting")) {
				ImGui::DragFloat3("CameraTranslate.translate", &camaraTransform.translate.x, 0.01f);
				ImGui::DragFloat3("CameraTranslate.rotate", &camaraTransform.rotate.x, 0.01f);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Material Setting")) {
				ImGui::DragFloat3("directionLight.direction", &directionalLightData->direction.x);
				ImGui::DragFloat("directionLight.intensity", &directionalLightData->intensity);
				ImGui::SliderFloat4("material.color", &materialData->color.x, 0.0f, 1.0f);
				ImGui::Checkbox("useMonsterBall", &useMonsterBall);
				ImGui::Checkbox("enableLighting", &isEnableLighting);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Sprite Setting")) {
				ImGui::DragFloat3("resourceSprite.translate", &transformSprite.translate.x);
				ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
				ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.0f, -10.0f, 10.0f);
				ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
				ImGui::Checkbox("isDrawSprite", &isDrawSprite);
				ImGui::TreePop();
			}

			ImGui::End();

			//======================================
			// WVPMatrix
			//======================================
			// material用
			transfrom.rotate.y += 0.03f;
			Matrix4x4 worldMatrix = MakeAffineMatrix(transfrom.scale, transfrom.rotate, transfrom.translate);
			Matrix4x4 cameraMatrix = MakeAffineMatrix(camaraTransform.scale, camaraTransform.rotate, camaraTransform.translate);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatirx(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			transformationMatrixData->WVP = worldViewProjectionMatrix;

			// sprite用
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
			Matrix4x4 viewMatrixSprite = MakeIdentityMatrix4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;

			// UVTransform用
			Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
			materialDataSprite->uvTransform = uvTransformMatrix;

			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			// transhitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			// barrierの張る対象
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			// 遷移前のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);
			
			// RenderTragetの設定
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			
			// 描画用descritorHeapの設定
			ID3D12DescriptorHeap* descriptorHeap[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeap);
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
			// imguiコマンド生成
			ImGui::Render();

			// DrawCall
			// 3d
			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);
			commandList->SetGraphicsRootSignature(rootSignature);
			commandList->SetPipelineState(graphicsPipelineState);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			// マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
			// lightingの有効化
			if (isEnableLighting) {
				materialData->enableLighting = true;
			} else {
				materialData->enableLighting = false;
			}
			commandList->DrawInstanced(kSubdivision * kSubdivision * 6, 1, 0, 0);
			// 2d
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
			commandList->IASetIndexBuffer(&indexBufferViewSprite);
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
			if (isDrawSprite) {
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			}
			// imgui
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);
			swapChain->Present(1, 0);
			// fenceの値を更新
			fenceValue++;
			// signalを送る
			commandQueue->Signal(fence, fenceValue);
			// signalにたどり着いたかを確認
			if (fence->GetCompletedValue() < fenceValue) {
				// たどり着くまで待つように設定
				fence->SetEventOnCompletion(fenceValue, fenceEvent);
				// イベントを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, nullptr);
			assert(SUCCEEDED(hr));


		}
	}

	// COMの終了処理
	CoUninitialize();

	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// 解放処理
	CloseHandle(fenceEvent);
	fence->Release();
	rtvDescriptorHeap->Release();
	srvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();
#ifdef _DEBUG
	debugController->Release();
#endif // _DEBUG
	vertexResource->Release();
	graphicsPipelineState->Release();
	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
	rootSignature->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
	materialResource->Release();
	materialResourceSprite->Release();
	directionalLightResource->Release();
	textureResource->Release();
	textureResource2->Release();
	transformationMatrixResource->Release();
	depthStencilResource->Release();
	dsvDescriptorHeap->Release();
	vertexResourceSprite->Release();
	transformationMatrixResourceSprite->Release();
	indexResourceSprite->Release();
	CloseWindow(hwnd);

	// リソースチェック
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

	return 0;
}

/// <summary>
/// デバッグ出力にメッセージを出力
/// </summary>
/// <param name="message">出力するログメッセージ</param>
void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

/// <summary>
/// 指定したストリームに時刻付きのログメッセージを出力
/// </summary>
/// <param name="os">出力先のストリーム</param>
/// <param name="message">出力するログメッセージ</param>
void Log(std::ostream& os, const std::string& message) {
	// 現在時刻を取得（日本時間）
	std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration> now = std::chrono::system_clock::now();
	std::chrono::zoned_time<std::chrono::system_clock::duration> localTime{ std::chrono::current_zone(), now };

	// 時刻を文字列にフォーマット
	std::string timeString = std::format("[{:%Y-%m-%d %H:%M:%S}] ", localTime);

	// 出力
	os << timeString << message << std::endl;

	// デバッグ出力にも（時刻付きで）
	OutputDebugStringA((timeString + message + "\n").c_str());
}

/// <summary>
/// UTF-8文字列をUTF-16に変換
/// </summary>
/// <param name="str">変換対象</param>
/// <returns>変換後</returns>
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

/// <summary>
/// UTF-16文字列をUTF-8に変換
/// </summary>
/// <param name="str">変換対象</param>
/// <returns>変換後</returns>
std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

/// <summary>
/// CompileShader関数
/// </summary>
/// <param name="filePath">Compilerに使用するファイルのパス</param>
/// <param name="profile">Compilerに使用するprofile</param>
/// <param name="dxcUtils">dxcUtils</param>
/// <param name="dxcCompiler">dxcCompiler</param>
/// <param name="includeHandler">includeHandler</param>
/// <returns>shaderBlob</returns>
IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler) {
	// シェーダーをコンパイルする旨をログに出力
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
	// hlslファイルを読み込む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら停止
	assert(SUCCEEDED(hr));
	//読み込んだ内容を設定
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E", L"main",
		L"-T", profile,
		L"-Zi", L"-Qembed_debug",
		L"-Od",
		L"-Zpr",
	};
	// shaderをコンパイル
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler,
		IID_PPV_ARGS(&shaderResult)
	);
	// dxcが起動できない状態のとき
	assert(SUCCEEDED(hr));

	// 警告、エラーが出たらログを出力し停止
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	// 成功したログを出力
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
	// リソースを解放
	shaderSource->Release();
	shaderResult->Release();
	// 実行用のバイナリを返却
	return shaderBlob;
}

/// <summary>
/// CreateBufferResource関数
/// </summary>
/// <param name="device">device</param>
/// <param name="sizeInBytes">sizeInBytes</param>
/// <returns>BufferResource</returns>
ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	// 頂点リソース用のヒープを設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	// 頂点リソースの設定
	D3D12_RESOURCE_DESC BufferResourceDesc{};
	// バッファリソース
	BufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	BufferResourceDesc.Width = sizeInBytes;
	BufferResourceDesc.Height = 1;
	BufferResourceDesc.DepthOrArraySize = 1;
	BufferResourceDesc.MipLevels = 1;
	BufferResourceDesc.SampleDesc.Count = 1;
	BufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	// 実際に頂点リソースを作る
	ID3D12Resource* BufferResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&BufferResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&BufferResource)
	);
	assert(SUCCEEDED(hr));
	return BufferResource;
}

/// <summary>
/// 4x4の単位行列の作成
/// </summary>
/// <returns>4x4の単位行列</returns>
Matrix4x4 MakeIdentityMatrix4x4() {
	Matrix4x4 identityMatrix;

	identityMatrix.m[0][0] = 1.0f;
	identityMatrix.m[0][1] = 0.0f;
	identityMatrix.m[0][2] = 0.0f;
	identityMatrix.m[0][3] = 0.0f;

	identityMatrix.m[1][0] = 0.0f;
	identityMatrix.m[1][1] = 1.0f;
	identityMatrix.m[1][2] = 0.0f;
	identityMatrix.m[1][3] = 0.0f;

	identityMatrix.m[2][0] = 0.0f;
	identityMatrix.m[2][1] = 0.0f;
	identityMatrix.m[2][2] = 1.0f;
	identityMatrix.m[2][3] = 0.0f;

	identityMatrix.m[3][0] = 0.0f;
	identityMatrix.m[3][1] = 0.0f;
	identityMatrix.m[3][2] = 0.0f;
	identityMatrix.m[3][3] = 1.0f;

	return identityMatrix;
}

/// <summary>
/// 4x4の拡縮行列を作成
/// </summary>
/// <param name="scale">scaleの変数</param>
/// <returns>4x4の拡縮行列</returns>
Matrix4x4 MakeScaleMatrix(Vector3 scale) {
	Matrix4x4 scaleMatirx;

	scaleMatirx.m[0][0] = scale.x;
	scaleMatirx.m[0][1] = 0.0f;
	scaleMatirx.m[0][2] = 0.0f;
	scaleMatirx.m[0][3] = 0.0f;

	scaleMatirx.m[1][0] = 0.0f;
	scaleMatirx.m[1][1] = scale.y;
	scaleMatirx.m[1][2] = 0.0f;
	scaleMatirx.m[1][3] = 0.0f;

	scaleMatirx.m[2][0] = 0.0f;
	scaleMatirx.m[2][1] = 0.0f;
	scaleMatirx.m[2][2] = scale.z;
	scaleMatirx.m[2][3] = 0.0f;

	scaleMatirx.m[3][0] = 0.0f;
	scaleMatirx.m[3][1] = 0.0f;
	scaleMatirx.m[3][2] = 0.0f;
	scaleMatirx.m[3][3] = 1.0f;

	return scaleMatirx;
}

/// <summary>
/// z軸の回転行列を作成
/// </summary>
/// <param name="rotateZ">rotateZの変数</param>
/// <returns>z軸の回転行列</returns>
Matrix4x4 MakeRotateZMatrix(float rotateZ) {
	Matrix4x4 rotateZMatrix;

	rotateZMatrix = {
		cosf(rotateZ), sinf(rotateZ), 0.0f, 0.0f,
		-sinf(rotateZ), cosf(rotateZ), 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	return rotateZMatrix;
}

/// <summary>
/// MakeAffineMatrix関数
/// </summary>
/// <param name="scale">拡縮</param>
/// <param name="rotate">回転</param>
/// <param name="translate">移動</param>
/// <returns>4x4アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(Vector3 scale, Vector3 rotate, Vector3 translate) {
	Matrix4x4 affineMatrix;

	// 回転行列を作成（Euler角を使用）
	float cosX = cosf(rotate.x);
	float sinX = sinf(rotate.x);
	float cosY = cosf(rotate.y);
	float sinY = sinf(rotate.y);
	float cosZ = cosf(rotate.z);
	float sinZ = sinf(rotate.z);

	// 回転行列（X軸回転）
	Matrix4x4 rotateX = {
		1.0f,  0.0f, 0.0f, 0.0f,
		0.0f, cosX, sinX, 0.0f,
		0.0f, -sinX,  cosX, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	// 回転行列（Y軸回転）
	Matrix4x4 rotateY = {
		cosY, 0.0f, -sinY, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		sinY, 0.0f, cosY, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	// 回転行列（Z軸回転）
	Matrix4x4 rotateZ = {
		cosZ, sinZ, 0.0f, 0.0f,
		-sinZ, cosZ, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	// 回転行列を合成
	Matrix4x4 rotationMatrix = Multiply(rotateX, Multiply(rotateY, rotateZ));

	// スケーリング行列
	Matrix4x4 scaleMatrix = {
		scale.x, 0.0f, 0.0f, 0.0f,
		0.0f, scale.y, 0.0f, 0.0f,
		0.0f, 0.0f, scale.z, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	// 平行移動行列
	Matrix4x4 translateMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		translate.x, translate.y, translate.z, 1.0f
	};

	affineMatrix = Multiply(translateMatrix, Multiply(scaleMatrix, rotationMatrix));

	return affineMatrix;
}

/// <summary>
/// 平行移動行列の作成
/// </summary>
/// <param name="translate"></param>
/// <returns></returns>
Matrix4x4 MakeTranslateMatrix(Vector3 translate) {
	Matrix4x4 translateMatrix;

	translateMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		translate.x, translate.y, translate.z, 1.0f
	};

	return translateMatrix;
}

/// <summary>
/// 4x4行列の乗算
/// </summary>
/// <param name="matrix1">4x4行列 1</param>
/// <param name="matrix2">4x4行列 2</param>
/// <returns>4x4行列の乗算</returns>
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2) {
	Matrix4x4 multiplyMatrix;
	float multiply[4];  // 4x4行列に合わせてサイズを変更

	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			for (int i = 0; i < 4; i++) {
				multiply[i] = matrix1.m[row][i] * matrix2.m[i][column];
			}
			multiplyMatrix.m[row][column] = multiply[0] + multiply[1] + multiply[2] + multiply[3];
		}
	}

	return multiplyMatrix;
}

/// <summary>
/// MakePerspectiveFovMatirx関数
/// </summary>
/// <param name="fovY">画角</param>
/// <param name="aspectRaito">アスペクト比</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>透視射影行列</returns>
Matrix4x4 MakePerspectiveFovMatirx(float fovY, float aspectRaito, float nearClip, float farClip) {
	Matrix4x4 perspectiveFovMatirx;

	perspectiveFovMatirx = {
		1.0f / aspectRaito * (cosf(fovY / 2.0f) / sinf(fovY / 2.0f)), 0.0f, 0.0f, 0.0f,
		0.0f, (cosf(fovY / 2.0f) / sinf(fovY / 2.0f)), 0.0f, 0.0f,
		0.0f, 0.0f, farClip / farClip - nearClip, 1.0f,
		0.0f, 0.0f, -nearClip * farClip / (farClip - nearClip), 0.0f
	};

	return perspectiveFovMatirx;
}

/// <summary>
/// 4x4の逆行列の作成
/// </summary>
/// <param name="matrix">元の行列</param>
/// <returns>逆行列</returns>
Matrix4x4 Inverse(Matrix4x4 matrix) {
	Matrix4x4 inverseMatrix;
	float determinant;

	// 4x4行列の行列式を計算
	determinant = matrix.m[0][0] * Determinant3x3(matrix, 0, 0)
		- matrix.m[0][1] * Determinant3x3(matrix, 0, 1)
		+ matrix.m[0][2] * Determinant3x3(matrix, 0, 2)
		- matrix.m[0][3] * Determinant3x3(matrix, 0, 3);

	// 余因子行列を求める
	Matrix4x4 cofactorMatrix;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			cofactorMatrix.m[i][j] = ((i + j) % 2 == 0 ? 1 : -1) * Determinant3x3(matrix, i, j);
		}
	}

	// 余因子行列の転置を求める（余因子行列の転置は共役行列に相当）
	Matrix4x4 adjugateMatrix;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			adjugateMatrix.m[i][j] = cofactorMatrix.m[j][i];
		}
	}

	// 逆行列 = (1 / 行列式) * 共役行列
	float invDet = 1.0f / determinant;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			inverseMatrix.m[i][j] = adjugateMatrix.m[i][j] * invDet;
		}
	}

	return inverseMatrix;
}

/// <summary>
/// 3x3の行列式を計算する関数
/// </summary>
/// <param name="matrix">元の4x4行列</param>
/// <param name="row">対象の行</param>
/// <param name="col">対象の列</param>
/// <returns>3x3行列の行列式</returns>
float Determinant3x3(Matrix4x4 matrix, int row, int col) {
	// 3x3の部分行列を取得
	float m[3][3];
	int mRow = 0;
	for (int i = 0; i < 4; i++) {
		if (i == row) continue;
		int mCol = 0;
		for (int j = 0; j < 4; j++) {
			if (j == col) continue;
			m[mRow][mCol] = matrix.m[i][j];
			mCol++;
		}
		mRow++;
	}

	// 3x3の行列式を計算
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
		- m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
		+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/// <summary>
/// CreateDescriptorHeap関数
/// </summary>
/// <param name="device">device</param>
/// <param name="heapType">heapType</param>
/// <param name="numDescriptors">numDescriptors</param>
/// <param name="shaderVisible">shaderVisible</param>
/// <returns>descriptorHeap</returns>
ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {
	// ディスクリプタヒープの生成
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	// ディスクリプタヒープが生成できなかった場合
	assert(SUCCEEDED(hr));
	return descriptorHeap;
}

/// <summary>
/// LoadTexture関数
/// </summary>
/// <param name="filePath">filePath</param>
/// <returns>mipImages</returns>
DirectX::ScratchImage LoadTexture(const std::string& filePath) {
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));
	return mipImages;
}

/// <summary>
/// CreateTextureResource関数
/// </summary>
/// <param name="device">device</param>
/// <param name="metadata">metadata</param>
/// <returns>resource</returns>
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	// metadataをもとにresourceを設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);

	// 利用するheapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// Resourceの作成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));

	return resource;
}

/// <summary>
/// UploadTextureData関数
/// </summary>
/// <param name="texture">texture</param>
/// <param name="mipImages">mipImages</param>
void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages) {
	// Meta情報を取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	// 全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {
		// mipLevelを指定して各Imageを取得
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);
		// Textureに転送
		HRESULT hr = texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,
			img->pixels,
			UINT(img->rowPitch),
			UINT(img->slicePitch)
		);
		assert(SUCCEEDED(hr));
	}
}

/// <summary>
/// CreateDepthStencilTextureResource関数
/// </summary>
/// <param name="device">device</param>
/// <param name="width">width</param>
/// <param name="height">height</param>
/// <returns>depthStencilResource</returns>
ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
	// 生成するresourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 利用するheapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));
	return resource;
}

/// <summary>
/// MakeOrthographicMatrix関数
/// </summary>
/// <param name="left">left</param>
/// <param name="top">top</param>
/// <param name="right">right</param>
/// <param name="bottom">bottom</param>
/// <param name="nearClip">nearClip</param>
/// <param name="farClip">farClip</param>
/// <returns>3次元正射影行列</returns>
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 orthographicMatrix;

	orthographicMatrix.m[0][0] = 2.0f / (right - left);
	orthographicMatrix.m[0][1] = 0.0f;
	orthographicMatrix.m[0][2] = 0.0f;
	orthographicMatrix.m[0][3] = 0.0f;

	orthographicMatrix.m[1][0] = 0.0f;
	orthographicMatrix.m[1][1] = 2.0f / (top - bottom);
	orthographicMatrix.m[1][2] = 0.0f;
	orthographicMatrix.m[1][3] = 0.0f;

	orthographicMatrix.m[2][0] = 0.0f;
	orthographicMatrix.m[2][1] = 0.0f;
	orthographicMatrix.m[2][2] = 1.0f / (farClip - nearClip);
	orthographicMatrix.m[2][3] = 0.0f;

	orthographicMatrix.m[3][0] = (left + right) / (left - right);
	orthographicMatrix.m[3][1] = (top + bottom) / bottom - top;
	orthographicMatrix.m[3][2] = (nearClip) / (nearClip - farClip);
	orthographicMatrix.m[3][3] = 1.0f;

	return orthographicMatrix;
}

/// <summary>
/// GetCPUDescriptorHandle関数
/// </summary>
/// <param name="descriptorHeap">descriptorHeap</param>
/// <param name="descriptorSize">descriptorSize</param>
/// <param name="index">index</param>
/// <returns>特定のインデックスのDescriptorHandleを取得</returns>
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;
}

/// <summary>
/// GetGPUDescriptorHandle関数
/// </summary>
/// <param name="descriptorHeap">descriptorHeap</param>
/// <param name="descriptorSize">descriptorSize</param>
/// <param name="index">index</param>
/// <returns>特定のインデックスのDescriptorHandleを取得</returns>
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;
}