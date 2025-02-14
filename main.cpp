#include <Windows.h>
#include <cstdint>

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
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

	// 出力ウィンドウへの文字出力
	OutputDebugStringA("Hello DirectX!\n");
	
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
		}
	}

	return 0;
}