#pragma once

#include <string>
#include <vector>

#include "token.h"

std::vector<Token> lex(const std::string &source);
