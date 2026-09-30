#include <doctest/doctest.h>

#include <algorithm>

#include "core/release_info.hpp"
#include "core/sha256.hpp"
#include "core/version.hpp"

using namespace edgepad;

TEST_CASE("version parsing and comparison") {
    const auto v = parseVersion("v0.1.12");
    REQUIRE(v);
    CHECK(*v == std::vector<int>{0, 1, 12});
    CHECK(compareVersions(*parseVersion("1.2"), *parseVersion("1.2.0")) == 0);
    CHECK(isNewerVersion("0.1.13", "0.1.12"));
    CHECK(isNewerVersion("v0.2.1", "0.1.99"));
    CHECK(isNewerVersion("0.1.10", "0.1.9"));
    CHECK_FALSE(isNewerVersion("0.1.12", "0.1.12"));
    CHECK_FALSE(isNewerVersion("0.1.11", "0.1.12"));
    CHECK_FALSE(isNewerVersion("latest", "0.1.0"));
    CHECK(parseVersion("1.4.0-beta") == std::vector<int>{1, 4, 0});
}

TEST_CASE("GitHub release JSON") {
    const std::string json = R"({
        "tag_name": "v0.1.7",
        "html_url": "https://github.com/Blake12609/ps5/releases/tag/v0.1.7",
        "body": "notes",
        "assets": [
            {"name": "EdgePad-windows-x64.exe", "size": 1234,
             "browser_download_url": "https://github.com/Blake12609/ps5/releases/download/v0.1.7/EdgePad-windows-x64.exe"},
            {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.invalid/sums"},
            {"name": 5}
        ]
    })";
    const auto info = parseReleaseJson(json);
    REQUIRE(info);
    CHECK(info->version == "0.1.7");
    CHECK(info->assets.size() == 2);
    const ReleaseAsset* exe = info->findAsset("EdgePad-windows-x64.exe");
    REQUIRE(exe);
    CHECK(exe->size == 1234);
    CHECK(info->findAsset(kChecksumAssetName));
    CHECK_FALSE(info->findAsset("nope"));

    std::string error;
    CHECK_FALSE(parseReleaseJson(R"({"message": "Not Found"})", &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parseReleaseJson("<html>", &error));
}

TEST_CASE("checksum file lookup") {
    const std::string sums =
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855  EdgePad-windows-x64.exe\r\n"
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *EdgePad-linux-x64\n"
        "short  broken-line\n";
    CHECK(findChecksum(sums, "EdgePad-windows-x64.exe") ==
          std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(findChecksum(sums, "EdgePad-linux-x64") ==
          std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_FALSE(findChecksum(sums, "broken-line"));
    CHECK_FALSE(findChecksum(sums, "EdgePad"));
    CHECK_FALSE(platformAssetName().empty());
}

TEST_CASE("SHA-256 test vectors") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 streamed;
    const std::string million(1000000, 'a');
    for (size_t i = 0; i < million.size(); i += 777) streamed.update(million.data() + i, std::min<size_t>(777, million.size() - i));
    CHECK(streamed.finishHex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
