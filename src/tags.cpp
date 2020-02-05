/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
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
