/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
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
