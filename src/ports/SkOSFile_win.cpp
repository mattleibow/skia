/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkTypes.h"
#if defined(SK_BUILD_FOR_WIN)

#include "include/private/SkMalloc.h"
#include "include/private/SkNoncopyable.h"
#include "include/private/SkTFitsIn.h"
#include "src/core/SkLeanWindows.h"
#include "src/core/SkOSFile.h"
#include "src/core/SkStringUtils.h"

#include <io.h>
#include <new>
#include <stdio.h>
#include <sys/stat.h>

bool sk_exists(const char *path, SkFILE_Flags flags) {
    int mode = 0; // existence
    if (flags & kRead_SkFILE_Flag) {
        mode |= 4; // read
    }
    if (flags & kWrite_SkFILE_Flag) {
        mode |= 2; // write
    }
    return (0 == _access(path, mode));
}

typedef struct {
    ULONGLONG fVolume;
    ULONGLONG fLsbSize;
    ULONGLONG fMsbSize;
} SkFILEID;

static bool sk_ino(FILE* f, SkFILEID* id) {
    int fileno = _fileno((FILE*)f);
    if (fileno < 0) {
        return false;
    }

    HANDLE file = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return false;
    }

#ifdef SK_BUILD_FOR_WINRT
    FILE_ID_INFO info;
    if (0 == GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof(info))) {
        return false;
    }
    id->fVolume = info.VolumeSerialNumber;
    //TODO (mattleibow): are these bits in the right places?
    BYTE* fid = info.FileId.Identifier;
    id->fLsbSize =
        ((ULONGLONG)fid[8]  << 56) + ((ULONGLONG)fid[9]  << 48) +
        ((ULONGLONG)fid[10] << 40) + ((ULONGLONG)fid[11] << 32) +
        ((ULONGLONG)fid[12] << 24) + ((ULONGLONG)fid[13] << 16) +
        ((ULONGLONG)fid[14] <<  8) + ((ULONGLONG)fid[15] <<  0);
    id->fMsbSize =
        ((ULONGLONG)fid[0] << 56) + ((ULONGLONG)fid[1] << 48) +
        ((ULONGLONG)fid[2] << 40) + ((ULONGLONG)fid[3] << 32) +
        ((ULONGLONG)fid[4] << 24) + ((ULONGLONG)fid[5] << 16) +
        ((ULONGLONG)fid[6] <<  8) + ((ULONGLONG)fid[7] <<  0);
#else // SK_BUILD_FOR_WINRT
    //TODO: call GetFileInformationByHandleEx on Vista and later with FileIdInfo.
    BY_HANDLE_FILE_INFORMATION info;
    if (0 == GetFileInformationByHandle(file, &info)) {
        return false;
    }
    id->fVolume = info.dwVolumeSerialNumber;
    id->fLsbSize = info.nFileIndexLow + (((ULONGLONG)info.nFileIndexHigh) << 32);
    id->fMsbSize = 0;
#endif // SK_BUILD_FOR_WINRT

    return true;
}

bool sk_fidentical(FILE* a, FILE* b) {
    SkFILEID aID, bID;
    return sk_ino(a, &aID) && sk_ino(b, &bID)
           && aID.fLsbSize == bID.fLsbSize
           && aID.fMsbSize == bID.fMsbSize
           && aID.fVolume == bID.fVolume;
}

class SkAutoNullKernelHandle : SkNoncopyable {
public:
    SkAutoNullKernelHandle(const HANDLE handle) : fHandle(handle) { }
    ~SkAutoNullKernelHandle() { CloseHandle(fHandle); }
    operator HANDLE() const { return fHandle; }
    bool isValid() const { return SkToBool(fHandle); }
private:
    HANDLE fHandle;
};
typedef SkAutoNullKernelHandle SkAutoWinMMap;

void sk_fmunmap(const void* addr, size_t) {
    UnmapViewOfFile(addr);
}

void* sk_fdmmap(int fileno, size_t* length) {
    HANDLE file = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return nullptr;
    }

    LARGE_INTEGER fileSize;
#ifdef SK_BUILD_FOR_WINRT
    FILE_STANDARD_INFO fsi;
    if (0 == GetFileInformationByHandleEx(file, FileStandardInfo, &fsi, sizeof(fsi))) {
        // TODO: use SK_TRACEHR(GetLastError(), "Could not get file size.") to report.
        return nullptr;
    }
    fileSize = fsi.EndOfFile;
#else // SK_BUILD_FOR_WINRT
    if (0 == GetFileSizeEx(file, &fileSize)) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not get file size.") to report.
        return nullptr;
    }
#endif // SK_BUILD_FOR_WINRT
    if (!SkTFitsIn<size_t>(fileSize.QuadPart)) {
        return nullptr;
    }

#ifdef SK_BUILD_FOR_WINRT
    SkAutoWinMMap mmap(CreateFileMappingFromApp(file, nullptr, PAGE_READONLY, 0, nullptr));
#else // SK_BUILD_FOR_WINRT
    SkAutoWinMMap mmap(CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0, nullptr));
#endif // SK_BUILD_FOR_WINRT
    if (!mmap.isValid()) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not create file mapping.") to report.
        return nullptr;
    }

    // Eventually call UnmapViewOfFile
    void* addr = MapViewOfFile(mmap, FILE_MAP_READ, 0, 0, 0);
    if (nullptr == addr) {
        //TODO: use SK_TRACEHR(GetLastError(), "Could not map view of file.") to report.
        return nullptr;
    }

    *length = static_cast<size_t>(fileSize.QuadPart);
    return addr;
}

