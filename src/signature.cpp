/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "signature.hpp"

#include "fwupderr.hpp"

#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// RAII support for openssl functions.
using BIO_MEM_Ptr = std::unique_ptr<BIO, decltype(&::BIO_free)>;
using EVP_PKEY_Ptr = std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)>;
using EVP_MD_CTX_Ptr =
    std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

/**
 * @brief RAII wrapper for memory mapped file.
 */
struct MappedMem
{
    MappedMem() = delete;
    MappedMem(const MappedMem&) = delete;
    MappedMem& operator=(const MappedMem&) = delete;
    MappedMem(MappedMem&&) = default;
    MappedMem& operator=(MappedMem&&) = default;

    MappedMem(void* addr, size_t length) : addr(addr), length(length)
    {
    }
    ~MappedMem()
    {
        munmap(addr, length);
    }

    void* get() const
    {
        return addr;
    }
    size_t size() const
    {
        return length;
    }
    operator bool() const
    {
        return addr != nullptr;
    }

    /**
     * @brief Map specified file into memory
     *
     * @param filePath - path to file
     *
     * @return MappedMem object with file content.
     */
    static MappedMem open(const std::string& filePath)
    {
        int fd = ::open(filePath.c_str(), O_RDONLY);
        if (fd == -1)
        {
            throw FwupdateError("open %s failed, error=%d: %s",
                                filePath.c_str(), errno, strerror(errno));
        }

        auto size = fs::file_size(filePath);
        auto addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        auto mmap_errno = errno;
        close(fd);

        if (addr == MAP_FAILED)
        {
            throw FwupdateError("mmap for %s failed, error=%d: %s",
                                filePath.c_str(), mmap_errno,
                                strerror(mmap_errno));
        }

        return MappedMem(addr, size);
    }

  private:
    void* addr = nullptr;
    size_t length = 0;
};

/**
 * @brief Create RSA object from the public key.
 *
 * @param publicKey - path to public key file
 *
 * @return RSA Object
 */
RSA* createPublicRSA(const std::string& publicKey)
{
    auto data = MappedMem::open(publicKey);
    BIO_MEM_Ptr keyBio(BIO_new_mem_buf(data.get(), data.size()), &::BIO_free);
    if (keyBio.get() == nullptr)
    {
        throw FwupdateError("Failed to create new BIO Memory buffer.");
    }

    // NOTE: Return value should be freed with RSA_free or
    //       as part of EVP_KEY sturcture.
    return PEM_read_bio_RSA_PUBKEY(keyBio.get(), nullptr, nullptr, nullptr);
}

bool verify_file(const std::string& keyFile, const std::string& hashFunc,
                 const std::string& filePath)
{
    fs::path fileSig(filePath + SIGNATURE_FILE_EXT);

    // Check existence of the files in the system.
    if (!fs::exists(filePath) || !fs::exists(fileSig))
    {
        throw FwupdateError("Failed to find the Data or signature file.");
    }

    // Create RSA
    auto publicRSA = createPublicRSA(keyFile);
    if (publicRSA == nullptr)
    {
        throw FwupdateError("Failed to create RSA.");
    }

    // Assign key to RSA.
    EVP_PKEY_Ptr pKeyPtr(EVP_PKEY_new(), ::EVP_PKEY_free);
    EVP_PKEY_assign_RSA(pKeyPtr.get(), publicRSA);
    // NOTE: publicRSA will be freed as part of pKeyPtr

    // Initializes a digest context.
    EVP_MD_CTX_Ptr rsaVerifyCtx(EVP_MD_CTX_new(), ::EVP_MD_CTX_free);

    // Adds all digest algorithms to the internal table
    OpenSSL_add_all_digests();

    // Create Hash structure.
    auto hashStruct = EVP_get_digestbyname(hashFunc.c_str());
    if (!hashStruct)
    {
        throw FwupdateError("EVP_get_digestbyname: Unknown message digest.");
    }

    auto result = EVP_DigestVerifyInit(rsaVerifyCtx.get(), nullptr, hashStruct,
                                       nullptr, pKeyPtr.get());
    if (result != 1)
    {
        throw FwupdateError("Error %lu occured during EVP_DigestVerifyInit.",
                            ERR_get_error());
    }

    auto data = MappedMem::open(filePath);
    result =
        EVP_DigestVerifyUpdate(rsaVerifyCtx.get(), data.get(), data.size());
    if (result != 1)
    {
        throw FwupdateError("Error %lu occured during EVP_DigestVerifyUpdate.",
                            ERR_get_error());
    }

    auto signature = MappedMem::open(fileSig);
    result = EVP_DigestVerifyFinal(
        rsaVerifyCtx.get(), reinterpret_cast<unsigned char*>(signature.get()),
        signature.size());
    if (result == -1)
    {
        throw FwupdateError("Error %lu occured during EVP_DigestVerifyFinal.",
                            ERR_get_error());
    }

    return result == 1;
}
