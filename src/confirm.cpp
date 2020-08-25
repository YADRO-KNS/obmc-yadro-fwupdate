/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#include "confirm.hpp"

#include <cstdio>
#include <iostream>
#include <regex>
#include <string>

static const std::regex yesno("^\\s*(y|n|yes|no)(\\s+.*)?$", std::regex::icase);

bool confirm(const char* title, const char* prompt)
{
    printf("%s\n", title);
    std::string answer;
    std::smatch match;

    while (true)
    {
        printf("%s [y/N]: ", prompt);
        if (std::getline(std::cin, answer))
        {
            if (answer.empty())
            {
                break;
            }

            if (std::regex_match(answer, match, yesno))
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
