#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>

namespace bio {
    using namespace boost::iostreams;
}

struct TARFileHeader {
    char filename[100]; // NUL-terminated
    char mode[8];
    char uid[8];
    char gid[8];
    char fileSize[12];
    char lastModification[12];
    char checksum[8];
    char typeFlag;
    char linkedFileName[100];

    // USTAR fields
    char ustarIndicator[6]; // "ustar"
    char ustarVersion[2];   // 00
    char ownerUserName[32];
    char ownerGroupName[32];
    char deviceMajorNumber[8];
    char deviceMinorNumber[8];
    char filenamePrefix[155];
    char padding[12];

    std::vector<char> contents;

    size_t getRealFileSize() {
        return std::stoi(fileSize, 0, 8);
    }
    /**
     * Each block of tar must be padded to a total 512 bytes.
     *
     * The padded filesize is not represented in the filesize field.
     */
    size_t getPaddedFileSize() {
        auto realSize = getRealFileSize();
        return realSize + 512 - (realSize % 512);
    }
};

struct Archive {
    std::string gzFile;
    std::string tmpTar;
};

struct ReaddirEntry {
    std::string name;
    TARFileHeader header;
    struct stat *stbuf;
    off_t off;
    enum fuse_fill_dir_flags flags;
    ReaddirEntry() {}
    ReaddirEntry(std::string n, struct stat *st, off_t offset)
        : name(n), stbuf(st), off(offset),
        flags(static_cast<enum fuse_fill_dir_flags>(0))
    {};
    ReaddirEntry(std::string n, TARFileHeader header, struct stat *st, off_t offset)
        : name(n), header(header), stbuf(st), off(offset),
        flags(static_cast<enum fuse_fill_dir_flags>(0))
    {};
};

Archive archive;
std::vector<ReaddirEntry> entries;

static void *compress_init(struct fuse_conn_info *conn,
                           struct fuse_config *cfg)
{
    cfg->kernel_cache = 1;

    return NULL;
}

static int compress_getattr(const char *path, struct stat *stbuf,
                            struct fuse_file_info *fi)
{
    int res = 0;
    std::string name(path);
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_gid = getgid();
    stbuf->st_uid = getuid();
    if (name == "/") {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_size = 0;
        stbuf->st_nlink = 2;
    } else {
        auto loc = std::find_if(entries.begin(), entries.end(),
                                [name](auto val) {
                                    return name.substr(1, name.size()) == val.name;
                                });

        if (loc != entries.end()) {
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_size = loc->header.getRealFileSize();
            stbuf->st_nlink = 1;
        } else {
            return -ENOENT;
        }
    }

    return 0;
}

static int compress_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi,
                            enum fuse_readdir_flags flags)
{
    int off = offset;
    auto nullFlag = static_cast<enum fuse_fill_dir_flags>(0);

    if (off < entries.size())
        filler(buf, entries[off].name.c_str(), entries[off].stbuf,
               entries[off].off, entries[off].flags);

    return 0;
}

/**
 * The FS is stateless, so open does not need to do anything other than
 * see if the file exists.
 */
static int compress_open(const char *path, struct fuse_file_info *fi)
{
    std::string name(path);
    auto loc = std::find_if(entries.begin(), entries.end(),
                            [name](auto val) {
                                return name.substr(1, name.size()) == val.name;
                            });
    if (loc == entries.end())
        return -ENOENT;
    else
        return 0;
}

static int compress_opendir(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

static int compress_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi)
{
    std::string name(path);
    auto loc = std::find_if(entries.begin(), entries.end(),
                            [name](auto val) {
                                return name.substr(1, name.size()) == val.name;
                            });
    if (loc == entries.end()) {
        return -ENOENT;
    } else {
        auto readCount = loc->header.contents.size() - (size + offset);
        if (readCount < 0) {
            std::memcpy(buf, &loc->header.contents[offset], -readCount);
            return -readCount;
        } else {
            std::memcpy(buf, &loc->header.contents[offset], size);
            return size;
        }
    }
}

int main(int argc, char *argv[])
{
    struct fuse_operations compress_oper;
    memset(&compress_oper, 0, sizeof(struct fuse_operations));
    compress_oper.init    = compress_init;
    compress_oper.getattr = compress_getattr;
    compress_oper.readdir = compress_readdir;
    compress_oper.open    = compress_open;
    compress_oper.opendir = compress_opendir;
    compress_oper.read    = compress_read;

    archive.gzFile = argv[1];
    {
        std::fstream compressed(archive.gzFile);
        std::stringstream decompressed;

        bio::filtering_streambuf<bio::input> out;
        out.push(bio::gzip_decompressor());
        out.push(compressed);
        bio::copy(out, decompressed);
        auto str = decompressed.str();
        std::ofstream tmp("/tmp/archive.tar");
        tmp << str << std::endl;
    }

    entries.emplace_back(std::string(".."), nullptr, 1);
    entries.emplace_back(std::string("."), nullptr, 2);

    {
        int inf = open("/tmp/archive.tar", O_RDONLY);
        int counter = 3;
        TARFileHeader header;
        char zeroes[512];
        memset(zeroes, 0, 512);
        while(true) {
            read(inf, &header, 512);
            if (!memcmp(&header, zeroes, 512))
                break;

            header.contents.resize(header.getRealFileSize());
            read(inf, &header.contents[0], header.getRealFileSize());
            std::cout.write(&header.contents[0], header.getRealFileSize());
            lseek(inf, header.getPaddedFileSize() - header.getRealFileSize(), SEEK_CUR);
            entries.emplace_back(header.filename, header, nullptr, counter++);
        }
        close(inf);
    }

    return fuse_main(argc - 1, argv + 1, &compress_oper, NULL);
}
