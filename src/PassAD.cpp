#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <thread>
#include "qrcodegen.hpp"
#include "app.h"
#include "PassAD.hpp"

std::string configFileName = "";
std::string GenerateConfigFileName(const nlohmann::json &screenResolution) {
    int screenWidth = screenResolution["Screen Resolution"]["Width"];
    int screenHeight = screenResolution["Screen Resolution"]["Height"];
    std::string configFileName =
            "./conf/config_" + std::to_string(screenWidth) + "x" + std::to_string(screenHeight) + ".json";
    return configFileName;
}
void WriteToOutputTextBox(const std::string &text) {
    outputBuffer << "\r\n" << text;
    std::string outputText = outputBuffer.str();
    SetWindowTextA(outputTextBox, outputText.c_str());
    SendMessage(outputTextBox, EM_LINESCROLL, 0, SendMessage(outputTextBox, EM_GETLINECOUNT, 0, 0));

    // 设置字体
    HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
    SendMessage(outputTextBox, WM_SETFONT, (WPARAM) hFont, TRUE);
}

void DisplayInstructions() {
    WriteToOutputTextBox("\r\n软件说明：\r\n"
                         "1，用途：识别屏幕下方区域中的关键图片，发现后模拟按下向下按键，跳过广告。\r\n"
                         "2，设置：识别区域可配置，模拟按键可配置，请自行修改conf/的配置下文件。\r\n"
                         "3，如果无法准确识别广告，请自行截取所要拦截图片，保存为bad.png。\r\n"
                         "4，调小config中的confidence值，可提高识别模糊程度。\r\n"
                         "5，按键：按下Esc键，暂停/恢复识别。D/Y/X/B是快捷键；\"=\"Warp链接，\"-\"Warp断链\r\n");
}
// 读取配置文件
bool ReadConfigFile(const std::string &filename, nlohmann::json &config) {
    std::ifstream configFile(filename);
    if (!configFile) {
        outputBuffer << "无法打开配置文件" << std::endl;
        SetWindowText(outputTextBox, outputBuffer.str().c_str());
        return false;
    }

    std::string configContent((std::istreambuf_iterator<char>(configFile)), std::istreambuf_iterator<char>());
    configFile.close();

    try {
        config = nlohmann::json::parse(configContent);

        // 忽略 __comment__ 键名及其对应的值
        config.erase("__comment__");
    } catch (const std::exception &e) {
        outputBuffer << "配置文件解析错误: " << e.what() << std::endl;
        SetWindowText(outputTextBox, outputBuffer.str().c_str());
        return false;
    }

    return true;
}

// 保存配置文件
bool SaveConfigFile(const std::string &filename, const nlohmann::json &config) {
    std::ofstream configFile(filename);
    if (!configFile) {
        outputBuffer << "无法保存配置文件" << std::endl;
        SetWindowText(outputTextBox, outputBuffer.str().c_str());
        return false;
    }

    configFile << config.dump(4);
    configFile.close();

    return true;
}

bool CaptureScreenResolution(nlohmann::json &screenResolution) {
    HWND hWnd = GetDesktopWindow();

    // Get the main window's device context
    HDC hDC = GetDC(hWnd);

    // Get the DPI values of the main window
    int dpiX = GetDpiForWindow(hWnd);
    int dpiY = dpiX; // Assume X and Y DPI values are the same

    // Get the screen resolution of the main window
    RECT rect;
    GetClientRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    // Calculate the scaling factors
    float scaleX = static_cast<float>(dpiX) / 96.0f;
    float scaleY = static_cast<float>(dpiY) / 96.0f;

    // Calculate the actual screen resolution
    int actualWidth = static_cast<int>(width * scaleX);
    int actualHeight = static_cast<int>(height * scaleY);

    // Store the screen resolution in the JSON object
    screenResolution["Window Size"]["Width"] = width;
    screenResolution["Window Size"]["Height"] = height;
    screenResolution["DPI"]["X"] = dpiX;
    screenResolution["DPI"]["Y"] = dpiY;
    screenResolution["Scale"]["X"] = scaleX;
    screenResolution["Scale"]["Y"] = scaleY;
    screenResolution["Screen Resolution"]["Width"] = actualWidth;
    screenResolution["Screen Resolution"]["Height"] = actualHeight;

    ReleaseDC(hWnd, hDC);
    return true;
}

