#include "wifi_setup_view.h"

WifiSetupView::WifiSetupView()
    : _selectedIndex(0),
      _isScanning(false),
      _isInputtingPassword(false),
      _passwordLen(0),
      _statusMessage(""),
      _statusMessageUntilMs(0),
      _activeRepeatChar(0),
      _charPressStartTime(0),
      _lastCharRepeatTime(0),
      _backPressStartTime(0),
      _lastBackRepeatTime(0) {
    memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
}

void WifiSetupView::showStatus(const String& msg, uint32_t durationMs) {
    _statusMessage = msg;
    _statusMessageUntilMs = millis() + durationMs;
}

void WifiSetupView::reset() {
    _isScanning = false;
    _isInputtingPassword = false;
    _passwordLen = 0;
    _statusMessage = "";
    _statusMessageUntilMs = 0;
    _lastWord.clear();
    _activeRepeatChar = 0;
    _charPressStartTime = 0;
    _lastCharRepeatTime = 0;
    _backPressStartTime = 0;
    _lastBackRepeatTime = 0;
    memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
    _networks.clear();
    _networks.shrink_to_fit();
    _selectedIndex = 0;
}

void WifiSetupView::startScan() {
    _isScanning = true;
}

void WifiSetupView::performScan() {
    _networks = HalWifi::scanNetworks();
    _isScanning = false;
    _selectedIndex = 0;
}

String WifiSetupView::truncateUtf8(const String& str, size_t maxChars) {
    size_t charCount = 0;
    size_t byteIdx = 0;
    size_t len = str.length();
    
    while (byteIdx < len && charCount < maxChars) {
        unsigned char c = (unsigned char)str[byteIdx];
        size_t charBytes = 1;
        if ((c & 0x80) == 0) charBytes = 1;
        else if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;
        
        if (byteIdx + charBytes > len) break;
        byteIdx += charBytes;
        charCount++;
    }
    
    if (byteIdx < len) {
        return str.substring(0, byteIdx) + "..";
    }
    return str;
}

