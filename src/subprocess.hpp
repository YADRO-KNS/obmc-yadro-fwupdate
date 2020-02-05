/**
 * @brief Execute externel commands helper declarations.
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
#include <sstream>
#include <string>

template <typename... Ts>
std::string concat_string(Ts const&... ts)
{
    std::stringstream s;
    ((s << ts << " "), ...);
    return s.str();
}

namespace subprocess
{

/**
 * @brief Check wait status.
 *
 * @param wstatus - status returned by pclose()/system()/waitpid().
 */
void check_wait_status(int wstatus);

/**
 * @brief Execute the external command
 *
 * @param cmd - Command and its arguments
 *
 * @return wait statue and command output
 */
std::pair<int, std::string> exec(const std::string& cmd);

template <typename... Args>
std::pair<int, std::string> exec(const std::string& cmd, Args const&... args)
{
    return exec(concat_string(cmd, args...));
}

} // namespace subprocess