static RECT normalRect;
static DWORD dwStyle;
static const wchar_t* windowClassName = L"PassADWindowClass";
static const wchar_t* windowTitle = L"PassAD";

void SaveWindowPosition(HWND hwnd) {
    WINDOWPLACEMENT wp;
    wp.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &wp);

    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\PassAD", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "WindowPosition", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wp), sizeof(WINDOWPLACEMENT));
        RegCloseKey(hKey);
    }
}

void LoadWindowPosition(HWND hwnd, int& windowX, int& windowY) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\PassAD", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WINDOWPLACEMENT wp;
        DWORD dataSize = sizeof(WINDOWPLACEMENT);
        if (RegQueryValueExA(hKey, "WindowPosition", NULL, NULL, reinterpret_cast<BYTE*>(&wp), &dataSize) == ERROR_SUCCESS) {
            wp.length = sizeof(WINDOWPLACEMENT);
            SetWindowPlacement(hwnd, &wp);
            windowX = wp.rcNormalPosition.left;
            windowY = wp.rcNormalPosition.top;
        } else {
            // 如果没有保存的窗口位置信息，则使用默认的窗口位置
            // 这里可以根据需要设置默认的窗口位置
            SetWindowPos(hwnd, NULL, windowX, windowY, 450, 300, 0);
        }
        RegCloseKey(hKey);
    } else {
        // 如果无法打开注册表项，则使用默认的窗口位置
        // 这里可以根据需要设置默认的窗口位置
        SetWindowPos(hwnd, NULL, windowX, windowY, 450, 400, 0);
    }
}
// 保存识别区图像
void SaveRecognitionAreaImage(const std::string &filename, int x, int y, int width, int height) {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);

    SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, x, y, SRCCOPY);

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = 0;
    bmi.bmiHeader.biXPelsPerMeter = 0;
    bmi.bmiHeader.biYPelsPerMeter = 0;
    bmi.bmiHeader.biClrUsed = 0;
    bmi.bmiHeader.biClrImportant = 0;

    cv::Mat image(height, width, CV_8UC3);
    GetDIBits(hMemoryDC, hBitmap, 0, height, image.data, &bmi, DIB_RGB_COLORS);

    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);
    cv::imwrite(filename, image);

    ReleaseDC(NULL, hScreenDC);
    DeleteDC(hMemoryDC);
    DeleteObject(hBitmap);

}

// 退出菜单回调函数
void OnExitMenuClicked() {
    stopCapture = true;
    PostQuitMessage(0);
}

// 重新加载菜单回调函数
void OnReloadMenuClicked() {

    nlohmann::json config;
    if (ReadConfigFile(configFileName, config)) {
        WriteToOutputTextBox("重新加载配置文件");
    }
}

// 恢复配置默认值菜单回调函数
void OnRestoreDefaultConfigMenuClicked() {

    nlohmann::json defaultConfigJson = nlohmann::json::parse(defaultConfig);
    if (SaveConfigFile(configFileName, defaultConfigJson)) {
        WriteToOutputTextBox("已恢复默认配置值");
    }
}

#include <shellapi.h>

// 显示图片菜单回调函数
void OnShowImageMenuClicked() {
    ShellExecuteW(NULL, L"open", L"recognition_area_image.png", NULL, NULL, SW_SHOW);
}

// dump识别区图像菜单回调函数
void OnDumpRecognitionAreaImageMenuClicked() {
    nlohmann::json config;
    if (!ReadConfigFile(configFileName, config)) {
        if (SaveConfigFile(configFileName, nlohmann::json::parse(defaultConfig))) {
            WriteToOutputTextBox("已创建默认配置文件");
        } else {
            WriteToOutputTextBox("无法创建默认配置文件");
        }
    }
    SaveRecognitionAreaImage("recognition_area_image.png", config["x"], config["y"], config["w"], config["h"]);
    WriteToOutputTextBox("已保存识别区图像: recognition_area_image.png");

    // 显示图片
    OnShowImageMenuClicked();
}

