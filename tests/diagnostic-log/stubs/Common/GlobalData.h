#pragma once
#include <string>
struct AsciiString {
    std::string value;
    bool isEmpty() const { return value.empty(); }
    int getLength() const { return static_cast<int>(value.size()); }
    const char* str() const { return value.c_str(); }
};
struct GlobalData {
    AsciiString dir;
    const AsciiString& getPath_UserData() const { return dir; }
};
extern GlobalData* TheGlobalData;
