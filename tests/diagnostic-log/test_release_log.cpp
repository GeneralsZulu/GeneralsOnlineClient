#include "Common/ReleaseLog.h"
#include "Common/GlobalData.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

GlobalData* TheGlobalData = nullptr;
static std::string read(const std::string& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
int main(int argc, char** argv)
{
    assert(argc == 2);
    GlobalData data;
    data.dir.value = std::string(argv[1]) + "/";
    std::filesystem::create_directories(data.dir.value);
    const std::string release = data.dir.value + "ReleaseLog.txt";
    const std::string network = data.dir.value + "GeneralsOnline.log";
    { std::ofstream f(release); f << "previous release"; }
    { std::ofstream f(network); f << "previous network"; }
    // Startup before the engine user directory is known must remain harmless.
    ReleaseLog("too early");
    TheGlobalData = &data;
    ReleaseLog("first frame=%u", 123u);
    DiagnosticLogSubmit(1, network.c_str(), (network + ".prev").c_str(), "network packet\n");
    assert(DiagnosticLogFlush(5000));
    assert(read(release + ".missing").empty());
    assert(read(data.dir.value + "ReleaseLog.prev.txt") == "previous release");
    assert(read(network + ".prev") == "previous network");
    assert(read(release).find("first frame=123") != std::string::npos);
    assert(read(network) == "network packet\n");
    ReleaseLog("second");
    for (unsigned i = 0; i < 20; ++i) ReleaseLogRateLimited(2, "slot=1 frame=%u", i);
    ReleaseLogRecordFrameCRC(123, 0xabc);
    ReleaseLogRecordSubsystemCRC(123, "objects", 0xdef);
    ReleaseLogDumpCRCHistory();
    assert(DiagnosticLogFlush(5000));
    const auto contents = read(release);
    assert(contents.find("first frame=123") < contents.find("second"));
    assert(contents.find("suppressed=19") != std::string::npos);
    assert(contents.find("objects CRC=00000DEF") != std::string::npos);
    assert(read(data.dir.value + "ReleaseLog.prev.txt") == "previous release");
    // Cached paths remain usable after engine teardown; worker owns only copies.
    TheGlobalData = nullptr;
    ReleaseLog("after teardown");
    assert(DiagnosticLogFlush(5000));
    assert(read(release).find("after teardown") != std::string::npos);
    std::cout << "release/network file integration tests passed\n";
}