void GenerateQRCode(const std::string &url, const std::string &imageName) {
    // 创建QR Code对象
    qrcodegen::QrCode qrCode = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::HIGH);

    // 计算QR Code的大小
    constexpr int scaleFactor = 10;
    const int qrSize = qrCode.getSize();
    const int imageSize = qrSize * scaleFactor;

    // 创建OpenCV图像
    cv::Mat qrCodeImage(imageSize, imageSize, CV_8UC1, cv::Scalar(255));

    // 绘制QR Code图像
    for (int y = 0; y < qrSize; ++y) {
        for (int x = 0; x < qrSize; ++x) {
            if (qrCode.getModule(x, y)) {
                cv::Rect roi(x * scaleFactor, y * scaleFactor, scaleFactor, scaleFactor);
                qrCodeImage(roi) = cv::Scalar(0);
            }
        }
    }

    // 加载本地图片
    cv::Mat titleImage = cv::imread(imageName);

    // 创建上下拼接的图像
    cv::Mat combinedImage(imageSize + titleImage.rows, imageSize, CV_8UC3, cv::Scalar(255, 255, 255));

    // 将QR Code图像复制到拼接图像中
    cv::Mat qrCodeRegion = combinedImage(cv::Rect(0, 0, imageSize, imageSize));
    cv::cvtColor(qrCodeImage, qrCodeRegion, cv::COLOR_GRAY2BGR);

    // 将本地图片复制到拼接图像中
    cv::Mat titleRegion = combinedImage(cv::Rect(0, imageSize, titleImage.cols, titleImage.rows));
    titleImage.copyTo(titleRegion);

    // 显示拼接图像
    cv::imshow("扫码打赏", combinedImage);
    cv::waitKey(0); // 等待用户按下任意键
}

void ToggleWindowStyle(HWND hwnd, RECT &normalRect, DWORD &dwStyle, bool &isMinimized, UINT menuItemID) {
    if (!isMinimized) {
        // 记录当前窗口尺寸和样式
        GetWindowRect(hwnd, &normalRect);
        dwStyle = GetWindowLong(hwnd, GWL_STYLE);
        // 调整窗口尺寸为最小尺寸
        SetWindowPos(hwnd, NULL, 0, 0, 80, 190, SWP_NOZORDER | SWP_NOMOVE);
        // 移除最大化和最小化按钮
        dwStyle &= ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
        SetWindowLong(hwnd, GWL_STYLE, dwStyle);

        // 修改菜单文字为"正常"
        wchar_t menuText[] = L"扩展窗口++";
        char menuTextA[sizeof(menuText)];
        WideCharToMultiByte(CP_ACP, 0, menuText, -1, menuTextA, sizeof(menuTextA), NULL, NULL);
        ModifyMenuA(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, menuTextA);
        // 更新窗口
        RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
        // 保存窗口状态
        isMinimized = true;
    } else {
        // 恢复窗口尺寸为原有尺寸
        SetWindowPos(hwnd, NULL, normalRect.left, normalRect.top,
                     normalRect.right - normalRect.left, normalRect.bottom - normalRect.top,
                     SWP_NOZORDER);
        // 恢复最大化和最小化按钮
        dwStyle |= (WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
        SetWindowLong(hwnd, GWL_STYLE, dwStyle);

        // 修改菜单文字为"极小"
        wchar_t menuText[] = L"极简风格==";
        char menuTextA[sizeof(menuText)];
        WideCharToMultiByte(CP_ACP, 0, menuText, -1, menuTextA, sizeof(menuTextA), NULL, NULL);
        ModifyMenuA(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, menuTextA);
        // 更新窗口
        RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
        isMinimized = false;
    }
}

void ToggleRecognition(HWND hwnd, bool &isPaused, UINT menuItemID) {
    if (isPaused) {
        // 恢复
        isPaused = false;
        ModifyMenu(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, "==暂停识别");
        programPaused = false;
        WriteToOutputTextBox("图像识别已恢复");
    } else {
        // 暂停
        isPaused = true;
        ModifyMenu(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, "++识别广告");
        programPaused = true;
        WriteToOutputTextBox("图像识别已暂停");
    }
}

void ToggleWindowTopmost(HWND hwnd, bool &isWindowTopmost, UINT menuItemID) {
    if (isWindowTopmost) {
        // 取消置顶
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        isWindowTopmost = false;
        ModifyMenu(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, "置顶窗口 ↑↑");
    } else {
        // 置顶窗口
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        isWindowTopmost = true;
        ModifyMenu(GetMenu(hwnd), menuItemID, MF_BYCOMMAND, menuItemID, "取消置顶 ↓↓");
    }
}

void OpenURL(const std::string& url) {
    std::wstring wideUrl;
    wideUrl.resize(MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0));
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wideUrl[0], static_cast<int>(wideUrl.size()));

    SHELLEXECUTEINFOW ShExecInfo = { 0 };
    ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
    ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ShExecInfo.hwnd = NULL;
    ShExecInfo.lpVerb = L"open";
    ShExecInfo.lpFile = wideUrl.c_str();
    ShExecInfo.lpParameters = NULL;
    ShExecInfo.lpDirectory = NULL;
    ShExecInfo.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&ShExecInfo)) {
        std::thread waitThread([&]() {
            WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
            CloseHandle(ShExecInfo.hProcess);
        });

        waitThread.detach();
    }
}

