// 全局变量
HWND hwnd;
HMENU hMenu;
HWND outputTextBox;
std::ostringstream outputBuffer;
nlohmann::json screenResolution;
bool isPaused = false;
bool programPaused = false;
bool stopCapture = false;
bool isWindowTopmost = false;
bool isMinimized = false;

std::string url = "https://qr.alipay.com/fkx11581faijzccf3kv0s60";
std::string wx1cny = "wxp://f2f1WispATMSNzzltLXVBk5ol8fR5zYbvCZq-0jjBX2oJvxllSJLqYbsyMGEJS_oJwDe";
const std::string defaultConfig = R"({
  "x": 280,
  "y": 810,
  "w": 1400,
  "h": 130,
  "__comment__": "VK_RIGHT=0x27=39;VK_ESCAPE=0x1B=28; https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes",
  "keyToPress": 40,
  "resolution" : 1920.0,
  "blacklist": [
    {
      "image": "./images/bad.png",
      "confidence": 0.6
    },
    {
      "image": "./images/bkouqiang.png",
      "confidence": 0.7
	},
    {
       "image": "./images/bgouwu.png",
       "confidence": 0.8
    }
  ],
  "sleepValue" : 300
}
)";
