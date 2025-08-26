// listfont.cpp : Lists all font families and their fonts with raw weight data
//

#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <Windows.h>
#include <dwrite.h>

#pragma comment(lib, "dwrite.lib")

// Simple UTF-16 to UTF-8 conversion
std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

// Safe console output that handles Unicode better
void ConsoleOutput(const std::wstring& text)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteConsoleW(hOut, text.c_str(), (DWORD)text.length(), &written, nullptr);
    }
}

// Get all localized strings from IDWriteLocalizedStrings
std::vector<std::wstring> GetAllLocalizedStrings(IDWriteLocalizedStrings* strings)
{
    std::vector<std::wstring> result;
    if (!strings) return result;
    
    UINT32 count = strings->GetCount();
    for (UINT32 i = 0; i < count; ++i)
    {
        UINT32 length = 0;
        if (SUCCEEDED(strings->GetStringLength(i, &length)))
        {
            std::wstring text(length, L'\0');
            if (SUCCEEDED(strings->GetString(i, &text[0], length + 1)))
            {
                result.push_back(text);
            }
        }
    }
    return result;
}

// Get the primary name (first available, preferring English)
std::wstring GetPrimaryName(IDWriteLocalizedStrings* strings)
{
    if (!strings) return L"";
    
    // Try to get English name first
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(strings->FindLocaleName(L"en-us", &index, &exists)) && exists)
    {
        UINT32 length = 0;
        if (SUCCEEDED(strings->GetStringLength(index, &length)))
        {
            std::wstring text(length, L'\0');
            if (SUCCEEDED(strings->GetString(index, &text[0], length + 1)))
                return text;
        }
    }
    
    // Fallback to first available
    if (strings->GetCount() > 0)
    {
        UINT32 length = 0;
        if (SUCCEEDED(strings->GetStringLength(0, &length)))
        {
            std::wstring text(length, L'\0');
            if (SUCCEEDED(strings->GetString(0, &text[0], length + 1)))
                return text;
        }
    }
    
    return L"";
}

struct FontInfo
{
    std::wstring name;
    std::wstring postScriptName;  // Add PostScript name
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_STYLE style;
};

struct FontFamily
{
    std::wstring primaryName;
    std::wstring postScriptFamilyName;  // Add PostScript family name
    std::set<std::wstring> allNames;
    std::vector<FontInfo> fonts;
};

