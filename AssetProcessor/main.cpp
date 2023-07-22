#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <string>
#include <filesystem>
#include <unordered_map>
#include <sys/stat.h>

namespace fs = std::filesystem;

struct Asset_File_Info
{
    uint64_t last_write_time = 0;
};

#define ASSET_CHACHE_FILE_NAME "assets.cache"
#define ASSET_CACHE_FILE_MAGIC_NUMBER 0x55555555
#define ASSET_CACHE_FILE_VERSION 0

struct Asset_Cache_File_Header
{
    uint32_t magic_number;
    uint32_t version;
    uint32_t entry_count;
};

template< typename T >
void push(std::vector< uint8_t > &buffer, uint32_t &offset, const T *value, uint32_t count = 1)
{
    if (offset + sizeof(T) * count > buffer.size())
    {
        uint64_t new_size = (uint64_t)(buffer.size() * 1.5) + sizeof(T) * count;
        buffer.resize(new_size);
    }

    uint8_t *data = buffer.data() + offset;
    memcpy(data, value, sizeof(T) * count);
    offset += sizeof(T) * count;
}

template< typename T >
void grap(std::vector< uint8_t > &buffer, uint32_t &offset, T *value, uint32_t count = 1)
{
    uint8_t *data = buffer.data() + offset;
    memcpy(value, data, sizeof(T) * count);
    offset += sizeof(T) * count;
}

bool load_asset_cache(std::unordered_map< std::string, Asset_File_Info > &asset_cache, const std::string &filepath)
{
    if (!fs::exists(filepath))
    {
        fprintf(stderr, "couldn't find asset cache file: %s\n", filepath.c_str());
        return false;
    }

    FILE *asset_cache_file = fopen(filepath.c_str(), "rb");
    if (!asset_cache_file)
    {
        fprintf(stderr, "Error: couldn't open asset cache file: %s\n", filepath.c_str());
        return false;
    }

    fseek(asset_cache_file, 0, SEEK_END);
    uint64_t asset_cache_file_size = (uint64_t)ftell(asset_cache_file);
    fseek(asset_cache_file, 0, SEEK_SET);

    std::vector< uint8_t > asset_cache_buffer(asset_cache_file_size);
    fread(asset_cache_buffer.data(), asset_cache_file_size, 1, asset_cache_file);
    fclose(asset_cache_file);

    uint32_t offset = 0;

    Asset_Cache_File_Header header;
    grap(asset_cache_buffer, offset, &header);

    if (header.magic_number != ASSET_CACHE_FILE_MAGIC_NUMBER)
    {
        fprintf(stderr, "Error: couldn't open asset cache file: %s\n", filepath.c_str());
        return false;
    }

    if (header.version != ASSET_CACHE_FILE_VERSION)
    {
        // todo(amer): handle multiple versions
        return false;
    }

    for (uint32_t entry_index = 0; entry_index < header.entry_count; entry_index++)
    {
        uint32_t asset_filepath_length = 0;
        grap(asset_cache_buffer, offset, &asset_filepath_length);
        assert(asset_filepath_length);

        std::string asset_filepath;
        asset_filepath.resize(asset_filepath_length);
        grap(asset_cache_buffer, offset, asset_filepath.data(), asset_filepath_length + 1);
        
        Asset_File_Info info = {};
        grap(asset_cache_buffer, offset, &info);

        asset_cache[asset_filepath] = info;
    }

    return true;
}

bool save_asset_cache(const std::unordered_map< std::string, Asset_File_Info > &asset_cache, const std::string &filepath)
{
    uint32_t offset = 0;
    std::vector< uint8_t > asset_cache_buffer;

    Asset_Cache_File_Header header;
    header.magic_number = ASSET_CACHE_FILE_MAGIC_NUMBER;
    header.version = ASSET_CACHE_FILE_VERSION;
    header.entry_count = uint32_t(asset_cache.size());

    push(asset_cache_buffer, offset, &header);

    for (const auto& asset_cache_entry : asset_cache)
    {
        const auto& [filepath, info] = asset_cache_entry;
        uint32_t filepath_length = uint32_t(filepath.length());
        push(asset_cache_buffer, offset, &filepath_length);
        push(asset_cache_buffer, offset, filepath.data(), filepath_length + 1);
        push(asset_cache_buffer, offset, &info);
    }

    FILE *asset_cache_file = fopen(filepath.c_str(), "wb");
    if (!asset_cache_file)
    {
        fprintf(stderr, "can't create asset cache file: %s\n", filepath.c_str());
        return false;
    }
    fwrite(asset_cache_buffer.data(), offset, 1, asset_cache_file);
    fclose(asset_cache_file);

    return true;
}

int main(int argc, char **args)
{
    if (argc < 3)
    {
        fprintf(stderr, "Error: missing arguments [asset directory] [output directory]\n");
        return -1;
    }

    const char *asset_path = args[1];
    if (!fs::exists(fs::path(asset_path)))
    {
        fprintf(stderr, "Error: asset directory: %s doesn't exist\n", asset_path);
        return -1;
    }

    const char *output_path = args[2];
    if (!fs::exists(fs::path(asset_path)))
    {
        fprintf(stderr, "Error: output directory: %s doesn't exist\n", output_path);
        return -1;
    }

    bool force = true;

    std::unordered_map< std::string, Asset_File_Info > asset_cache;

    std::string asset_cache_filepath = std::string(asset_path) + "/" + ASSET_CHACHE_FILE_NAME;

    fprintf(stderr, "loading asset cache...\n");
    load_asset_cache(asset_cache, asset_cache_filepath);

    fprintf(stderr, "cooking assets...\n");

    for (const auto &entry : fs::recursive_directory_iterator(asset_path))
    {
        if (entry.is_regular_file())
        {
            const fs::path &filepath = entry.path();
            
            const std::string &extension = filepath.extension().string();
            const std::string &filename = filepath.string();
            const std::string &name = filepath.filename().string();

            // todo(amer): compiling shaders for now...
            bool is_shader = extension == ".vert" || extension == ".frag";
            if (is_shader)
            {
                Asset_File_Info &asset_file_info = asset_cache[filename];
                
                struct stat s;
                stat(filename.c_str(), &s);
                if (s.st_mtime != asset_file_info.last_write_time || force)
                {
                    asset_file_info.last_write_time = s.st_mtime;
                    std::string command = "glslangValidator -V --auto-map-locations " + filename + " -o " + output_path + "/" + name + ".spv";
                    system(command.c_str());
                }
            }
        }
    }

    fprintf(stderr, "saving asset cache...\n");
    save_asset_cache(asset_cache, asset_cache_filepath);
}