// 图像处理线程函数
void ImageProcessingThread(const nlohmann::json &screenResolution, const nlohmann::json &config) {
    // 初始化线程的 COM 环境
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int adjusted_x = config["x"];
    int adjusted_y = config["y"];
    int adjusted_width = config["w"];
    int adjusted_height = config["h"];
    int keyToPress = config["keyToPress"];
    int sleepValue = config["sleepValue"]; // 读取 sleep 的值

    float screen_width = screenResolution["Screen Resolution"]["Width"];
    float screen_height = screenResolution["Screen Resolution"]["Height"];
    int screen_width_int = static_cast<int>(screen_width);
    int screen_height_int = static_cast<int>(screen_height);

    float dpi_scale_x = screenResolution["Scale"]["X"];
    float dpi_scale_y = screenResolution["Scale"]["Y"];

    float dpi_scale = (screen_width / config["resolution"]);

    adjusted_x = static_cast<int>(adjusted_x);
    adjusted_y = static_cast<int>(adjusted_y );
    adjusted_width = static_cast<int>(adjusted_width );
    adjusted_height = static_cast<int>(adjusted_height);

    DisplayInstructions();
    WriteToOutputTextBox("屏幕参数：" + std::to_string(screen_width_int)
                         + "/" + std::to_string(screen_height_int)
                         + "--调整缩放比例：" + std::to_string(dpi_scale));
    WriteToOutputTextBox("当前识别区：左上点：X: " + std::to_string(adjusted_x)
                         + " - Y: " + std::to_string(adjusted_y) + " / 宽: " + std::to_string(adjusted_width)
                         + " - 高: " + std::to_string(adjusted_height));

    HDC hScreenDC = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, adjusted_width, adjusted_height);

    SelectObject(hMemoryDC, hBitmap);

    std::vector<std::pair<cv::Mat, double>> blacklistedImages;
    for (const auto &imageInfo: config["blacklist"]) {
        std::string imageFile = imageInfo["image"];
        double confidence = imageInfo["confidence"];

        cv::Mat image = cv::imread(imageFile, cv::IMREAD_COLOR);
        if (!image.empty()) {
            blacklistedImages.push_back({image, confidence});
        } else {
            WriteToOutputTextBox("无法读取图像文件: " + imageFile);
        }
    }

    while (!stopCapture) {
        if (programPaused) {
            Sleep(sleepValue); // 使用从配置中读取的 sleep 值
            continue;
        }

        BitBlt(hMemoryDC, 0, 0, adjusted_width, adjusted_height, hScreenDC, adjusted_x, adjusted_y, SRCCOPY);

        cv::Mat screenshot;
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = adjusted_width;
        bmi.bmiHeader.biHeight = -adjusted_height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 0;
        bmi.bmiHeader.biXPelsPerMeter = 0;
        bmi.bmiHeader.biYPelsPerMeter = 0;
        bmi.bmiHeader.biClrUsed = 0;
        bmi.bmiHeader.biClrImportant = 0;

        cv::Mat temp(adjusted_height, adjusted_width, CV_8UC3);
        GetDIBits(hMemoryDC, hBitmap, 0, adjusted_height, temp.data, &bmi, DIB_RGB_COLORS);

        cv::cvtColor(temp, screenshot, cv::COLOR_BGR2RGB);

        for (const auto &blacklistedImageInfo : config["blacklist"]) {
            std::string imageFile = blacklistedImageInfo["image"];
            double confidence = blacklistedImageInfo["confidence"];

            cv::Mat blacklistedImage = cv::imread(imageFile, cv::IMREAD_COLOR);
            if (!blacklistedImage.empty()) {
                blacklistedImages.push_back({blacklistedImage, confidence});

                cv::Mat result;
                cv::matchTemplate(screenshot, blacklistedImage, result, cv::TM_CCOEFF_NORMED);

                cv::threshold(result, result, confidence, 1.0, cv::THRESH_TOZERO);
                cv::normalize(result, result, 0, 255, cv::NORM_MINMAX, CV_8U);

                cv::Point min_loc, max_loc;
                double min_val, max_val;
                cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);

                if (max_val >= confidence) {
                    INPUT input;
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = keyToPress;
                    input.ki.dwFlags = 0;
                    input.ki.time = 0;
                    input.ki.dwExtraInfo = 0;
                    SendInput(1, &input, sizeof(INPUT));

                    std::string imageName = imageFile; // 获取图像文件路径
                    WriteToOutputTextBox("识别区中存在黑名单图像: " + imageName);
                    WriteToOutputTextBox("匹配结果置信度: " + std::to_string(max_val));
                    WriteToOutputTextBox("匹配位置: (x: " + std::to_string(max_loc.x) + ", y: " + std::to_string(max_loc.y) + ")");

                    // 找到一个图像后退出对blacklist的遍历
                    Sleep(sleepValue / 10);
                    break;
                }
            } else {
                WriteToOutputTextBox("无法读取图像文件: " + imageFile);
            }
            Sleep(sleepValue); // 使用从配置中读取的 sleep 值
        }

    }

    ReleaseDC(NULL, hScreenDC);
    DeleteDC(hMemoryDC);
    DeleteObject(hBitmap);
    // 初始化线程的 COM 环境
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}

