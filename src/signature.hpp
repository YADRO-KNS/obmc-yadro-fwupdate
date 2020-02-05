/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

#include <string>

/**
 * @brief Verify signature of specified file
 *
 * @param keyFile  - path to publickey file
 * @param hashFunc - signature hash function
 * @param filePath - path to the file for verification
 * @param fileSig  - path to the file signature.
 *                   if not specified will be used as filePath + '.sig'
 *
 * @return true if signature is valid
 */
bool verify_file(const std::string& keyFile, const std::string& hashFunc,
                 const std::string& filePath);
