#include "engine/assets/assets.hpp"

#include "engine/core/log.hpp"
#include "engine/platform/platform.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

bool assets_read_file(const char* path, std::vector<uint8_t>& out)
{
    out.clear();
    FILE* file = platform_file_open(path, "rb");
    if (!file)
    {
        LOG_ERROR("could not open '%s'", path);
        return false;
    }

    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    ok = ok && size >= 0 && fseek(file, 0, SEEK_SET) == 0;
    if (ok)
    {
        out.resize((size_t)size);
        ok = size == 0 || fread(out.data(), 1, (size_t)size, file) == (size_t)size;
    }
    fclose(file);

    if (!ok)
    {
        LOG_ERROR("could not read '%s'", path);
        out.clear();
        return false;
    }
    return true;
}