constexpr auto MAX_LOADSTRING = 20;

struct WebData {
    std::string title;
    std::string url;
};

std::vector<WebData> webData;

void LoadWebDataFromJson() {
    try {
        std::ifstream file("./conf/url.json");
        if (file.is_open()) {
            nlohmann::json jsonData;
            file >> jsonData;
            file.close();

            for (const auto& item : jsonData["web"]) {
                WebData data;
                data.title = item["title"];
                data.url = item["url"];
                webData.push_back(data);
            }
        } else {
            throw std::runtime_error("Failed to open file.");
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception occurred: " << e.what() << std::endl;
        // Perform default value dump here
        WebData defaultData;
        defaultData.title = "Baidu-YiYan";
        defaultData.url = "https://yiyan.baidu.com/";
        webData.push_back(defaultData);
    }
}

void AddMoreMenuItems(int index ) {
    HMENU hMoreMenu = CreateMenu();
    for (const auto& data : webData) {
        AppendMenu(hMoreMenu, MF_STRING, index, data.title.c_str());
        index++;
    }
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hMoreMenu, "---- 更多 ----");
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // 创建菜单
            hMenu = CreateMenu();
            HMENU hFileMenu = CreateMenu();
            // 添加"暂停"和"恢复"
            AppendMenu(hMenu, MF_STRING, 2, "==暂停识别");

            AppendMenu(hMenu, MF_STRING, 11, "打开抖音 D →");
            AppendMenu(hMenu, MF_STRING, 12, "Youtube Y →");
            AppendMenu(hMenu, MF_STRING, 13, "西瓜视频 X →");
            AppendMenu(hMenu, MF_STRING, 14, "哔哩哔哩 B →");
            AppendMenu(hFileMenu, MF_STRING, 1, "扩展窗口++");
            AppendMenu(hFileMenu, MF_STRING, 3, "置顶窗口 ↑↑");
            AppendMenu(hFileMenu, MF_STRING, 6, "识别区快照");
            AppendMenu(hFileMenu, MF_STRING, 7, "程序说明");
            AppendMenu(hFileMenu, MF_STRING, 4, "重载识别区参数config.json");
            AppendMenu(hFileMenu, MF_STRING, 5, "恢复默认值到config.json");
            AppendMenu(hFileMenu, MF_STRING, 8, "任性投喂...（支付宝）");
            AppendMenu(hFileMenu, MF_STRING, 9, "退出");

            AppendMenu(hMenu, MF_STRING, 15, "WARP+");
            AppendMenu(hMenu, MF_STRING, 16, "WARP-");
            LoadWebDataFromJson();
            AddMoreMenuItems(17);
            AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, "设置");

            AppendMenu(hMenu, MF_STRING, 10, "打赏(1元)");
            // 将菜单与窗口关联
            SetMenu(hwnd, hMenu);
            // 创建输出文本框
            outputTextBox = CreateWindowW(
                    L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                    1, 1, 450, 200,
                    hwnd, NULL, NULL, NULL
            );
            // 设置初始状态为非置顶
            isWindowTopmost = false;
            // 设置初始状态为恢复
            isPaused = false;
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case 1: // 切换窗口样式
                    ToggleWindowStyle(hwnd, normalRect, dwStyle, isMinimized, 1);
                    break;
                case 2: // 暂停识别
                    ToggleRecognition(hwnd, isPaused, 2);
                    break;
                case 3: // 置顶窗口
                    ToggleWindowTopmost(hwnd, isWindowTopmost, 3);
                    break;
                case 4: // 重新加载
                    OnReloadMenuClicked();
                    break;
                case 5: // restore
                    OnRestoreDefaultConfigMenuClicked();
                    break;
                case 6: // dump识别区图像
                    OnDumpRecognitionAreaImageMenuClicked();
                    break;
                case 7: // 程序说明
                    SetWindowTextA(outputTextBox, "");
                    DisplayInstructions();
                    break;
                case 8: // 打赏
                    if (isWindowTopmost) {
                        ToggleWindowTopmost(hwnd, isWindowTopmost, 3);
                    }
                    GenerateQRCode(url, "./images/alipay.jpg");
                    break;
                case 9: // 退出
                    SaveWindowPosition(hwnd);  // 保存窗口位置信息
                    OnExitMenuClicked();
                    break;
                case 10: // 1元
                    // 设置窗口为非置顶
                    if (isWindowTopmost) {
                        ToggleWindowTopmost(hwnd, isWindowTopmost, 3);
                    }
                    GenerateQRCode(wx1cny, "./images/weixin.jpg");
                    break;
                case 11: // 打开抖音
                {
                    isPaused = true;
                    ToggleRecognition(hwnd, isPaused, 2);
                    std::string url = "https://www.douyin.com";
                    OpenURL(url);
                    break;
                }
                case 12: // 打开youtube
                {
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    std::string url = "https://www.youtube.com";
                    OpenURL(url);
                    break;
                }
                case 13: // 打开西瓜
                {
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    std::string url = "https://www.ixigua.com/my/attention/new";
                    OpenURL(url);
                    break;
                }
                case 14: // 打开哔哩哔哩
                {
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    std::string url = "https://t.bilibili.com/?spm_id_from=333.1007.0.0";
                    OpenURL(url);
                    break;
                }
                case 15: // 链接Warp
                {
                    std::string url = "scripts\\warpstart.bat";
                    OpenURL(url);
                    break;
                }
                case 16: // 停止Warp
                {
                    std::string url = "scripts\\warpstop.bat";
                    OpenURL(url);
                    break;
                }
                default: // 更多菜单项
                    int moreMenuFirstIndex =17;
                    if (LOWORD(wParam) >= webData.size() + 1) {
                        int index = LOWORD(wParam) - moreMenuFirstIndex ;
                        if (index >= 0 && index < webData.size()) {
                            const std::string& url = webData[index].url;
                            OpenURL(url);
                        } else {
                            WriteToOutputTextBox("更多菜单项打开失败，编辑conf/url.json修改。Invalid index: " + std::to_string(index));
                            WriteToOutputTextBox("webData.size() = " + std::to_string(webData.size()));
                            WriteToOutputTextBox("LOWORD(wParam) = " + std::to_string(LOWORD(wParam)));
                        }
                    }
                    break;

            }
            break;
        case WM_CLOSE:
            SaveWindowPosition(hwnd);  // 保存窗口位置信息
            stopCapture = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            switch (wParam) {
                case '=':
                    OpenURL("scripts\\warpstart.bat");
                    break;
                case '-':
                    OpenURL("scripts\\warpstop.bat");
                    break;
                case 'D':
                    isPaused = true;
                    ToggleRecognition(hwnd, isPaused, 2);
                    OpenURL("https://www.douyin.com");
                    break;
                case 'Y':
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    OpenURL("https://www.youtube.com");
                    break;
                case 'X':
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    OpenURL("https://www.ixigua.com/my/attention/new");
                    break;
                case 'B':
                    isPaused = false;
                    ToggleRecognition(hwnd, isPaused, 2);
                    OpenURL("https://t.bilibili.com/?spm_id_from=333.1007.0.0");
                    break;
                case VK_ESCAPE:
                    if (!programPaused && !isPaused) {
                        ToggleRecognition(hwnd, isPaused, 2);
                    }
                    else if (programPaused && isPaused) {
                        ToggleRecognition(hwnd, isPaused, 2);
                    }
                    break;
            }
            break;

    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 创建窗口类
    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = windowClassName;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
