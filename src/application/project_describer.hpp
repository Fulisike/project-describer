#pragma once

#include <iosfwd>

#include "application/command_line.hpp"

namespace project_describer {

int RunProjectDescriber(const CommandLineOptions& options, std::ostream& output,
                        std::ostream& errors);

}  // namespace project_describer
