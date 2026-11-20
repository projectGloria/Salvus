#include "recovery/recovery_components.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace salvus::recovery {

namespace {

std::string BytesToLowerHex(std::span<const std::uint8_t> bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const std::uint8_t b : bytes) {
        oss << std::setw(2) << static_cast<unsigned int>(b);
    }
    return oss.str();
}

} // namespace

common::Result<std::string> IntegrityChecker::ComputeSha256(const std::wstring& filePath) const {
#ifndef _WIN32
    (void)filePath;
    return common::Result<std::string>::Fail(
        common::StatusCode::NotImplemented, "Windows CNG SHA-256 is only supported on Windows.");
#else
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return common::Result<std::string>::Fail(
            common::StatusCode::IoError, "Failed to open file for SHA-256 verification.");
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return common::Result<std::string>::Fail(
            common::StatusCode::InternalError, "BCryptOpenAlgorithmProvider(SHA256) failed.");
    }

    DWORD hashObjSize = 0;
    DWORD cbData = 0;
    if (::BCryptGetProperty(
            hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&hashObjSize), sizeof(DWORD), &cbData, 0) != 0) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        return common::Result<std::string>::Fail(
            common::StatusCode::InternalError, "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed.");
    }

    std::vector<std::uint8_t> hashObj(hashObjSize, 0U);
    if (::BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0) != 0) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        return common::Result<std::string>::Fail(
            common::StatusCode::InternalError, "BCryptCreateHash failed.");
    }

    constexpr std::size_t kChunk = 1024U * 1024U; // Bounded 1 MiB read buffer
    std::vector<std::uint8_t> buf(kChunk, 0U);
    while (in.good()) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            if (::BCryptHashData(hHash, buf.data(), static_cast<ULONG>(got), 0) != 0) {
                ::BCryptDestroyHash(hHash);
                ::BCryptCloseAlgorithmProvider(hAlg, 0);
                return common::Result<std::string>::Fail(
                    common::StatusCode::InternalError, "BCryptHashData failed.");
            }
        }
    }

    std::array<std::uint8_t, 32> digest{};
    const NTSTATUS finStatus = ::BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    ::BCryptDestroyHash(hHash);
    ::BCryptCloseAlgorithmProvider(hAlg, 0);

    if (finStatus != 0) {
        return common::Result<std::string>::Fail(
            common::StatusCode::InternalError, "BCryptFinishHash failed.");
    }

    return common::Result<std::string>::Ok(BytesToLowerHex(digest));
#endif
}

} // namespace salvus::recovery