//    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;

    // 注册窗口类
    if (!RegisterClassExW(&wc))
    {
        return 1;
    }

    // 获取屏幕尺寸
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // 计算窗口位置
    int windowWidth = 465;
    int windowHeight = 280;
    int windowX = (screenWidth - windowWidth) / 100;  // 将窗口位置设置为屏幕最左边的百分之一处
    int windowY = (screenHeight - windowHeight) / 2;
    LoadWindowPosition(hwnd, windowX, windowY);
    // 创建窗口
    HWND hwnd = CreateWindowExW(
            0,
            windowClassName,
            windowTitle,
            WS_OVERLAPPEDWINDOW,
            windowX, windowY, windowWidth, windowHeight,
            NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 1;
    }
    // 显示窗口
    ShowWindow(hwnd, nCmdShow);
    ToggleRecognition(hwnd, isPaused, 2);
    ToggleWindowStyle(hwnd, normalRect, dwStyle, isMinimized, 1);
    ToggleWindowTopmost(hwnd, isWindowTopmost, 3);
	UpdateWindow(hwnd);

    CaptureScreenResolution(screenResolution);

    configFileName = GenerateConfigFileName(screenResolution);

    nlohmann::json config;

    try {
        if (!ReadConfigFile(configFileName, config)) {
            std::ofstream configFile(configFileName);
            if (configFile.is_open()) {
                configFile << defaultConfig;
                configFile.close();
                WriteToOutputTextBox("已创建默认配置文件。请重新启动程序");
                config = nlohmann::json::parse(defaultConfig);
            }
            else {
                WriteToOutputTextBox("无法创建默认配置文件");
            }
        }
    }
    catch (const std::exception& e) {
        WriteToOutputTextBox("加载配置文件时发生异常: " + std::string(e.what()));
        WriteToOutputTextBox("将使用默认配置");
//        config = nlohmann::json::parse(defaultConfig, nullptr, false);
        std::ofstream configFile(configFileName);
        if (configFile.is_open()) {
            configFile << config.dump(4);
            configFile.close();
            WriteToOutputTextBox("已保存配置到" + configFileName);
        }
        else {
            WriteToOutputTextBox("无法保存配置到" + configFileName);
        }
    }

    // 创建图像处理线程
    std::thread imageProcessingThread(ImageProcessingThread, screenResolution, config);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 等待图像处理线程结束
    stopCapture = true;
    imageProcessingThread.join();

    return 0;
}
