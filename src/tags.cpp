/**
 * @brief Tags value reader definitions.
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

#include "tags.hpp"

#include <fstream>
#include <regex>
#include <stdexcept>

static const std::regex tagLine("^\\s*([a-z0-9_]+)\\s*=\\s*\"?([^\"]+)\"?\\s*$",
                                std::regex::icase);

std::string get_tag_value(const std::string& filePath,
                          const std::string& tagName)
{
    std::string ret;
    std::ifstream efile;
    efile.exceptions(std::ifstream::failbit | std::ifstream::badbit |
                     std::ifstream::eofbit);
    try
    {
        std::string line;
        std::smatch match;
        efile.open(filePath);
        while (std::getline(efile, line))
        {
            if (std::regex_match(line, match, tagLine) &&
                match[1].str() == tagName)
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