int sk_fileno(FILE* f) {
    return _fileno((FILE*)f);
}

void* sk_fmmap(FILE* f, size_t* length) {
    int fileno = sk_fileno(f);
    if (fileno < 0) {
        return nullptr;
    }

    return sk_fdmmap(fileno, length);
}

size_t sk_qread(FILE* file, void* buffer, size_t count, size_t offset) {
    int fileno = sk_fileno(file);
    HANDLE fileHandle = (HANDLE)_get_osfhandle(fileno);
    if (INVALID_HANDLE_VALUE == file) {
        return SIZE_MAX;
    }

    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    ULARGE_INTEGER winOffset;
    winOffset.QuadPart = offset;
    overlapped.Offset = winOffset.LowPart;
    overlapped.OffsetHigh = winOffset.HighPart;

    if (!SkTFitsIn<DWORD>(count)) {
        count = std::numeric_limits<DWORD>::max();
    }

    DWORD bytesRead;
    if (ReadFile(fileHandle, buffer, static_cast<DWORD>(count), &bytesRead, &overlapped)) {
        return bytesRead;
    }
    if (GetLastError() == ERROR_HANDLE_EOF) {
        return 0;
    }
    return SIZE_MAX;
}

////////////////////////////////////////////////////////////////////////////

struct SkOSFileIterData {
    SkOSFileIterData() : fHandle(0), fPath16(nullptr) { }
    HANDLE fHandle;
    uint16_t* fPath16;
};
static_assert(sizeof(SkOSFileIterData) <= SkOSFile::Iter::kStorageSize, "not_enough_space");

static uint16_t* concat_to_16(const char src[], const char suffix[]) {
    size_t  i, len = strlen(src);
    size_t  len2 = 3 + (suffix ? strlen(suffix) : 0);
    uint16_t* dst = (uint16_t*)sk_malloc_throw((len + len2) * sizeof(uint16_t));

    for (i = 0; i < len; i++) {
        dst[i] = src[i];
    }

    if (i > 0 && dst[i-1] != '/') {
        dst[i++] = '/';
    }
    dst[i++] = '*';

    if (suffix) {
        while (*suffix) {
            dst[i++] = *suffix++;
        }
    }
    dst[i] = 0;
    SkASSERT(i + 1 <= len + len2);

    return dst;
}

SkOSFile::Iter::Iter() { new (fSelf.get()) SkOSFileIterData; }

SkOSFile::Iter::Iter(const char path[], const char suffix[]) {
    new (fSelf.get()) SkOSFileIterData;
    this->reset(path, suffix);
}

SkOSFile::Iter::~Iter() {
    SkOSFileIterData& self = *static_cast<SkOSFileIterData*>(fSelf.get());
    sk_free(self.fPath16);
    if (self.fHandle) {
        ::FindClose(self.fHandle);
    }
    self.~SkOSFileIterData();
}

void SkOSFile::Iter::reset(const char path[], const char suffix[]) {
    SkOSFileIterData& self = *static_cast<SkOSFileIterData*>(fSelf.get());
    if (self.fHandle) {
        ::FindClose(self.fHandle);
        self.fHandle = 0;
    }
    if (nullptr == path) {
        path = "";
    }

    sk_free(self.fPath16);
    self.fPath16 = concat_to_16(path, suffix);
}

static bool is_magic_dir(const uint16_t dir[]) {
    // return true for "." and ".."
    return dir[0] == '.' && (dir[1] == 0 || (dir[1] == '.' && dir[2] == 0));
}

static bool get_the_file(HANDLE handle, SkString* name, WIN32_FIND_DATAW* dataPtr, bool getDir) {
    WIN32_FIND_DATAW    data;

    if (nullptr == dataPtr) {
        if (::FindNextFileW(handle, &data))
            dataPtr = &data;
        else
            return false;
    }

    for (;;) {
        if (getDir) {
            if ((dataPtr->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !is_magic_dir((uint16_t*)dataPtr->cFileName))
            {
                break;
            }
        } else {
            if (!(dataPtr->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                break;
            }
        }
        if (!::FindNextFileW(handle, dataPtr)) {
            return false;
        }
    }
    // if we get here, we've found a file/dir
    if (name) {
        const uint16_t* utf16name = (const uint16_t*)dataPtr->cFileName;
        const uint16_t* ptr = utf16name;
        while (*ptr != 0) { ++ptr; }
        *name = SkStringFromUTF16(utf16name, ptr - utf16name);
    }
    return true;
}

bool SkOSFile::Iter::next(SkString* name, bool getDir) {
    SkOSFileIterData& self = *static_cast<SkOSFileIterData*>(fSelf.get());
    WIN32_FIND_DATAW    data;
    WIN32_FIND_DATAW*   dataPtr = nullptr;

    if (self.fHandle == 0) {  // our first time
        if (self.fPath16 == nullptr || *self.fPath16 == 0) {  // check for no path
            return false;
        }

#ifdef SK_BUILD_FOR_WINRT
        self.fHandle = ::FindFirstFileExW((LPCWSTR)self.fPath16, FindExInfoStandard, &data, FindExSearchNameMatch, nullptr, 0);
#else // SK_BUILD_FOR_WINRT
        self.fHandle = ::FindFirstFileW((LPCWSTR)self.fPath16, &data);
#endif // SK_BUILD_FOR_WINRT
        if (self.fHandle != 0 && self.fHandle != (HANDLE)~0) {
            dataPtr = &data;
        }
    }
    return self.fHandle != (HANDLE)~0 && get_the_file(self.fHandle, name, dataPtr, getDir);
}

#endif//defined(SK_BUILD_FOR_WIN)
