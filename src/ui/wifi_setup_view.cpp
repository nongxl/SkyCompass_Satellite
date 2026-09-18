#include "wifi_setup_view.h"

WifiSetupView::WifiSetupView()
    : _selectedIndex(0),
      _isScanning(false),
      _isInputtingPassword(false),
      _passwordLen(0) {
    memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
}

void WifiSetupView::reset() {
    _isScanning = false;
    _isInputtingPassword = false;
    _passwordLen = 0;
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
            canvas->drawString(I18N::get(TXT_WIFI_HELP_CONN), 10, height - 15);
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
                if (index == _selectedIndex) {
                    canvas->fillRect(5, yPos - 2, width - 10, 18, canvas->color565(50, 100, 150));
                    canvas->setTextColor(TFT_WHITE);
                } else {
                    canvas->setTextColor(TFT_LIGHTGRAY);
                }
                
                String ssidStr = truncateUtf8(_networks[index].ssid, 14);
                canvas->drawString(ssidStr.c_str(), 10, yPos);
                
                char rssiStr[16];
                snprintf(rssiStr, sizeof(rssiStr), "%ddBm", _networks[index].rssi);
                canvas->drawString(rssiStr, width - 50, yPos);
                
                yPos += 20;
            }
            
            canvas->setTextColor(TFT_LIGHTGRAY);
            canvas->drawString(I18N::get(TXT_WIFI_HELP_SEL), 5, height - 15);
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
        if (justEnter) {
            if (!_networks.empty() && _selectedIndex >= 0 && _selectedIndex < (int)_networks.size()) {
                outConnectReq.triggered = true;
                outConnectReq.ssid = _networks[_selectedIndex].ssid;
                outConnectReq.pass = String(_passwordBuffer);
                reset();
                return true;
            }
        } else if (justBack) {
            if (_passwordLen > 0) {
                _passwordBuffer[--_passwordLen] = '\0';
            }
        } else {
            for (auto c : keysState.word) {
                if (_passwordLen < 63 && c >= ' ' && c <= '~') {
                    _passwordBuffer[_passwordLen++] = c;
                    _passwordBuffer[_passwordLen] = '\0';
                }
            }
        }
    } else {
        if (justR) {
            _isScanning = true;
        } else if (justEnter) {
            if (!_networks.empty()) {
                _isInputtingPassword = true;
                memset(_passwordBuffer, 0, sizeof(_passwordBuffer));
                _passwordLen = 0;
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
    return false;
}
