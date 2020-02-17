/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "config.h"

#include "fwupdate.hpp"

#include "fwupderr.hpp"
#include "openbmc.hpp"
#include "signature.hpp"
#include "subprocess.hpp"
#include "tags.hpp"
#include "tracer.hpp"

#ifdef OPENPOWER_SUPPORT
#include "openpower.hpp"
#endif

#include <cstring>

namespace firmware
{

static fs::path create_tmp_dir()
{
    std::string dir(fs::temp_directory_path() / "fwupdateXXXXXX");
    if (!mkdtemp(dir.data()))
    {
        throw FwupdateError("mkdtemp() failed, error=%d: %s", errno,
                            strerror(errno));
    }
    return dir;
}

FwUpdate::FwUpdate(bool with_lock) :
    tmpdir(create_tmp_dir()), with_lock(with_lock)
{
#ifdef OPENPOWER_SUPPORT
    updaters.emplace_back(
        std::make_unique<openpower::OpenPowerUpdater>(tmpdir));
#endif
    updaters.emplace_back(std::make_unique<openbmc::OpenBmcUpdater>(tmpdir));
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
    if (with_lock)
    {
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

bool FwUpdate::add_file(const fs::path& file)
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
    if (!add_file(path))
    {
        Tracer tracer("Unpack firmware package");

        std::ignore = subprocess::exec("tar -xzf %s -C %s 2>/dev/null",
                                       path.c_str(), tmpdir.c_str());

        for (const auto& it : fs::directory_iterator(tmpdir))
        {
            add_file(it.path());
        }

        tracer.done();
    }
}

fs::path FwUpdate::get_fw_file(const std::string& filename)
{
    fs::path ret(tmpdir / filename);
    if (!fs::exists(ret))
    {
        throw FwupdateError("%s not found!", filename.c_str());
    }
    return ret;
}

void FwUpdate::system_level_verify(void)
{
    Tracer tracer("Check signature of firmware package");

    auto manifestFile = get_fw_file(MANIFEST_FILE_NAME);
    auto publicKeyFile = get_fw_file(PUBLICKEY_FILE_NAME);

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
            auto hashFunc =
                get_tag_value(p.path() / HASH_FILE_NAME, "HashType");

            try
            {
                valid = verify_file(publicKey, hashFunc, manifestFile);
                if (valid)
                {
                    valid = verify_file(publicKey, hashFunc, publicKeyFile);
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

void FwUpdate::check_machine_type(void)
{
    auto currentMachine =
        get_tag_value(OS_RELEASE_FILE, "OPENBMC_TARGET_MACHINE");
    if (currentMachine.empty())
    {
        // We are running on an old BMC version.
        fprintf(stdout, "WARNING: Current machine name is undefined, "
                        "the check is skipped.\n");
    }
    else
    {
        Tracer tracer("Check target machine type");

        auto manifestFile = get_fw_file(MANIFEST_FILE_NAME);
        auto targetMachine = get_tag_value(manifestFile, "MachineName");
        if (currentMachine != targetMachine)
        {
            throw FwupdateError("Frimware package is not compatible with this "
                                "system.");
        }

        tracer.done();
    }
}

void FwUpdate::verify(void)
{
    system_level_verify();
    check_machine_type();

    auto publicKeyFile = get_fw_file(PUBLICKEY_FILE_NAME);
    auto manifestFile = get_fw_file(MANIFEST_FILE_NAME);
    auto hashFunc = get_tag_value(manifestFile, "HashType");

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

} // namespace firmware
