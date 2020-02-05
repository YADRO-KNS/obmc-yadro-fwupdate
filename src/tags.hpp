/**
 * @brief Tags value reader declaration.
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
 * @brief Read the file to get the value of the tag.
 *
 * @param filePath - path to file which contains the value of the tag.
 * @param tagName  - the tag name.
 *
 * @return the tag value
 */
std::string get_tag_value(const std::string& filePath,
                          const std::string& tagName);