void WifiSetupView::draw(LGFX_Sprite* canvas) {
    if (!canvas) return;

    uint16_t width = canvas->width();
    uint16_t height = canvas->height();
    
    // Set font matching current language
    canvas->setFont(I18N::getFont());
    canvas->setTextSize(1);
    
    // Background
    canvas->fillRect(0, 0, width, height, canvas->color565(15, 20, 25));
    
    // Top Bar
    canvas->fillRect(0, 0, width, 25, canvas->color565(30, 60, 100));
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(I18N::get(TXT_WIFI_SETUP), 10, 5);
    
    if (_statusMessage.length() > 0 && millis() < _statusMessageUntilMs) {
        canvas->setTextColor(TFT_YELLOW);
        canvas->drawString(_statusMessage.c_str(), width - 95, 5);
    }
    
    canvas->setTextColor(TFT_WHITE);
    
    if (_isScanning) {
        canvas->drawString(I18N::get(TXT_SCANNING_NETWORKS), 20, 50);
        return; // Will be handled in main loop after rendering
    }
    
    if (_networks.empty() && !_isScanning) {
        _isInputtingPassword = false;
        canvas->drawString(I18N::get(TXT_NO_NETWORKS_FOUND), 20, 80);
        canvas->drawString(I18N::get(TXT_PRESS_R_RESCAN), 20, 100);
    } else {
        if (_isInputtingPassword && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) {
            canvas->drawString(I18N::get(TXT_CONNECT_TO), 20, 40);
            canvas->setTextColor(TFT_GREEN);
            String ssid = truncateUtf8(_networks[_selectedIndex].ssid, 16);
            canvas->drawString(ssid.c_str(), 20, 55);
            
            canvas->setTextColor(TFT_WHITE);
            canvas->drawString(I18N::get(TXT_PASSWORD), 20, 80);
            
            canvas->fillRect(20, 95, width - 40, 25, canvas->color565(50, 50, 50));
            canvas->drawRect(20, 95, width - 40, 25, TFT_WHITE);
            
            char displayStr[66];
            snprintf(displayStr, sizeof(displayStr), "%s_", _passwordBuffer);
            canvas->drawString(displayStr, 25, 100);
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            if (I18N::getLanguage() == LANG_ZH) {
                canvas->drawString("[Enter] 连接  [Fn+Del] 清空  [ESC] 取消", 5, height - 15);
            } else {
                canvas->drawString("[Enter] Conn  [Fn+Del] Clear  [ESC] Cancel", 5, height - 15);
            }
        } else {
            canvas->drawString(I18N::get(TXT_SELECT_NETWORK), 10, 30);
            
            if (_selectedIndex < 0) _selectedIndex = 0;
            if (!_networks.empty() && _selectedIndex >= (int)_networks.size()) {
                _selectedIndex = (int)_networks.size() - 1;
            }
            
            int yPos = 45;
            int itemsPerPage = 4;
            int startIndex = (_selectedIndex / itemsPerPage) * itemsPerPage;
            if (startIndex < 0) startIndex = 0;
            
            for (int i = 0; i < itemsPerPage && (startIndex + i) < (int)_networks.size(); i++) {
                int index = startIndex + i;
                bool isSaved = HalWifi::hasCredential(_networks[index].ssid);
                
                if (index == _selectedIndex) {
                    canvas->fillRect(5, yPos - 2, width - 10, 18, canvas->color565(50, 100, 150));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(isSaved ? canvas->color565(120, 220, 120) : TFT_LIGHTGRAY);
                }
                
                String prefix = isSaved ? "* " : "  ";
                String ssidStr = prefix + truncateUtf8(_networks[index].ssid, 12);
                canvas->drawString(ssidStr.c_str(), 8, yPos);
                
                char rssiStr[16];
                snprintf(rssiStr, sizeof(rssiStr), "%ddBm", (int)_networks[index].rssi);
                canvas->drawString(rssiStr, width - 50, yPos);
                
                yPos += 20;
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            bool currentSelectedSaved = (!_networks.empty() && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) ?
                                        HalWifi::hasCredential(_networks[_selectedIndex].ssid) : false;
            if (currentSelectedSaved) {
                if (I18N::getLanguage() == LANG_ZH) {
                    canvas->drawString("[^/v] 选择 [Enter] 连接 [F] 忘记 [ESC] 退出", 5, height - 15);
                } else {
                    canvas->drawString("[^/v] Sel [Enter] Conn [F] Forget [ESC] Exit", 5, height - 15);
                }
            } else {
                canvas->drawString(I18N::get(TXT_WIFI_HELP_SEL), 5, height - 15);
            }
        }
    }
}

bool WifiSetupView::handleInput(bool justEsc, bool justTick, bool justBack, bool justEnter, 
                               bool justR, bool justSemi, bool justDot, 
                               const Keyboard_Class::KeysState& keysState,
                               WifiConnectRequest& outConnectReq, bool& outExit) {
    outConnectReq.triggered = false;
    outExit = false;

    if (justEsc || justTick) {
        if (_isInputtingPassword) {
            _isInputtingPassword = false;
            return false;
        } else {
            outExit = true;
            return true;
        }
    } else if (!_isInputtingPassword && justBack) {
        outExit = true;
        return true;
    } else if (_isInputtingPassword) {
        uint32_t now = millis();
        if (justEnter) {
            if (!_networks.empty() && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) {
                outConnectReq.triggered = true;
                outConnectReq.ssid = _networks[_selectedIndex].ssid;
                outConnectReq.pass = String(_passwordBuffer);
                outConnectReq.channel = _networks[_selectedIndex].channel;
                memcpy(outConnectReq.bssid, _networks[_selectedIndex].bssid, 6);
                outConnectReq.hasBssid = true;
                reset();
                return true;
            }
        } else if (justBack) {
            _backPressStartTime = now;
            _lastBackRepeatTime = now;
            // Fn+Back 或 Ctrl+Back 或 Opt/Alt+Back -> 一键清空密码
            if (keysState.fn || keysState.ctrl || keysState.opt) {
                memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
                _passwordLen = 0;
            } else if (_passwordLen > 0) {
                _passwordBuffer[--_passwordLen] = '\0';
            }
        } else if (keysState.del && _passwordLen > 0) {
            // 退格键长按连续删除：按住超过 350ms 后，每 70ms 删一个字符
            if (_backPressStartTime > 0 && (now - _backPressStartTime >= 350) && (now - _lastBackRepeatTime >= 70)) {
                _lastBackRepeatTime = now;
                _passwordBuffer[--_passwordLen] = '\0';
            }
        } else {
            if (!keysState.del) {
                _backPressStartTime = 0;
            }

            // 字符输入去抖与平滑连发：
            // 单次点按只在按键边沿触发（首次按下立即录入），彻底杜绝单次点按导致的连击
            // 持续按住超过 400ms 后才进入连发，每 100ms 重复一个字符
            char currentChar = 0;
            for (auto c : keysState.word) {
                if (c >= ' ' && c <= '~') {
                    currentChar = c;
                    break;
                }
            }

            if (currentChar != 0) {
                bool wasPressed = false;
                for (auto lc : _lastWord) {
                    if (lc == currentChar) {
                        wasPressed = true;
                        break;
                    }
                }

                if (!wasPressed) {
                    // 新按下的字符：立即边沿输入
                    if (_passwordLen < 63) {
                        _passwordBuffer[_passwordLen++] = currentChar;
                        _passwordBuffer[_passwordLen] = '\0';
                    }
                    _activeRepeatChar = currentChar;
                    _charPressStartTime = now;
                    _lastCharRepeatTime = now;
                } else if (currentChar == _activeRepeatChar) {
                    // 持续按住：达到长按门限才连发
                    if (now - _charPressStartTime >= 400 && now - _lastCharRepeatTime >= 100) {
                        if (_passwordLen < 63) {
                            _passwordBuffer[_passwordLen++] = currentChar;
                            _passwordBuffer[_passwordLen] = '\0';
                        }
                        _lastCharRepeatTime = now;
                    }
                }
            } else {
                _activeRepeatChar = 0;
                _charPressStartTime = 0;
            }
        }
    } else {
        bool justF = false;
        bool justD = false;
        for (auto c : keysState.word) {
            if (c == 'f' || c == 'F') justF = true;
            if (c == 'd' || c == 'D') justD = true;
        }

        if (justR) {
            _isScanning = true;
        } else if ((justF || justD) && !_networks.empty() && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) {
            if (HalWifi::hasCredential(_networks[_selectedIndex].ssid)) {
                String forgottenSsid = _networks[_selectedIndex].ssid;
                HalWifi::forgetNetwork(forgottenSsid);
                showStatus((I18N::getLanguage() == LANG_ZH) ? "已忘记网络" : "Network forgotten");
            }
        } else if (justEnter) {
            if (!_networks.empty() && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) {
                _isInputtingPassword = true;
                memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
                _passwordLen = 0;
                _lastWord = keysState.word;
                _activeRepeatChar = 0;
                _charPressStartTime = 0;
                _backPressStartTime = 0;
                String savedPass = "";
                if (HalWifi::getCredential(_networks[_selectedIndex].ssid, savedPass)) {
                    strncpy(_passwordBuffer, savedPass.c_str(), sizeof(_passwordBuffer) - 1);
                    _passwordLen = strlen(_passwordBuffer);
                }
            }
        } else if (justSemi) { // UP arrow

            if (!_networks.empty()) {
                if (_selectedIndex > 0) _selectedIndex--;
                else _selectedIndex = _networks.size() - 1;
            }
        } else if (justDot) { // DOWN arrow
            if (!_networks.empty()) {
                _selectedIndex = (_selectedIndex + 1) % _networks.size();
            }
        }
    }
    _lastWord = keysState.word;
    return false;
}

