/**
 * @brief User confirmation tool definition.
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

#include "utils/confirm.hpp"

#include <cstdio>
#include <iostream>
#include <regex>
#include <string>

namespace utils
{

const std::regex yesno("^\\s*(y|n|yes|no)(\\s+.*)?$", std::regex::icase);

bool confirm(const char* title, const char* prompt)
{
    printf("%s\n", title);
    std::string answer;
    std::smatch match;

    while (true)
    {
        printf("%s [y/N]: ", prompt);
        fflush(stdout);
        if (std::getline(std::cin, answer))
        {
            if (answer.empty())
            {
                break;
            }

            if (std::regex_match(answer, match, yesno) && 3 == match.size())
            {
                const auto& c = match[1].str()[0];
                return (c == 'y' || c == 'Y');
            }
        }
        else
        {
            break;
        }
    }

    return false;
}

} // namespace utils
