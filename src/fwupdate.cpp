/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "fwupdate.hpp"

#include "dbus.hpp"
#include "fwupderr.hpp"
#include "signature.hpp"
#include "subprocess.hpp"
#include "tracer.hpp"

#ifdef OBMC_PHOSPHOR_IMAGE
#include "image_openbmc.hpp"
#endif

#ifdef INTEL_PLATFORMS
#include "image_intel.hpp"
#endif

#ifdef INTEL_C62X_SUPPORT
#include "image_bios.hpp"
#endif

#ifdef OPENPOWER_SUPPORT
#include "image_openpower.hpp"
#endif

#include <cstring>
#include <fstream>
#include <regex>

/**
 * @brief Create temporary directory
 *
 * @return - Path to the new temporary directory
 */
static fs::path createTmpDir()
{
    std::string dir(fs::temp_directory_path() / "fwupdateXXXXXX");
    if (!mkdtemp(dir.data()))
    {
        throw FwupdateError("mkdtemp() failed, error=%d: %s", errno,
                            strerror(errno));
    }
    return dir;
}

static const std::regex cfgLine("^\\s*([a-z0-9_]+)\\s*=\\s*\"?([^\"]+)\"?\\s*$",
                                std::regex::icase);

/**
 * @brief Get value of the key from specified config file
 *
 * @param file - path to the config file
 * @param key  - the key name
 *
 * @return - value of the key
 */
static std::string getCfgValue(const fs::path& file, const std::string& key)
{
    std::string ret;
    std::ifstream efile;
    efile.exceptions(std::ifstream::failbit | std::ifstream::badbit |
                     std::ifstream::eofbit);
    try
    {
        std::string line;
        std::smatch match;
        efile.open(file);
        while (std::getline(efile, line))
        {
            if (std::regex_match(line, match, cfgLine) && match[1].str() == key)
            {
                ret = match[2].str();
                break;
            }
        }
        efile.close();
    }
    catch (const std::ios_base::failure&)
    {
        efile.close();
    }

    return ret;
}

FwUpdate::FwUpdate(bool force) : tmpdir(createTmpDir()), force(force)
{
#ifdef OPENPOWER_SUPPORT
    updaters.emplace_back(std::make_unique<OpenPowerUpdater>(tmpdir));
#endif
#ifdef INTEL_C62X_SUPPORT
    updaters.emplace_back(std::make_unique<BIOSUpdater>(tmpdir));
#endif
#ifdef OBMC_PHOSPHOR_IMAGE
    updaters.emplace_back(std::make_unique<OBMCPhosphorImageUpdater>(tmpdir));
#endif
#ifdef INTEL_PLATFORMS
    updaters.emplace_back(std::make_unique<IntelPlatformsUpdater>(tmpdir));
#endif
}

FwUpdate::~FwUpdate()
{
    unlock();

    updaters.clear();

    if (!tmpdir.empty())
    {
        std::error_code ec;
        fs::remove_all(tmpdir, ec);
        tmpdir.clear();
    }
}

void FwUpdate::lock()
{
    if (!force)
    {
#ifdef REBOOT_GUARD_SUPPORT
        Tracer tracer("Locking BMC reboot");
        startUnit(REBOOT_GUARD_ENABLE);
        locked = true;
        tracer.done();
#endif

        for (auto it = updaters.rbegin(); it != updaters.rend(); ++it)
        {
            (*it)->lock();
        }
    }
}

void FwUpdate::unlock()
{
    for (const auto& updater : updaters)
    {
        updater->unlock();
    }

#ifdef REBOOT_GUARD_SUPPORT
    if (locked)
    {
        Tracer tracer("Unlocking BMC reboot");
        startUnit(REBOOT_GUARD_DISABLE);
        locked = false;
        tracer.done();
    }
#endif
}

void FwUpdate::reset()
{
    lock();
    for (auto& updater : updaters)
    {
        updater->reset();
    }
    unlock();
}

bool FwUpdate::addFile(const fs::path& file)
{
    bool ret = false;

    if (fs::is_regular_file(file) && file.extension() != SIGNATURE_FILE_EXT)
    {
        for (auto& updater : updaters)
        {
            if (updater->add(file))
            {
                ret = true;
            }
        }
    }

    return ret;
}

void FwUpdate::unpack(const fs::path& path)
{
    if (!addFile(path))
    {
        Tracer tracer("Unpack firmware package");

        std::ignore =
            exec(TAR_CMD " -xf %s -C %s 2>&1", path.c_str(), tmpdir.c_str());

        for (const auto& it : fs::directory_iterator(tmpdir))
        {
            addFile(it.path());
        }

        tracer.done();
    }
}

fs::path FwUpdate::getFWFile(const std::string& filename)
{
    fs::path ret(tmpdir / filename);
    if (!fs::exists(ret))
    {
        throw FwupdateError("%s not found!", filename.c_str());
    }
    return ret;
}

void FwUpdate::systemLevelVerify()
{
    Tracer tracer("Check signature of firmware package");

    auto manifestFile = getFWFile(MANIFEST_FILE_NAME);
    auto publicKeyFile = getFWFile(PUBLICKEY_FILE_NAME);

    bool valid = false;
    try
    {
        // Verify the file signature with available public keys and hash
        // function. For any internal failure during the key/hash pair specific
        // validation, should continue the validation with next available
        // key/hash pair.
        for (const auto& p : fs::directory_iterator(SIGNED_IMAGE_CONF_PATH))
        {
            auto publicKey(p.path() / PUBLICKEY_FILE_NAME);
            auto hashFunc = getCfgValue(p.path() / HASH_FILE_NAME, "HashType");

            try
            {
                valid = verifyFile(publicKey, hashFunc, manifestFile);
                if (valid)
                {
                    valid = verifyFile(publicKey, hashFunc, publicKeyFile);
                    if (valid)
                    {
                        break;
                    }
                }
            }
            catch (...)
            {
                valid = false;
            }
        }
    }
    catch (const fs::filesystem_error&)
    {
        valid = false;
    }

    if (!valid)
    {
        throw FwupdateError("System level verification failed!");
    }
    tracer.done();
}

void FwUpdate::checkMachineType()
{
    auto currentMachine =
        getCfgValue(OS_RELEASE_FILE, "OPENBMC_TARGET_MACHINE");
    if (currentMachine.empty())
    {
        // We are running on an old BMC version.
        fprintf(stderr, "WARNING: Current machine name is undefined, "
                        "the check is skipped.\n");
    }
    else
    {
        Tracer tracer("Check target machine type");

        auto manifestFile = getFWFile(MANIFEST_FILE_NAME);
        auto targetMachine = getCfgValue(manifestFile, "MachineName");
        if (currentMachine != targetMachine)
        {
            throw FwupdateError(
                "Firmware package is not compatible with this system.\n"
                "Expected target machine type :  %s\n"
                "Actual target machine type   :  %s\n",
                targetMachine.c_str(), currentMachine.c_str());
        }

        tracer.done();
    }
}

void FwUpdate::verify()
{
    systemLevelVerify();

    auto publicKeyFile = getFWFile(PUBLICKEY_FILE_NAME);
    auto manifestFile = getFWFile(MANIFEST_FILE_NAME);
    auto hashFunc = getCfgValue(manifestFile, "HashType");

    for (auto& updater : updaters)
    {
        updater->verify(publicKeyFile, hashFunc);
    }
}

bool FwUpdate::install(bool reset)
{
    bool ret = false;

    lock();
    for (auto& updater : updaters)
    {
        if (updater->install(reset))
        {
            ret = true;
        }
    }
    unlock();

    return ret;
}
