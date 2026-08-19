#ifndef _SD_FILE_UTILS_H_
#define _SD_FILE_UTILS_H_

#include <Arduino.h>
#include <SdFat.h>

namespace SdFileUtils {

inline bool isSafeFileName(const String &name) {
    return name.length() > 0 &&
           name.indexOf('/') == -1 &&
           name.indexOf('\\') == -1 &&
           name.indexOf("..") == -1;
}

inline String joinPath(const String &dirPath, const String &name) {
    return (dirPath == "/" ? "/" : dirPath + "/") + name;
}

inline String toRelativePath(const String &path) {
    return path.startsWith("/") ? path.substring(1) : path;
}

inline bool openDirectory(SdFat &sd, SdFile &dir, const String &path) {
    if (path == "/") {
        return dir.open("/", O_READ) || dir.open(sd.vwd(), "/", O_READ);
    }

    if (dir.open(sd.vwd(), path.c_str(), O_READ)) {
        return dir.isDir();
    }

    String relativePath = toRelativePath(path);
    return relativePath.length() > 0 && dir.open(sd.vwd(), relativePath.c_str(), O_READ) && dir.isDir();
}

inline bool openFileRead(SdFat &sd, SdFile &file, const String &path) {
    if (file.open(sd.vwd(), path.c_str(), O_READ)) {
        return !file.isDir();
    }

    String relativePath = toRelativePath(path);
    return relativePath.length() > 0 && file.open(sd.vwd(), relativePath.c_str(), O_READ) && !file.isDir();
}

inline bool openFileWriteTruncate(SdFat &sd, SdFile &file, const String &path) {
    String relativePath = toRelativePath(path);
    return relativePath.length() > 0 && file.open(sd.vwd(), relativePath.c_str(), O_WRITE | O_CREAT | O_TRUNC) && !file.isDir();
}

inline bool openFileReadWrite(SdFat &sd, SdFile &file, const String &path) {
    String relativePath = toRelativePath(path);
    return relativePath.length() > 0 && file.open(sd.vwd(), relativePath.c_str(), O_RDWR) && !file.isDir();
}

inline bool removeFile(SdFat &sd, const String &path) {
    String relativePath = toRelativePath(path);
    return relativePath.length() > 0 && (SdFile::remove(sd.vwd(), relativePath.c_str()) || sd.remove(relativePath.c_str()));
}

inline bool isUsableDirEntry(const dir_t &dir) {
    return dir.name[0] != DIR_NAME_FREE &&
           dir.name[0] != DIR_NAME_DELETED &&
           dir.name[0] != '.' &&
           !DIR_IS_LONG_NAME(&dir) &&
           DIR_IS_FILE_OR_SUBDIR(&dir);
}

inline void shortNameFromDir(const dir_t &dir, char *out, size_t outSize) {
    if (outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (outSize >= 14) {
        FatFile::dirName(&dir, out);
    }
}

inline uint32_t fileSizeFromDir(const dir_t &dir) {
    uint32_t size = 0;
    memcpy(&size, &dir.fileSize, sizeof(size));
    return size;
}

template <typename Reader>
bool writeFileFromKnownLength(SdFat &sd, const String &path, size_t contentLength, Reader reader, size_t *bytesWrittenOut = nullptr) {
    SdFile file;
    if (!openFileWriteTruncate(sd, file, path)) {
        DBG_PRINT("[SDUTIL] open write failed: ");
        DBG_PRINTLN(path);
        return false;
    }

    const size_t WRITE_BLOCK_SIZE = 512;
    uint8_t buf[WRITE_BLOCK_SIZE];
    size_t numRemaining = contentLength;
    size_t bytesWritten = 0;
    unsigned long lastYield = millis();

    while (numRemaining > 0) {
        if (millis() - lastYield > 50) {
            yield();
            ESP.wdtFeed();
            lastYield = millis();
        }

        size_t numToRead = (numRemaining > WRITE_BLOCK_SIZE) ? WRITE_BLOCK_SIZE : numRemaining;
        int numRead = reader(buf, sizeof(buf), numToRead);
        if (numRead <= 0) {
            DBG_PRINT("[SDUTIL] reader failed path=");
            DBG_PRINT(path);
            DBG_PRINT(" requested=");
            DBG_PRINT(numToRead);
            DBG_PRINT(" read=");
            DBG_PRINTLN(numRead);
            file.close();
            removeFile(sd, path);
            return false;
        }

        int written = file.write(buf, (size_t)numRead);
        if (written < 0 || written != numRead) {
            DBG_PRINT("[SDUTIL] write failed path=");
            DBG_PRINT(path);
            DBG_PRINT(" read=");
            DBG_PRINT(numRead);
            DBG_PRINT(" written=");
            DBG_PRINTLN(written);
            file.close();
            removeFile(sd, path);
            return false;
        }

        numRemaining -= (size_t)numRead;
        bytesWritten += (size_t)written;
    }

    if (!file.sync()) {
        DBG_PRINT("[SDUTIL] sync failed path=");
        DBG_PRINTLN(path);
        file.close();
        removeFile(sd, path);
        return false;
    }

    file.close();
    if (bytesWrittenOut) {
        *bytesWrittenOut = bytesWritten;
    }
    return true;
}

inline uint16_t lfnCharAt(const ldir_t &ldir, uint8_t i) {
    if (i < LDIR_NAME1_DIM) {
        return ldir.name1[i];
    }
    if (i < (LDIR_NAME1_DIM + LDIR_NAME2_DIM)) {
        return ldir.name2[i - LDIR_NAME1_DIM];
    }
    if (i < (LDIR_NAME1_DIM + LDIR_NAME2_DIM + LDIR_NAME3_DIM)) {
        return ldir.name3[i - LDIR_NAME1_DIM - LDIR_NAME2_DIM];
    }
    return 0;
}

inline void clearLfnState(char *buf, uint8_t &expectedOrd, uint8_t &checksum, bool &valid) {
    memset(buf, 0, 256);
    expectedOrd = 0;
    checksum = 0;
    valid = false;
}

template <typename Callback>
bool scanDirectoryEntries(SdFile &dir, Callback callback) {
    dir.rewind();

    dir_t rawDir;
    uint16_t rawIndex = 0;
    char rawLfnBuf[256];
    uint8_t rawExpectedOrd = 0;
    uint8_t rawChecksum = 0;
    bool rawChainValid = false;
    clearLfnState(rawLfnBuf, rawExpectedOrd, rawChecksum, rawChainValid);

    while (dir.read(&rawDir, sizeof(dir_t)) == (int)sizeof(dir_t)) {
        if (rawDir.name[0] == DIR_NAME_FREE) {
            break;
        }

        if (rawDir.name[0] == DIR_NAME_DELETED || rawDir.name[0] == '.') {
            clearLfnState(rawLfnBuf, rawExpectedOrd, rawChecksum, rawChainValid);
            rawIndex++;
            continue;
        }

        if (DIR_IS_LONG_NAME(&rawDir)) {
            const ldir_t *ldir = reinterpret_cast<const ldir_t*>(&rawDir);
            uint8_t ord = ldir->ord & 0x1F;
            bool isLast = (ldir->ord & LDIR_ORD_LAST_LONG_ENTRY) != 0;

            if (isLast) {
                clearLfnState(rawLfnBuf, rawExpectedOrd, rawChecksum, rawChainValid);
                rawExpectedOrd = ord;
                rawChecksum = ldir->chksum;
            }

            bool chainOk = rawExpectedOrd > 0 && ord == rawExpectedOrd && ldir->type == 0;
            if (!chainOk || ord == 0 || ord > 20) {
                clearLfnState(rawLfnBuf, rawExpectedOrd, rawChecksum, rawChainValid);
                rawIndex++;
                continue;
            }

            for (uint8_t i = 0; i < 13; i++) {
                uint16_t c = lfnCharAt(*ldir, i);
                size_t pos = (size_t)(ord - 1) * 13 + i;
                if (pos >= sizeof(rawLfnBuf) - 1) {
                    break;
                }
                if (c == 0) {
                    rawLfnBuf[pos] = '\0';
                    break;
                }
                if (c == 0xFFFF) {
                    break;
                }
                rawLfnBuf[pos] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
            }

            rawExpectedOrd--;
            if (rawExpectedOrd == 0 && rawLfnBuf[0] != '\0') {
                rawChainValid = true;
            }
            rawIndex++;
            continue;
        }

        if (DIR_IS_FILE_OR_SUBDIR(&rawDir)) {
            char shortName[14];
            shortNameFromDir(rawDir, shortName, sizeof(shortName));
            String resolvedName = rawChainValid && rawLfnBuf[0] != '\0' ? String(rawLfnBuf) : String(shortName);
            if (resolvedName.length() > 0) {
                if (!callback(rawDir, resolvedName, rawIndex)) {
                    return false;
                }
            }
        }

        clearLfnState(rawLfnBuf, rawExpectedOrd, rawChecksum, rawChainValid);
        rawIndex++;
        yield();
    }

    return true;
}

} // namespace SdFileUtils

#endif