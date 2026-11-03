/*
 * TestMain.cxx
 *
 * Copyright (C) 2026, Charles Chiou
 */

#include <cstdlib>
#include <filesystem>
#include <CppUTest/CommandLineTestRunner.h>

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    std::string testHome = "test_data/test_home_main";
    if (fs::exists(testHome)) {
        fs::remove_all(testHome);
    }
    fs::create_directories(testHome);
    std::string absHome = fs::absolute(testHome).string();
    setenv("HOME", absHome.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (absHome + "/.config").c_str(), 1);

    int result = CommandLineTestRunner::RunAllTests(argc, argv);

    if (fs::exists(testHome)) {
        fs::remove_all(testHome);
    }

    return result;
}

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
