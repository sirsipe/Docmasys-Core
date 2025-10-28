#include "Cache.h"
#include <fstream>
#include <openssl/evp.h>
#include <vector>
#include <zstd.h>
#include <random>
#include <thread>

namespace fs = std::filesystem;

Cache::Cache(fs::path root) 
    : m_Root(std::move(root))
{
    fs::create_directories(Root());
    fs::create_directories(Objects());
}

Cache::~Cache()
{

}


/// @brief 
/// @param file 
/// @return 
std::string Cache::Store(const fs::path& file) const
{
 
    std::ifstream in(file, std::ios::binary);
    if (!in) 
        throw std::runtime_error("PushBlob: cannot open input");

    // --- Hash (EVP SHA-256)
    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (!md) 
        throw std::runtime_error("EVP_MD_CTX_new failed");
    
    if (EVP_DigestInit_ex(md, EVP_sha256(), nullptr) != 1) 
    {
        EVP_MD_CTX_free(md);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    // --- zstd streaming compressor
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!cctx) 
    { 
        EVP_MD_CTX_free(md); 
        throw std::runtime_error("ZSTD_createCCtx failed"); 
    }
    
    auto pledged = static_cast<unsigned long long>(std::filesystem::file_size(file));
    ZSTD_CCtx_setPledgedSrcSize(cctx, pledged);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_contentSizeFlag, 1); // store original size in frame

    int level = 3; // tune as you like
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
    // Optional: enable multithreaded compression for big files
    // ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 2);

    // Prepare destination directory (by SHA prefix later; temp lives under m_Objects/.tmp)
    fs::path tmpDir = Objects() / ".tmp";
    fs::create_directories(tmpDir);

    // Unique temp name (thread/pid/random)
    auto rand64 = []{
        static thread_local std::mt19937_64 rng{
            std::random_device{}() ^ (uint64_t(std::hash<std::thread::id>{}(std::this_thread::get_id()))<<1)
        };
        return rng();
    };

    fs::path tmpPath = tmpDir / ("tmp-" + std::to_string(rand64()) + ".zst");

    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) 
    { 
        ZSTD_freeCCtx(cctx); 
        EVP_MD_CTX_free(md); 
        throw std::runtime_error("open temp failed"); 
    }

    constexpr size_t IN_CHUNK  = 1u << 20; // 1 MiB
    constexpr size_t OUT_CHUNK = 1u << 17; // 128 KiB
    std::vector<char> inBuf(IN_CHUNK);
    std::vector<char> outBuf(OUT_CHUNK);

    uint64_t total = 0;

    // Stream input -> hash + compress
    for (;;) 
    {
        in.read(inBuf.data(), inBuf.size());
        std::streamsize got = in.gcount();
        if (got <= 0) break;

        // hash
        if (EVP_DigestUpdate(md, inBuf.data(), static_cast<size_t>(got)) != 1) 
        {
            ZSTD_freeCCtx(cctx); EVP_MD_CTX_free(md);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }

        // compress
        ZSTD_inBuffer zin{ inBuf.data(), static_cast<size_t>(got), 0 };
        while (zin.pos < zin.size) 
        {
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            size_t r = ZSTD_compressStream2(cctx, &zout, &zin, ZSTD_e_continue);

            if (ZSTD_isError(r)) 
            {
                ZSTD_freeCCtx(cctx); EVP_MD_CTX_free(md);
                throw std::runtime_error(std::string("zstd compressStream2 failed: ") + ZSTD_getErrorName(r));
            }

            if (zout.pos) 
                out.write(outBuf.data(), static_cast<std::streamsize>(zout.pos));
        }

        total += static_cast<uint64_t>(got);
    }

    // flush & finalize compressor
    {
        ZSTD_inBuffer zin{ nullptr, 0, 0 };

        for (;;) 
        {
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            size_t r = ZSTD_compressStream2(cctx, &zout, &zin, ZSTD_e_end);

            if (ZSTD_isError(r)) 
            {
                ZSTD_freeCCtx(cctx); EVP_MD_CTX_free(md);
                throw std::runtime_error(std::string("zstd finalize failed: ") + ZSTD_getErrorName(r));
            }

            if (zout.pos) 
                out.write(outBuf.data(), static_cast<std::streamsize>(zout.pos));

            if (r == 0) 
                break; // done
        }
    }
    out.flush();
    ZSTD_freeCCtx(cctx);

    // finalize hash
    unsigned char mdBuf[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    if (EVP_DigestFinal_ex(md, mdBuf, &mdLen) != 1) 
    {
        EVP_MD_CTX_free(md);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(md);

    std::string sha = toHex(mdBuf, mdLen); // 32 bytes -> 64 hex chars

    // destination path by prefix
    fs::path p = Objects() / sha.substr(0,2) / sha.substr(2,2);
    fs::create_directories(p);
    fs::path objPath = p / sha;

    // Atomic install: try rename; if target already exists, drop temp
    std::error_code ec;
    fs::rename(tmpPath, objPath, ec);
    if (ec) 
    {
        if (fs::exists(objPath)) 
        {
            // Another thread already stored it; remove our temp
            fs::remove(tmpPath);
        } 
        else 
        {
            // Some other failure; keep temp for diagnostics or clean up
            fs::remove(tmpPath);
            throw std::runtime_error(std::string("rename failed: ") + ec.message());
        }
    }

    return sha;
}

bool Cache::Retrieve(const std::string& sha, const std::filesystem::path& outFile) const
{
    namespace fs = std::filesystem;

    const fs::path obj = Objects() / sha.substr(0,2) / sha.substr(2,2) / sha;
    if (!fs::exists(obj))
        return false;

    std::ifstream in(obj, std::ios::binary);
    if (!in)
        throw std::runtime_error("Get: cannot open compressed object");

    fs::create_directories(outFile.parent_path());

    // temp file (atomic install)
    const fs::path tmpDir  = outFile.parent_path() / ".tmp";
    fs::create_directories(tmpDir);
    const fs::path tmpFile = tmpDir / (outFile.filename().string() + ".part");

    std::ofstream out(tmpFile, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Get: cannot open temp output");

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) {
        fs::remove(tmpFile);
        throw std::runtime_error("Get: ZSTD_createDCtx failed");
    }

    const size_t inChunk  = ZSTD_DStreamInSize();
    const size_t outChunk = ZSTD_DStreamOutSize();
    std::vector<char> inBuf(inChunk);
    std::vector<char> outBuf(outChunk);

    bool frameEnded = false;

    // Read & decompress input chunks
    while (!frameEnded) 
    {
        in.read(inBuf.data(), static_cast<std::streamsize>(inBuf.size()));
        const size_t readBytes = static_cast<size_t>(in.gcount());

        if (readBytes == 0 && !in.eof()) {
            // real I/O error
            ZSTD_freeDCtx(dctx);
            fs::remove(tmpFile);
            throw std::runtime_error("Get: read failed");
        }

        if (readBytes == 0) {
            // EOF reached: do ONE final empty call to flush and check frame end
            ZSTD_inBuffer  zin{ nullptr, 0, 0 };
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            size_t const r = ZSTD_decompressStream(dctx, &zout, &zin);
            
            if (ZSTD_isError(r))
            {
                ZSTD_freeDCtx(dctx);
                fs::remove(tmpFile);
                throw std::runtime_error(std::string("Get: finalize failed: ")
                                         + ZSTD_getErrorName(r));
            }
            
            if (zout.pos) 
            {
                out.write(outBuf.data(), static_cast<std::streamsize>(zout.pos));
                if (!out) 
                {
                    ZSTD_freeDCtx(dctx);
                    fs::remove(tmpFile);
                    throw std::runtime_error("Get: write failed (finalize)");
                }
            }
            if (r != 0) 
            {
                // decoder still expects more input → compressed file is truncated
                ZSTD_freeDCtx(dctx);
                fs::remove(tmpFile);
                throw std::runtime_error("Get: unexpected EOF in compressed stream");
            }
            frameEnded = true;
            break;
        }

        // Normal chunk
        ZSTD_inBuffer zin{ inBuf.data(), readBytes, 0 };
        while (zin.pos < zin.size) 
        {
            ZSTD_outBuffer zout{ outBuf.data(), outBuf.size(), 0 };
            size_t const r = ZSTD_decompressStream(dctx, &zout, &zin);

            if (ZSTD_isError(r)) 
            {
                ZSTD_freeDCtx(dctx);
                fs::remove(tmpFile);
                throw std::runtime_error(std::string("Get: zstd decompressStream failed: ")
                                         + ZSTD_getErrorName(r));
            }

            if (zout.pos) 
            {
                out.write(outBuf.data(), static_cast<std::streamsize>(zout.pos));
                if (!out) {
                    ZSTD_freeDCtx(dctx);
                    fs::remove(tmpFile);
                    throw std::runtime_error("Get: write failed");
                }
            }
            
            if (r == 0) 
            { // end of frame reached
                frameEnded = true;
                // If your format is guaranteed single-frame, you can ignore any trailing junk;
                // here we stop cleanly.
                break;
            }
        }
    }

    ZSTD_freeDCtx(dctx);
    out.flush();
    out.close();

    // Atomic rename (fallback copy if cross-device)
    std::error_code ec;
    fs::rename(tmpFile, outFile, ec);
    if (ec) 
    {
        // cross-device fallback
        std::ifstream tf(tmpFile, std::ios::binary);
        std::ofstream of(outFile, std::ios::binary | std::ios::trunc);
        of << tf.rdbuf();
        tf.close();
        of.close();
        fs::remove(tmpFile);
        if (!fs::exists(outFile))
            throw std::runtime_error(std::string("Get: rename/copy failed: ") + ec.message());
    }

    return true;
}


std::string Cache::toHex(const unsigned char* d, size_t n)
{
    static const char* H = "0123456789abcdef";
    
    std::string s; 
    s.resize(n*2);
    
    for (size_t i = 0; i < n; i++)
    { 
        s[2*i]=H[ (d[i] >> 4) & 0xF ]; 
        s[2*i+1]=H[ d[i] & 0xF ]; 
    }

    return s;
}