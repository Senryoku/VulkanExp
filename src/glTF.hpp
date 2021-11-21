#pragma once

#include <filesystem>

class glTF {
  public:
    glTF(std::filesystem::path path);
    ~glTF();
  private:
};