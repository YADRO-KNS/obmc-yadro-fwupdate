/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 YADRO.
 */

#pragma once

/**
 * @brief Ask user for confirmation.
 *
 * @param title  - message title
 * @param prompt - prompt message
 *
 * @return true if user agreed
 */
bool confirm(const char* title,
             const char* prompt = "Do you want to continue?");