int main()
{
    // Setup console for UTF-8 and Unicode
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    // Enable Unicode console output
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    // Open log file
    std::ofstream logFile("font.log", std::ios::out | std::ios::trunc | std::ios::binary);
    if (!logFile.is_open())
    {
        ConsoleOutput(L"Error: Could not create font.log file!\n");
        return 1;
    }
    logFile << "\xEF\xBB\xBF"; // UTF-8 BOM
    
    ConsoleOutput(L"Font Family Enumerator\n");
    ConsoleOutput(L"======================\n");
    
    // Initialize DirectWrite
    IDWriteFactory* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), 
                                   reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(hr) || !factory)
    {
        ConsoleOutput(L"Error: Failed to create DirectWrite factory.\n");
        return 1;
    }
    
    // Get system font collection
    IDWriteFontCollection* collection = nullptr;
    hr = factory->GetSystemFontCollection(&collection);
    if (FAILED(hr) || !collection)
    {
        ConsoleOutput(L"Error: Failed to get system font collection.\n");
        factory->Release();
        return 1;
    }
    
    std::vector<FontFamily> fontFamilies;
    UINT32 familyCount = collection->GetFontFamilyCount();
    
    // Process each font family
    for (UINT32 i = 0; i < familyCount; ++i)
    {
        IDWriteFontFamily* family = nullptr;
        if (FAILED(collection->GetFontFamily(i, &family)) || !family)
            continue;
            
        FontFamily fontFamily;
        
        // Get family names
        IDWriteLocalizedStrings* familyNames = nullptr;
        if (SUCCEEDED(family->GetFamilyNames(&familyNames)) && familyNames)
        {
            fontFamily.primaryName = GetPrimaryName(familyNames);
            auto allNames = GetAllLocalizedStrings(familyNames);
            for (const auto& name : allNames)
            {
                fontFamily.allNames.insert(name);
            }
            familyNames->Release();
        }
        
        // Get PostScript family name from a representative font
        IDWriteFont* repFont = nullptr;
        if (SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &repFont)) && repFont)
        {
            BOOL exists = FALSE;
            IDWriteLocalizedStrings* psNames = nullptr;
            if (SUCCEEDED(repFont->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &psNames, &exists)) && exists && psNames)
            {
                std::wstring psName = GetPrimaryName(psNames);
                // Extract family part (before the hyphen)
                size_t pos = psName.find(L'-');
                if (pos != std::wstring::npos)
                {
                    fontFamily.postScriptFamilyName = psName.substr(0, pos);
                }
                else
                {
                    fontFamily.postScriptFamilyName = psName;
                }
                psNames->Release();
            }
            repFont->Release();
        }
        
        // Get fonts in this family
        UINT32 fontCount = family->GetFontCount();
        for (UINT32 j = 0; j < fontCount; ++j)
        {
            IDWriteFont* font = nullptr;
            if (FAILED(family->GetFont(j, &font)) || !font)
                continue;
                
            FontInfo fontInfo;
            fontInfo.weight = font->GetWeight();
            fontInfo.stretch = font->GetStretch();
            fontInfo.style = font->GetStyle();
            
            // Get font face name
            BOOL exists = FALSE;
            IDWriteLocalizedStrings* faceNames = nullptr;
            if (SUCCEEDED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_FULL_NAME, &faceNames, &exists)) 
                && exists && faceNames)
            {
                fontInfo.name = GetPrimaryName(faceNames);
                faceNames->Release();
            }
            
            // Get PostScript name
            IDWriteLocalizedStrings* psNames = nullptr;
            if (SUCCEEDED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME, &psNames, &exists)) 
                && exists && psNames)
            {
                fontInfo.postScriptName = GetPrimaryName(psNames);
                psNames->Release();
            }
            
            // Fallback: use subfamily name
            if (fontInfo.name.empty())
            {
                IDWriteLocalizedStrings* subfamilyNames = nullptr;
                if (SUCCEEDED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES, &subfamilyNames, &exists)) 
                    && exists && subfamilyNames)
                {
                    std::wstring subfamily = GetPrimaryName(subfamilyNames);
                    fontInfo.name = fontFamily.primaryName + L" " + subfamily;
                    subfamilyNames->Release();
                }
                else
                {
                    fontInfo.name = fontFamily.primaryName + L" (Unknown Style)";
                }
            }
            
            fontFamily.fonts.push_back(fontInfo);
            font->Release();
        }
        
        if (!fontFamily.primaryName.empty())
        {
            fontFamilies.push_back(fontFamily);
        }
        
        family->Release();
    }
    
    // Output results
    ConsoleOutput(L"Found " + std::to_wstring(fontFamilies.size()) + L" font families\n\n");
    logFile << "Found " << fontFamilies.size() << " font families\n\n";
    
    for (const auto& family : fontFamilies)
    {
        // Output family name and aliases
        std::wstring familyLine = L"FAMILY: " + family.primaryName;
        if (!family.postScriptFamilyName.empty() && family.postScriptFamilyName != family.primaryName)
        {
            familyLine += L" [" + family.postScriptFamilyName + L"]";
        }
        familyLine += L"\n";
        ConsoleOutput(familyLine);
        
        std::string logFamilyLine = "FAMILY: " + WideToUtf8(family.primaryName);
        if (!family.postScriptFamilyName.empty() && family.postScriptFamilyName != family.primaryName)
        {
            logFamilyLine += " [" + WideToUtf8(family.postScriptFamilyName) + "]";
        }
        logFamilyLine += "\n";
        logFile << logFamilyLine;
        
        if (family.allNames.size() > 1)
        {
            std::wstring aliasLine = L"  Aliases: ";
            logFile << "  Aliases: ";
            bool first = true;
            for (const auto& name : family.allNames)
            {
                if (name != family.primaryName)
                {
                    if (!first) 
                    {
                        aliasLine += L", ";
                        logFile << ", ";
                    }
                    aliasLine += name;
                    logFile << WideToUtf8(name);
                    first = false;
                }
            }
            aliasLine += L"\n";
            ConsoleOutput(aliasLine);
            logFile << "\n";
        }
        
        // Output fonts in family
        for (const auto& font : family.fonts)
        {
            // Build output line with PostScript name if different
            std::wstring fontLine = L"  " + font.name;
            if (!font.postScriptName.empty() && font.postScriptName != font.name)
            {
                fontLine += L" [" + font.postScriptName + L"]";
            }
            fontLine += L" (Weight: " + std::to_wstring(font.weight) + 
                       L", Stretch: " + std::to_wstring(font.stretch) + 
                       L", Style: " + std::to_wstring(font.style) + L")\n";
            
            ConsoleOutput(fontLine);
                      
            // Same for log file
            std::string logLine = "  " + WideToUtf8(font.name);
            if (!font.postScriptName.empty() && font.postScriptName != font.name)
            {
                logLine += " [" + WideToUtf8(font.postScriptName) + "]";
            }
            logLine += " (Weight: " + std::to_string(font.weight) + 
                      ", Stretch: " + std::to_string(font.stretch) + 
                      ", Style: " + std::to_string(font.style) + ")\n";
            logFile << logLine;
        }
        
        ConsoleOutput(L"\n");
        logFile << "\n";
    }
    
    logFile.close();
    collection->Release();
    factory->Release();
    
    ConsoleOutput(L"Results saved to font.log\n");
    ConsoleOutput(L"Press Enter to exit...\n");
    
    // Wait for user input
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD inputRecord;
    DWORD eventsRead;
    do {
        ReadConsoleInput(hIn, &inputRecord, 1, &eventsRead);
    } while (inputRecord.EventType != KEY_EVENT || !inputRecord.Event.KeyEvent.bKeyDown);
    
    return 0;
}