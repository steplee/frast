#pragma once

#include <stdexcept>

#define glCheck(x) { (x); auto e = glGetError(); if (e != 0) throw std::runtime_error(fmt::format("gl call {} failed with {:x}", #x, e)); }
