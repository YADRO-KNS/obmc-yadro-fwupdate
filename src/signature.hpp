/**
 * @brief Verify signature helpers declarations.
 *
 * This file is part of OpenBMC/OpenPOWER firmware updater.
 *
 * Copyright 2020 YADRO
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
