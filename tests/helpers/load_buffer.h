#pragma once
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>

inline std::vector<uint8_t> LoadFileOrSkip(const std::string &path) {
  std::vector<uint8_t> out;
  std::ifstream f(path, std::ios::binary);
  if (!f) return out;  // empty = caller should skip
  f.seekg(0, std::ios::end);
  auto sz = f.tellg();
  f.seekg(0, std::ios::beg);
  out.resize(static_cast<size_t>(sz));
  f.read(reinterpret_cast<char *>(out.data()), sz);
  return out;
}