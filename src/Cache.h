#pragma once
#include <filesystem>

class Cache 
{
public: 
    explicit Cache(std::filesystem::path root);
    ~Cache();

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_Root; }
    
    [[nodiscard]] std::string Store(const std::filesystem::path& file) const;
    bool Retrieve(const std::string& sha, const std::filesystem::path& outFile) const;

private:
    [[nodiscard]] const std::filesystem::path Objects() const noexcept { return m_Root / "objects"; }

    [[nodiscard]] static std::string toHex(const unsigned char* d, size_t n);

private:
    std::filesystem::path m_Root;
};