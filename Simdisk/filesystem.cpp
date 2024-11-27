#include "filesystem.h"
#include <iostream>
#include <cstring>
#include <sstream>

using namespace std;

FileSystem::FileSystem() : diskFile(nullptr), blockBitmap(nullptr),
inodeBitmap(nullptr), currentPath("/"), currentInodeNo(0),
currentUid(0), currentGid(0) {
    initializeUserSystem();
}

FileSystem::~FileSystem() {
    unmount();
}

void FileSystem::initializeUserSystem() {
    // 创建root用户
    // 创建默认用户
    UserInfo defaultUsers[] = {
        {0, 0, "root", "root"},      // 超级管理员
        {1, 1, "user1", "123456"},   // 普通用户1
        {2, 1, "user2", "123456"}    // 普通用户2
    };

    users.clear();
    for (const auto& user : defaultUsers) {
        users.push_back(user);
    }
}

bool FileSystem::format() {
    if (diskFile) {
        fclose(diskFile);
    }

    // 创建新的磁盘文件
    // 使用 fopen_s 替代 fopen
    errno_t err;
    if (fopen_s(&diskFile, "virtual_disk.img", "wb+") != 0) {
        cout << "无法创建虚拟磁盘文件" << endl;
        return false;
    }

    // 创建固定大小的文件
    char* zeroBuffer = new char[BLOCK_SIZE]();
    for (int i = 0; i < DISK_SIZE / BLOCK_SIZE; i++) {
        fwrite(zeroBuffer, BLOCK_SIZE, 1, diskFile);
    }
    delete[] zeroBuffer;

    // 回到文件开始处
    fseek(diskFile, 0, SEEK_SET);

    // 初始化超级块
    superBlock.blockSize = BLOCK_SIZE;
    superBlock.totalBlocks = DISK_SIZE / BLOCK_SIZE;
    superBlock.inodeCount = 1024; // 1024个inode
    superBlock.freeBlockCount = superBlock.totalBlocks - 145; // 减去系统占用的块
    superBlock.freeInodeCount = superBlock.inodeCount - 1; // 减去根目录inode
    superBlock.firstDataBlock = 145; // 1(超级块) + 16(位图) + 128(inode区)
    // 写入超级块
    fwrite(&superBlock, sizeof(SuperBlock), 1, diskFile);

    // 初始化位图
    blockBitmap = new char[16 * BLOCK_SIZE]();
    inodeBitmap = new char[BLOCK_SIZE]();

    // 标记系统区域为已使用
    for (int i = 0; i < 145; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        blockBitmap[byteIndex] |= (1 << bitIndex);
    }

    // 标记根目录inode为已使用
    inodeBitmap[0] |= 1;

    // 写入位图
    fwrite(blockBitmap, 16 * BLOCK_SIZE, 1, diskFile);
    fwrite(inodeBitmap, BLOCK_SIZE, 1, diskFile);

    // 初始化根目录的inode
    Inode rootInode;
    memset(&rootInode, 0, sizeof(Inode));
    rootInode.fileType = TYPE_DIR;
    rootInode.createTime = time(nullptr);
    rootInode.modifyTime = rootInode.createTime;
    rootInode.uid = 0;  // root用户
    rootInode.gid = 0;  // root组
    rootInode.perm = { 7, 5, 5 }; // rwxr-xr-x

    // 分配根目录的数据块
    int rootDataBlock = allocateBlock();
    if (rootDataBlock == -1) {
        cout << "分配根目录数据块失败" << endl;
        return false;
    }
    rootInode.blockAddresses[0] = rootDataBlock;

    // 写入根目录inode
    writeInode(0, rootInode);

    // 初始化根目录内容
    char dirBuffer[BLOCK_SIZE] = { 0 };
    DirEntry* entries = (DirEntry*)dirBuffer;

    // 添加 . 和 .. 目录项
    // 使用 strcpy_s 替代 strcpy
    strcpy_s(entries[0].fileName, sizeof(entries[0].fileName), ".");
    entries[0].inodeNo = 0;
    strcpy_s(entries[1].fileName, sizeof(entries[1].fileName), "..");
    entries[1].inodeNo = 0;

    writeBlock(rootDataBlock, dirBuffer);

    return true;
}

bool FileSystem::mount(const char* filename) {
    errno_t err;
    // 尝试以读写方式打开现有文件
    if (fopen_s(&diskFile, filename, "rb+") != 0) {
        cout << "文件系统不存在，正在格式化..." << endl;
        // 如果文件不存在，则格式化
        if (!format()) {
            cout << "格式化失败！" << endl;
            return false;
        }
        // 重新打开文件
        if (fopen_s(&diskFile, filename, "rb+") != 0) {
            cout << "无法打开文件系统！" << endl;
            return false;
        }
    }

    // 读取超级块
    fseek(diskFile, 0, SEEK_SET);
    //fread(&superBlock, sizeof(SuperBlock), 1, diskFile);
    if (fread(&superBlock, sizeof(SuperBlock), 1, diskFile) != 1) {
        cout << "读取超级块失败！" << endl;
        return false;
    }

    // 验证文件系统
    if (superBlock.blockSize != BLOCK_SIZE ||
        superBlock.totalBlocks != DISK_SIZE / BLOCK_SIZE) {
        cout << "文件系统格式不正确！" << endl;
        return false;
    }

    // 读取位图
    blockBitmap = new char[16 * BLOCK_SIZE];
    inodeBitmap = new char[BLOCK_SIZE];

    if (fread(blockBitmap, 16 * BLOCK_SIZE, 1, diskFile) != 1 ||
        fread(inodeBitmap, BLOCK_SIZE, 1, diskFile) != 1) {
        cout << "读取位图失败！" << endl;
        return false;
    }

    currentPath = "/";
    currentInodeNo = 0;

    return true;
}

bool FileSystem::unmount() {
    if (!diskFile) return false;

    // 保存超级块
    fseek(diskFile, 0, SEEK_SET);
    fwrite(&superBlock, sizeof(SuperBlock), 1, diskFile);

    // 保存位图
    fwrite(blockBitmap, 16 * BLOCK_SIZE, 1, diskFile);
    fwrite(inodeBitmap, BLOCK_SIZE, 1, diskFile);

    delete[] blockBitmap;
    delete[] inodeBitmap;
    fclose(diskFile);

    diskFile = nullptr;
    blockBitmap = nullptr;
    inodeBitmap = nullptr;

    return true;
}

// 分配一个空闲块
int FileSystem::allocateBlock() {
    for (int i = 0; i < superBlock.totalBlocks; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        if (!(blockBitmap[byteIndex] & (1 << bitIndex))) {
            blockBitmap[byteIndex] |= (1 << bitIndex);
            superBlock.freeBlockCount--;
            return i;
        }
    }
    return -1;
}

// 释放一个块
void FileSystem::freeBlock(int blockNo) {
    int byteIndex = blockNo / 8;
    int bitIndex = blockNo % 8;
    blockBitmap[byteIndex] &= ~(1 << bitIndex);
    superBlock.freeBlockCount++;
}

// 分配一个inode
int FileSystem::allocateInode() {
    for (int i = 0; i < superBlock.inodeCount; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        if (!(inodeBitmap[byteIndex] & (1 << bitIndex))) {
            inodeBitmap[byteIndex] |= (1 << bitIndex);
            superBlock.freeInodeCount--;
            return i;
        }
    }
    return -1;
}

// 释放一个inode
void FileSystem::freeInode(int inodeNo) {
    int byteIndex = inodeNo / 8;
    int bitIndex = inodeNo % 8;
    inodeBitmap[byteIndex] &= ~(1 << bitIndex);
    superBlock.freeInodeCount++;
}

// 读取数据块
void FileSystem::readBlock(int blockNo, char* buffer) {
    fseek(diskFile, blockNo * BLOCK_SIZE, SEEK_SET);
    fread(buffer, BLOCK_SIZE, 1, diskFile);
}

// 写入数据块
void FileSystem::writeBlock(int blockNo, const char* buffer) {
    fseek(diskFile, blockNo * BLOCK_SIZE, SEEK_SET);
    fwrite(buffer, BLOCK_SIZE, 1, diskFile);
}

// 读取inode
Inode FileSystem::readInode(int inodeNo) {
    Inode inode;
    long offset = sizeof(SuperBlock) + 17 * BLOCK_SIZE + inodeNo * sizeof(Inode);
    fseek(diskFile, offset, SEEK_SET);
    fread(&inode, sizeof(Inode), 1, diskFile);
    return inode;
}

// 写入inode
void FileSystem::writeInode(int inodeNo, const Inode& inode) {
    long offset = sizeof(SuperBlock) + 17 * BLOCK_SIZE + inodeNo * sizeof(Inode);
    fseek(diskFile, offset, SEEK_SET);
    fwrite(&inode, sizeof(Inode), 1, diskFile);
}


// 接上一部分代码...

// 分割路径
vector<string> FileSystem::splitPath(const string& path) {
    vector<string> parts;
    std::stringstream ss(path);
    string item;

    while (getline(ss, item, '/')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

// 根据路径查找inode号
int FileSystem::findInodeByPath(const string& path) {
    if (path == "/") return 0;

    vector<string> parts = splitPath(path);
    int currentInode = currentInodeNo;  // 从当前目录开始

    if (path[0] == '/') {
        currentInode = 0;  // 绝对路径从根目录开始
    }

    for (const auto& part : parts) {
        if (part == ".") continue;
        if (part == "..") {
            // 读取当前目录的inode
            Inode inode = readInode(currentInode);
            // 读取目录内容
            char buffer[BLOCK_SIZE];
            readBlock(inode.blockAddresses[0], buffer);
            DirEntry* entries = (DirEntry*)buffer;
            // 获取父目录的inode号（第二个目录项）
            currentInode = entries[1].inodeNo;
            continue;
        }

        bool found = false;
        Inode inode = readInode(currentInode);

        // 读取目录内容
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        // 查找目录项
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 && strcmp(entries[i].fileName, part.c_str()) == 0) {
                currentInode = entries[i].inodeNo;
                found = true;
                break;
            }
        }

        if (!found) return -1;
    }

    return currentInode;
}

// 创建目录
bool FileSystem::createDir(const string& path) {
    // 获取父目录路径和新目录名
    size_t pos = path.find_last_of('/');    //查找路径中最后一个 '/' 的位置
    string parentPath = (pos == std::string::npos) ? "." : path.substr(0, pos);//如果找不到 '/'（pos == npos）
    string dirName = (pos == std::string::npos) ? path : path.substr(pos + 1);

    if (dirName.length() >= MAX_FILENAME) {
        cout << "目录名过长" << endl;
        return false;
    }

    // 查找父目录的inode
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "父目录不存在" << endl;
        return false;
    }

    // 获取完整路径
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "完整路径: " << fullPath << endl;

    // 检查文件是否已存在
    if (findInodeByPath(fullPath) != -1) {
        cout << "错误: 该目录已存在" << endl;
        return false;
    }

    // 分配新的inode
    int newInodeNo = allocateInode();
    if (newInodeNo == -1) {
        cout << "无法分配inode" << endl;
        return false;
    }

    // 分配数据块
    int newBlockNo = allocateBlock();
    if (newBlockNo == -1) {
        freeInode(newInodeNo);
        cout << "无法分配数据块" << endl;
        return false;
    }

    // 初始化新目录的inode
    Inode newInode;
    memset(&newInode, 0, sizeof(Inode));
    newInode.fileType = TYPE_DIR;
    newInode.createTime = time(nullptr);
    newInode.modifyTime = newInode.createTime;
    newInode.blockAddresses[0] = newBlockNo;
    newInode.uid = currentUid;
    newInode.gid = currentGid;
    newInode.perm = { 7, 5, 5 }; // rwxr-xr-x

    // 写入新目录的inode
    writeInode(newInodeNo, newInode);

    // 初始化新目录的内容（. 和 ..）
    char dirBuffer[BLOCK_SIZE] = { 0 };
    DirEntry* entries = (DirEntry*)dirBuffer;

    strcpy_s(entries[0].fileName, sizeof(entries[0].fileName), ".");
    entries[0].inodeNo = newInodeNo;
    strcpy_s(entries[1].fileName, sizeof(entries[1].fileName), "..");
    entries[1].inodeNo = parentInodeNo;
   

    writeBlock(newBlockNo, dirBuffer);

    // 在父目录中添加新目录项
    Inode parentInode = readInode(parentInodeNo);
    char parentBuffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], parentBuffer);
    DirEntry* parentEntries = (DirEntry*)parentBuffer;

    // 查找空闲目录项
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (parentEntries[i].inodeNo == 0) {
            strcpy_s(parentEntries[i].fileName, sizeof(parentEntries[i].fileName), dirName.c_str());
            parentEntries[i].inodeNo = newInodeNo;
            writeBlock(parentInode.blockAddresses[0], parentBuffer);
            return true;
        }
    }
    
    cout << "父目录已满" << endl;
    freeBlock(newBlockNo);
    freeInode(newInodeNo);
    return false;
}

string FileSystem::getFormattedSize(size_t bytes) const {
    const char* units[] = { "B", "KB", "MB", "GB" };
    int unitIndex = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unitIndex < 3) {
        size /= 1024;
        unitIndex++;
    }

    stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return ss.str();
}

int FileSystem::countUsedInodes() const {
    int count = 0;
    for (int i = 0; i < superBlock.inodeCount; i++) {
        if ((inodeBitmap[i / 8] & (1 << (i % 8))) != 0) {
            count++;
        }
    }
    return count;
}

int FileSystem::countUsedBlocks() const {
    int count = 0;
    for (int i = 0; i < superBlock.totalBlocks; i++) {
        if ((blockBitmap[i / 8] & (1 << (i % 8))) != 0) {
            count++;
        }
    }
    return count;
}

string FileSystem::getSystemInfo() const {
    std::stringstream ss;

    // 文件系统基本信息
    ss << "\nSimDisk 文件系统信息\n"
        << "----------------------------------------\n"
        << "块大小: " << getFormattedSize(superBlock.blockSize) << "\n"
        << "总容量: " << getFormattedSize(static_cast<size_t>(superBlock.totalBlocks) * BLOCK_SIZE) << "\n"
        << "\n";

    // 使用情况
    int usedInodes = countUsedInodes();
    int usedBlocks = countUsedBlocks();

    ss << "Inode 使用情况:\n"
        << "  总数: " << superBlock.inodeCount << "\n"
        << "  已用: " << usedInodes << "\n"
        << "  可用: " << (superBlock.inodeCount - usedInodes) << "\n"
        << "  使用率: " << std::fixed << std::setprecision(2)
        << (usedInodes * 100.0 / superBlock.inodeCount) << "%\n"
        << "\n";

    ss << "数据块使用情况:\n"
        << "  总数: " << superBlock.totalBlocks << "\n"
        << "  已用: " << usedBlocks << "\n"
        << "  可用: " << (superBlock.totalBlocks - usedBlocks) << "\n"
        << "  使用率: " << std::fixed << std::setprecision(2)
        << (usedBlocks * 100.0 / superBlock.totalBlocks) << "%\n"
        << "\n";

    // 文件系统布局
    ss << "文件系统布局:\n"
        << "  超级块: 0 - " << sizeof(SuperBlock) << " 字节\n"
        << "  块位图: " << sizeof(SuperBlock) << " - "
        << (sizeof(SuperBlock) + 16 * BLOCK_SIZE) << " 字节\n"
        << "  Inode位图: " << (sizeof(SuperBlock) + 16 * BLOCK_SIZE) << " - "
        << (sizeof(SuperBlock) + 17 * BLOCK_SIZE) << " 字节\n"
        << "  Inode区: " << (sizeof(SuperBlock) + 17 * BLOCK_SIZE) << " - "
        << superBlock.firstDataBlock * BLOCK_SIZE << " 字节\n"
        << "  数据区: " << superBlock.firstDataBlock * BLOCK_SIZE << " - "
        << superBlock.totalBlocks * BLOCK_SIZE << " 字节\n";

    return ss.str();
}


// 改变当前目录
bool FileSystem::changeDir(const string& path) {
    // 如果是当前目录（.），直接返回成功
    if (path == ".") {
        return true;  // 保持在当前目录
    }

    int inodeNo = findInodeByPath(path);
    if (inodeNo == -1) {
        cout << "目录不存在" << endl;
        return false;
    }

    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_DIR) {
        cout << "不是目录" << endl;
        return false;
    }

    // 保存旧的状态
    int oldInodeNo = currentInodeNo;
    string oldPath = currentPath;

    // 尝试更新状态
    currentInodeNo = inodeNo;

    // 更新当前路径
    if (path[0] == '/') {
        currentPath = path;
    }
    else if (path == "..") {
        size_t pos = currentPath.find_last_of('/');
        if (pos == 0) currentPath = "/";
        else currentPath = currentPath.substr(0, pos);
    }
    else if (path != ".") {
        if (currentPath == "/") currentPath += path;
        else currentPath += "/" + path;
    }

    // 验证新路径是否有效
    if (findInodeByPath(currentPath) == -1) {
        // 如果无效，恢复旧状态
        currentInodeNo = oldInodeNo;
        currentPath = oldPath;
        return false;
    }

    return true;
}

// 列出目录内容
vector<string> FileSystem::listDir(bool recursive) {
    vector<string> result;

    if (!recursive) {
        // 非递归实现
        Inode inode = readInode(currentInodeNo);
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                Inode entryInode = readInode(entries[i].inodeNo);
                result.push_back(getFileInfo(entries[i], entryInode));
            }
        }
    }
    else {
        // 递归列出所有子目录
        listDirRecursive(currentInodeNo, currentPath, result);
    }
    return result;
}

string FileSystem::getFileInfo(const DirEntry& entry, const Inode& inode) {
    string type = (inode.fileType == TYPE_DIR) ? "d" : "-";
    string name = entry.fileName;

    char timeStr[26];
    ctime_s(timeStr, sizeof(timeStr), &inode.modifyTime);
    string modTime(timeStr);
    modTime = modTime.substr(0, modTime.length() - 1);  // 移除换行符

    std::stringstream ss;
    ss << type << "rwxr-xr-x " << std::setw(8) << inode.fileSize << " "
        << modTime << " " << name;
    return ss.str();
}

void FileSystem::listDirRecursive(int inodeNo, const string& path, vector<string>& result) {
    Inode inode = readInode(inodeNo);
    char buffer[BLOCK_SIZE];
    readBlock(inode.blockAddresses[0], buffer);
    DirEntry* entries = (DirEntry*)buffer;

    // 添加目录路径标题
    result.push_back("\n" + path + " 的目录");
    result.push_back("----------------------------------------");

    //// 列出当前目录的内容
    //for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
    //    if (entries[i].inodeNo != 0) {
    //        Inode entryInode = readInode(entries[i].inodeNo);
    //        result.push_back(getFileInfo(entries[i], entryInode));

    //        // 递归处理子目录
    //        if (entryInode.fileType == TYPE_DIR &&
    //            strcmp(entries[i].fileName, ".") != 0 &&
    //            strcmp(entries[i].fileName, "..") != 0) {
    //            string newPath = path;
    //            if (newPath != "/") newPath += "/";
    //            newPath += entries[i].fileName;
    //            listDirRecursive(entries[i].inodeNo, newPath, result);
    //        }
    //    }
    //}


    // 先列出当前目录下的所有内容
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            Inode entryInode = readInode(entries[i].inodeNo);
            result.push_back(getFileInfo(entries[i], entryInode));
        }
    }
    // 然后单独处理子目录的递归
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            Inode entryInode = readInode(entries[i].inodeNo);
            // 只对目录类型进行递归，且跳过 . 和 ..
            if (entryInode.fileType == TYPE_DIR &&
                strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                string newPath = path;
                if (newPath != "/") newPath += "/";
                newPath += entries[i].fileName;
                listDirRecursive(entries[i].inodeNo, newPath, result);
            }
        }
    }


}


bool FileSystem::isDirEmpty(int inodeNo) {
    Inode inode = readInode(inodeNo);
    char buffer[BLOCK_SIZE];
    readBlock(inode.blockAddresses[0], buffer);
    DirEntry* entries = (DirEntry*)buffer;

    int count = 0;
    // 检查是否只有 . 和 .. 两个条目
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            count++;
            if (strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                return false;
            }
        }
    }
    // 确保只有 . 和 .. 两个条目
    return count == 2;
}

void FileSystem::recursiveDelete(int inodeNo) {
    Inode inode = readInode(inodeNo);

    if (inode.fileType == TYPE_DIR) {
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        // 递归删除所有条目
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 &&
                strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                recursiveDelete(entries[i].inodeNo);
            }
        }
    }

    // 释放数据块
    for (int i = 0; i < 10; i++) {
        if (inode.blockAddresses[i] != 0) {
            freeBlock(inode.blockAddresses[i]);
        }
    }

    // 释放inode
    freeInode(inodeNo);
}

bool FileSystem::deleteDir(const string& path, bool force) {
    cout << "\n开始删除目录: " << path << endl;

    // 基本检查
    if (path == "/" || path == "." || path == "..") {
        cout << "错误: 不能删除特殊目录" << endl;
        return false;
    }

    // 获取完整路径
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "完整路径: " << fullPath << endl;

    int inodeNo = findInodeByPath(fullPath);
    if (inodeNo == -1) {
        cout << "错误: 目录不存在: " << fullPath << endl;
        return false;
    }
    cout << "找到目录inode号: " << inodeNo << endl;

    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_DIR) {
        cout << "错误: 不是目录" << endl;
        return false;
    }

    // 检查是否为空目录
    bool isEmpty = isDirEmpty(inodeNo);
    cout << "目录是否为空: " << (isEmpty ? "是" : "否") << endl;

    if (!isEmpty && !force) {
        cout << "目录不为空" << endl;
        return false;
    }

    try {
        // 获取父目录路径
        string parentPath;
        string dirName;

        size_t lastSlash = fullPath.find_last_of('/');
        if (lastSlash == 0) {
            parentPath = "/";
            dirName = fullPath.substr(1);
        }
        else {
            parentPath = fullPath.substr(0, lastSlash);
            dirName = fullPath.substr(lastSlash + 1);
        }

        cout << "父目录路径: " << parentPath << endl;
        cout << "目录名: " << dirName << endl;

        int parentInodeNo = findInodeByPath(parentPath);
        if (parentInodeNo == -1) {
            cout << "错误: 找不到父目录" << endl;
            return false;
        }
        cout << "父目录inode号: " << parentInodeNo << endl;

        // 从父目录中删除该目录项
        Inode parentInode = readInode(parentInodeNo);
        char buffer[BLOCK_SIZE];
        readBlock(parentInode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        bool found = false;
        cout << "开始在父目录中查找目录项..." << endl;
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                cout << "检查目录项 " << i << ": " << entries[i].fileName
                    << " (inode: " << entries[i].inodeNo << ")" << endl;

                if (strcmp(entries[i].fileName, dirName.c_str()) == 0) {
                    cout << "找到目标目录项，位置: " << i << endl;
                    int targetInodeNo = entries[i].inodeNo;

                    // 清除目录项
                    entries[i].inodeNo = 0;
                    memset(entries[i].fileName, 0, MAX_FILENAME);
                    found = true;

                    // 写回父目录的更改
                    writeBlock(parentInode.blockAddresses[0], buffer);
                    cout << "已从父目录移除目录项" << endl;

                    // 递归删除目录内容
                    recursiveDelete(targetInodeNo);
                    cout << "递归删除完成" << endl;
                    break;
                }
            }
        }

        if (!found) {
            cout << "错误: 在父目录中未找到目录项" << endl;
            return false;
        }

        cout << "目录删除成功" << endl;
        return true;
    }
    catch (const std::exception& e) {
        cout << "删除目录时发生错误: " << e.what() << endl;
        return false;
    }
}


bool FileSystem::createFile(const string& path, bool inputContent) {
    cout << "开始创建文件: " << path << endl;

    // 基本检查
    if (path.empty()) {
        cout << "错误: 文件路径为空" << endl;
        return false;
    }

    // 检查文件名是否合法（不能包含特殊字符）
    string invalidChars = "\\/:*?\"<>|";
    if (path.find_first_of(invalidChars) != string::npos) {
        cout << "错误: 文件名包含非法字符" << endl;
        return false;
    }

    // 获取完整路径
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "完整路径: " << fullPath << endl;

    // 检查文件是否已存在
    if (findInodeByPath(fullPath) != -1) {
        cout << "错误: 文件已存在" << endl;
        return false;
    }

    // 获取父目录路径和文件名
    string parentPath;
    string fileName;
    size_t lastSlash = fullPath.find_last_of('/');
    if (lastSlash == 0) {
        parentPath = "/";
        fileName = fullPath.substr(1);
    }
    else {
        parentPath = fullPath.substr(0, lastSlash);
        fileName = fullPath.substr(lastSlash + 1);
    }

    // 检查文件名长度
    if (fileName.length() >= MAX_FILENAME) {
        cout << "错误: 文件名过长" << endl;
        return false;
    }

    // 检查父目录是否存在
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "错误: 父目录不存在" << endl;
        return false;
    }

    // 检查父目录是否是目录
    Inode parentInode = readInode(parentInodeNo);
    if (parentInode.fileType != TYPE_DIR) {
        cout << "错误: 父路径不是目录" << endl;
        return false;
    }

    // 检查是否有足够的inode 
    int newInodeNo = allocateInode();
    if (newInodeNo == -1) {
        cout << "错误: 没有空闲的inode" << endl;
        return false;
    }

    // 初始化inode
    Inode newInode;
    newInode.fileType = TYPE_FILE;
    newInode.fileSize = 0;
    newInode.createTime = time(nullptr);
    newInode.modifyTime = newInode.createTime;
    memset(newInode.blockAddresses, 0, sizeof(newInode.blockAddresses));

    // 写入新inode
    writeInode(newInodeNo, newInode);
    cout << "已分配inode号: " << newInodeNo << endl;

    // 在父目录中添加目录项
    char buffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], buffer);
    DirEntry* entries = (DirEntry*)buffer;

    // 查找空闲目录项
    bool entryAdded = false;
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo == 0) {
            entries[i].inodeNo = newInodeNo;
            strncpy_s(entries[i].fileName, fileName.c_str(), MAX_FILENAME - 1);
            entries[i].fileName[MAX_FILENAME - 1] = '\0';
            entryAdded = true;
            cout << "已在父目录添加目录项" << endl;
            break;
        }
    }

    if (!entryAdded) {
        cout << "错误: 父目录已满" << endl;
        freeInode(newInodeNo);
        return false;
    }

    // 写回父目录块
    writeBlock(parentInode.blockAddresses[0], buffer);

    // 如果需要输入内容
    if (inputContent) {
        cout << "请输入文件内容（输入 :wq 保存并退出）：" << endl;
        string content, line;
        while (getline(cin, line)) {
            if (line == ":wq") {
                break;
            }
            content += line + "\n";
        }

        if (!writeFileContent(newInodeNo, content)) {
            cout << "错误: 写入文件内容失败" << endl;
            // 清理已创建的文件
            entries[entryAdded].inodeNo = 0;
            memset(entries[entryAdded].fileName, 0, MAX_FILENAME);
            writeBlock(parentInode.blockAddresses[0], buffer);
            freeInode(newInodeNo);
            return false;
        }
        cout << "文件内容写入成功" << endl;
    }

    cout << "文件创建成功" << endl;
    return true;
}

bool FileSystem::writeFileContent(int inodeNo, const string& content) {
    Inode inode = readInode(inodeNo);

    // 计算需要的块数
    size_t contentSize = content.length();
    size_t blocksNeeded = (contentSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // 分配所需的块
    if (!allocateFileBlocks(inode, contentSize)) {
        return false;
    }

    // 写入内容
    size_t written = 0;
    for (size_t i = 0; i < blocksNeeded && written < contentSize; i++) {
        char buffer[BLOCK_SIZE] = { 0 };
        size_t toWrite = std::min<size_t>(BLOCK_SIZE, contentSize - written);
        memcpy(buffer, content.c_str() + written, toWrite);
        writeBlock(inode.blockAddresses[i], buffer);
        written += toWrite;
    }

    // 更新inode
    inode.fileSize = contentSize;
    inode.modifyTime = time(nullptr);
    writeInode(inodeNo, inode);

    return true;
}

bool FileSystem::allocateFileBlocks(Inode& inode, size_t size) {
    size_t blocksNeeded = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocksNeeded > 10) {  // 暂时只支持直接块
        return false;
    }

    // 分配新块
    for (size_t i = 0; i < blocksNeeded; i++) {
        if (inode.blockAddresses[i] == 0) {
            int newBlock = allocateBlock();
            if (newBlock == -1) {
                // 分配失败，释放已分配的块
                for (size_t j = 0; j < i; j++) {
                    freeBlock(inode.blockAddresses[j]);
                    inode.blockAddresses[j] = 0;
                }
                return false;
            }
            inode.blockAddresses[i] = newBlock;
        }
    }

    return true;
}

bool FileSystem::catFile(const string& path) {
    // 获取完整路径
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "完整路径: " << fullPath << endl;

    // 查找文件
    int inodeNo = findInodeByPath(fullPath);
    cout << "找到inode号: " << inodeNo << endl;

    if (inodeNo == -1) {
        cout << "错误: 文件不存在: " << path << endl;
        return false;
    }

    // 读取inode
    Inode inode = readInode(inodeNo);
    cout << "文件类型: " << inode.fileType << endl;
    cout << "文件大小: " << inode.fileSize << " 字节" << endl;

    if (inode.fileType != TYPE_FILE) {
        cout << "错误: " << path << " 不是文件" << endl;
        return false;
    }

    // 计算需要读取的块数
    int blocksNeeded = (inode.fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // 读取并显示文件内容
    cout << "\n文件 " << path << " 的内容：" << endl;
    cout << std::string(40, '-') << endl;

    size_t totalRead = 0;
    char buffer[BLOCK_SIZE];

    for (int i = 0; i < blocksNeeded && totalRead < inode.fileSize; i++) {
        if (inode.blockAddresses[i] == 0) {
            cout << "错误: 文件数据块损坏" << endl;
            return false;
        }

        // 读取数据块
        readBlock(inode.blockAddresses[i], buffer);

        // 计算这个块需要显示多少字节
        size_t bytesToShow = std::min<size_t>(BLOCK_SIZE, inode.fileSize - totalRead);

        // 显示内容
        for (size_t j = 0; j < bytesToShow; j++) {
            cout << buffer[j];
        }
        totalRead += bytesToShow;
    }

    cout << endl;
    cout << std::string(40, '-') << endl;
    return true;
}

bool FileSystem::copyFile(const string& srcPath, const string& destPath) {
    cout << "\n开始复制文件..." << endl;
    cout << "源文件: " << srcPath << endl;
    cout << "目标文件: " << destPath << endl;

    bool isFromHost = srcPath.substr(0, 6) == "<host>";
    bool isToHost = destPath.substr(0, 6) == "<host>";

    try {
        if (isFromHost && isToHost) {
            cout << "错误: 不支持在主机文件系统间复制" << endl;
            return false;
        }
        else if (isFromHost) {
            // 从主机复制到模拟文件系统
            return copyFromHostFile(srcPath.substr(6), destPath);
        }
        else if (isToHost) {
            // 从模拟文件系统复制到主机
            return copyToHostFile(srcPath, destPath.substr(6));
        }
        else {
            // 在模拟文件系统内部复制
            return copyWithinFS(srcPath, destPath);
        }
    }
    catch (const std::exception& e) {
        cout << "复制过程中发生错误: " << e.what() << endl;
        return false;
    }
}

bool FileSystem::copyWithinFS(const string& srcPath, const string& destPath) {
    // 获取完整路径
    string fullSrcPath = getFullPath(srcPath);
    string fullDestPath = getFullPath(destPath);

    // 检查源文件
    int srcInodeNo = findInodeByPath(fullSrcPath);
    if (srcInodeNo == -1) {
        cout << "错误: 源文件不存在: " << srcPath << endl;
        return false;
    }

    // 读取源文件inode
    Inode srcInode = readInode(srcInodeNo);
    if (srcInode.fileType != TYPE_FILE) {
        cout << "错误: 源路径不是文件" << endl;
        return false;
    }

    // 处理目标路径
    if (isDirectory(fullDestPath)) {
        fullDestPath += "/" + getFileName(fullSrcPath);
    }

    // 检查目标文件是否已存在
    if (findInodeByPath(fullDestPath) != -1) {
        cout << "错误: 目标文件已存在" << endl;
        return false;
    }

    // 创建目标文件
    int destInodeNo = allocateInode();
    if (destInodeNo == -1) {
        cout << "错误: 无法分配inode" << endl;
        return false;
    }

    // 初始化目标文件inode
    Inode destInode;
    destInode.fileType = TYPE_FILE;
    destInode.fileSize = 0;
    destInode.createTime = time(nullptr);
    destInode.modifyTime = time(nullptr);
    memset(destInode.blockAddresses, 0, sizeof(destInode.blockAddresses));

    // 复制文件内容
    char buffer[BLOCK_SIZE];
    int blocksNeeded = (srcInode.fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (int i = 0; i < blocksNeeded; i++) {
        // 读取源块
        readBlock(srcInode.blockAddresses[i], buffer);

        // 为目标文件分配新块
        int newBlock = allocateBlock();
        if (newBlock == -1) {
            cout << "错误: 无法分配数据块" << endl;
            // 清理已分配的资源
            for (int j = 0; j < i; j++) {
                freeBlock(destInode.blockAddresses[j]);
            }
            freeInode(destInodeNo);
            return false;
        }

        destInode.blockAddresses[i] = newBlock;
        writeBlock(newBlock, buffer);
    }

    // 更新目标文件inode
    destInode.fileSize = srcInode.fileSize;
    writeInode(destInodeNo, destInode);

    // 在父目录中添加目标文件的目录项
    string destParentPath = fullDestPath.substr(0, fullDestPath.find_last_of('/'));
    string destFileName = getFileName(fullDestPath);

    int parentInodeNo = findInodeByPath(destParentPath);
    Inode parentInode = readInode(parentInodeNo);

    char dirBuffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], dirBuffer);
    DirEntry* entries = (DirEntry*)dirBuffer;

    bool entryAdded = false;
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo == 0) {
            entries[i].inodeNo = destInodeNo;
            //strncpy(entries[i].fileName, destFileName.c_str(), MAX_FILENAME - 1);
            //entries[i].fileName[MAX_FILENAME - 1] = '\0';
            strncpy_s(entries[i].fileName, MAX_FILENAME, destFileName.c_str(), MAX_FILENAME - 1);
            entryAdded = true;
            break;
        }
    }

    if (!entryAdded) {
        cout << "错误: 目录已满" << endl;
        // 清理已分配的资源
        for (int i = 0; i < blocksNeeded; i++) {
            freeBlock(destInode.blockAddresses[i]);
        }
        freeInode(destInodeNo);
        return false;
    }

    writeBlock(parentInode.blockAddresses[0], dirBuffer);
    return true;
}

bool FileSystem::copyFromHostFile(const string& hostPath, const string& destPath) {
    // 打开主机文件
    ifstream hostFile(hostPath, ios::binary);
    if (!hostFile) {
        cout << "错误: 无法打开主机文件: " << hostPath << endl;
        return false;
    }

    // 获取文件大小
    hostFile.seekg(0, ios::end);
    size_t fileSize = hostFile.tellg();
    hostFile.seekg(0, ios::beg);

    // 检查文件大小
    if (fileSize > BLOCK_SIZE * 10) {
        cout << "错误: 文件太大" << endl;
        return false;
    }

    // 读取文件内容
    vector<char> content(fileSize);
    hostFile.read(content.data(), fileSize);
    hostFile.close();

    // 准备目标路径
    string fullDestPath = getFullPath(destPath);
    if (isDirectory(fullDestPath)) {
        fullDestPath += "/" + getFileName(hostPath);
    }

    // 创建新文件
    int destInodeNo = allocateInode();
    if (destInodeNo == -1) {
        cout << "错误: 无法分配inode" << endl;
        return false;
    }

    // 初始化inode
    Inode destInode;
    destInode.fileType = TYPE_FILE;
    destInode.fileSize = fileSize;
    destInode.createTime = time(nullptr);
    destInode.modifyTime = time(nullptr);
    memset(destInode.blockAddresses, 0, sizeof(destInode.blockAddresses));

    // 分配并写入数据块
    int blocksNeeded = (fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t remainingSize = fileSize;

    for (int i = 0; i < blocksNeeded; i++) {
        int newBlock = allocateBlock();
        if (newBlock == -1) {
            cout << "错误: 无法分配数据块" << endl;
            // 清理已分配的资源
            for (int j = 0; j < i; j++) {
                freeBlock(destInode.blockAddresses[j]);
            }
            freeInode(destInodeNo);
            return false;
        }

        destInode.blockAddresses[i] = newBlock;

        // 准备块数据
        char buffer[BLOCK_SIZE] = { 0 };
        size_t blockSize = std::min<size_t>(remainingSize, static_cast<size_t>(BLOCK_SIZE));
        memcpy(buffer, content.data() + i * BLOCK_SIZE, blockSize);

        writeBlock(newBlock, buffer);
        remainingSize -= blockSize;
    }

    // 写入inode
    writeInode(destInodeNo, destInode);

    // 在父目录中添加目录项
    string destParentPath = fullDestPath.substr(0, fullDestPath.find_last_of('/'));
    string destFileName = getFileName(fullDestPath);

    int parentInodeNo = findInodeByPath(destParentPath);
    Inode parentInode = readInode(parentInodeNo);

    char dirBuffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], dirBuffer);
    DirEntry* entries = (DirEntry*)dirBuffer;

    bool entryAdded = false;
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo == 0) {
            entries[i].inodeNo = destInodeNo;
            //strncpy(entries[i].fileName, destFileName.c_str(), MAX_FILENAME - 1);
            //entries[i].fileName[MAX_FILENAME - 1] = '\0';
            strncpy_s(entries[i].fileName, MAX_FILENAME, destFileName.c_str(), MAX_FILENAME - 1);
            entryAdded = true;
            break;
        }
    }

    if (!entryAdded) {
        cout << "错误: 目录已满" << endl;
        // 清理已分配的资源
        for (int i = 0; i < blocksNeeded; i++) {
            freeBlock(destInode.blockAddresses[i]);
        }
        freeInode(destInodeNo);
        return false;
    }

    writeBlock(parentInode.blockAddresses[0], dirBuffer);
    return true;
}

bool FileSystem::copyToHostFile(const string& srcPath, const string& hostPath) {
    // 检查源文件
    string fullSrcPath = getFullPath(srcPath);
    int srcInodeNo = findInodeByPath(fullSrcPath);
    if (srcInodeNo == -1) {
        cout << "错误: 源文件不存在" << endl;
        return false;
    }

    Inode srcInode = readInode(srcInodeNo);
    if (srcInode.fileType != TYPE_FILE) {
        cout << "错误: 源路径不是文件" << endl;
        return false;
    }

    // 创建主机文件
    ofstream hostFile(hostPath, ios::binary);
    if (!hostFile) {
        cout << "错误: 无法创建主机文件: " << hostPath << endl;
        return false;
    }

    // 复制内容
    char buffer[BLOCK_SIZE];
    int blocksToRead = (srcInode.fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t remainingSize = srcInode.fileSize;

    for (int i = 0; i < blocksToRead; i++) {
        readBlock(srcInode.blockAddresses[i], buffer);
        size_t blockSize = std::min<size_t>(remainingSize, static_cast<size_t>(BLOCK_SIZE));
        hostFile.write(buffer, blockSize);
        remainingSize -= blockSize;
    }

    hostFile.close();
    return true;
}

// 辅助函数实现
string FileSystem::getFullPath(const string& path) {
    if (path[0] == '/') return path;
    if (currentPath == "/") return "/" + path;
    return currentPath + "/" + path;
}

string FileSystem::getFileName(const string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

bool FileSystem::isDirectory(const string& path) {
    int inodeNo = findInodeByPath(path);
    if (inodeNo == -1) return false;

    Inode inode = readInode(inodeNo);
    return inode.fileType == TYPE_DIR;
}


bool FileSystem::deleteFile(const string& path) {
    cout << "\n开始删除文件: " << path << endl;

    // 获取完整路径
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "完整路径: " << fullPath << endl;

    // 检查文件是否存在
    int inodeNo = findInodeByPath(fullPath);
    if (inodeNo == -1) {
        cout << "错误: 文件不存在: " << path << endl;
        return false;
    }

    // 检查是否是文件
    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_FILE) {
        cout << "错误: " << path << " 不是文件" << endl;
        return false;
    }

    // 获取父目录路径
    string parentPath;
    string fileName;
    size_t lastSlash = fullPath.find_last_of('/');
    if (lastSlash == 0) {
        parentPath = "/";
        fileName = fullPath.substr(1);
    }
    else {
        parentPath = fullPath.substr(0, lastSlash);
        fileName = fullPath.substr(lastSlash + 1);
    }

    cout << "父目录: " << parentPath << endl;
    cout << "文件名: " << fileName << endl;

    // 获取父目录inode
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "错误: 无法访问父目录" << endl;
        return false;
    }

    try {
        // 从父目录中删除目录项
        Inode parentInode = readInode(parentInodeNo);
        char buffer[BLOCK_SIZE];
        readBlock(parentInode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        bool found = false;
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 && strcmp(entries[i].fileName, fileName.c_str()) == 0) {
                // 清除目录项
                entries[i].inodeNo = 0;
                memset(entries[i].fileName, 0, MAX_FILENAME);
                found = true;
                cout << "已从父目录移除目录项" << endl;
                break;
            }
        }

        if (!found) {
            cout << "错误: 在父目录中未找到文件项" << endl;
            return false;
        }

        // 写回父目录块
        writeBlock(parentInode.blockAddresses[0], buffer);

        // 释放文件占用的数据块
        for (int i = 0; i < 10; i++) {
            if (inode.blockAddresses[i] != 0) {
                freeBlock(inode.blockAddresses[i]);
                cout << "释放数据块: " << inode.blockAddresses[i] << endl;
            }
        }

        // 释放inode
        freeInode(inodeNo);
        cout << "释放inode: " << inodeNo << endl;

        cout << "文件删除成功" << endl;
        return true;
    }
    catch (const std::exception& e) {
        cout << "删除文件时发生错误: " << e.what() << endl;
        return false;
    }
}


bool FileSystem::checkFileSystem() {
    cout << "\n开始检查文件系统..." << endl;
    bool needRepair = false;

    // 1. 检查超级块
    cout << "检查超级块..." << endl;
    if (!checkSuperBlock()) {
        cout << "错误: 超级块已损坏，无法修复" << endl;
        return false;
    }

    // 2. 检查inode位图
    cout << "检查inode位图..." << endl;
    if (!checkInodeBitmap()) {
        cout << "发现inode位图不一致" << endl;
        needRepair = true;
    }

    // 3. 检查数据块位图
    cout << "检查数据块位图..." << endl;
    if (!checkBlockBitmap()) {
        cout << "发现数据块位图不一致" << endl;
        needRepair = true;
    }

    // 4. 检查目录结构
    cout << "检查目录结构..." << endl;
    if (!checkDirectoryStructure()) {
        cout << "发现目录结构异常" << endl;
        needRepair = true;
    }

    // 如果发现问题，进行修复
    if (needRepair) {
        cout << "\n开始修复文件系统..." << endl;
        repairFileSystem();
        cout << "文件系统修复完成" << endl;
    }
    else {
        cout << "文件系统检查完成，未发现问题" << endl;
    }

    return true;
}

bool FileSystem::checkSuperBlock() {
    // 检查基本参数的合理性
    if (superBlock.blockSize != BLOCK_SIZE) {
        cout << "错误: 块大小不正确" << endl;
        return false;
    }

    if (superBlock.totalBlocks <= 0) {
        cout << "错误: 总块数无效" << endl;
        return false;
    }

    if (superBlock.inodeCount <= 0) {
        cout << "错误: inode数量无效" << endl;
        return false;
    }

    if (superBlock.freeBlockCount < 0 ||
        superBlock.freeBlockCount > superBlock.totalBlocks) {
        cout << "错误: 空闲块数量无效" << endl;
        return false;
    }

    if (superBlock.freeInodeCount < 0 ||
        superBlock.freeInodeCount > superBlock.inodeCount) {
        cout << "错误: 空闲inode数量无效" << endl;
        return false;
    }

    if (superBlock.firstDataBlock <= 0) {
        cout << "错误: 第一个数据块位置无效" << endl;
        return false;
    }

    return true;
}

bool FileSystem::checkInodeBitmap() {
    vector<bool> actualInodeUsage(superBlock.inodeCount, false);

    // 从根目录开始遍历整个文件系统
    queue<int> dirQueue;
    dirQueue.push(ROOT_INODE_NO);       //根节点为0
    actualInodeUsage[ROOT_INODE_NO] = true;

    while (!dirQueue.empty()) {
        int currentInodeNo = dirQueue.front();
        dirQueue.pop();

        Inode inode = readInode(currentInodeNo);
        if (inode.fileType != TYPE_DIR) continue;

        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                if (entries[i].inodeNo >= superBlock.inodeCount) {
                    cout << "错误: 无效的inode号: " << entries[i].inodeNo << endl;
                    continue;
                }

                actualInodeUsage[entries[i].inodeNo] = true;

                Inode entryInode = readInode(entries[i].inodeNo);
                if (entryInode.fileType == TYPE_DIR &&
                    strcmp(entries[i].fileName, ".") != 0 &&
                    strcmp(entries[i].fileName, "..") != 0) {
                    dirQueue.push(entries[i].inodeNo);
                }
            }
        }
    }

    // 检查位图一致性
    bool consistent = true;
    int actualFreeCount = 0;

    for (int i = 0; i < superBlock.inodeCount; i++) {
        if (getBit(inodeBitmap, i) != actualInodeUsage[i]) {
            cout << "inode " << i << " 位图标记不一致" << endl;
            consistent = false;
        }
        if (!actualInodeUsage[i]) {
            actualFreeCount++;
        }
    }

    if (actualFreeCount != superBlock.freeInodeCount) {
        cout << "空闲inode计数不一致: 实际=" << actualFreeCount
            << ", 记录=" << superBlock.freeInodeCount << endl;
        consistent = false;
    }

    return consistent;
}

bool FileSystem::checkBlockBitmap() {
    vector<bool> actualBlockUsage(superBlock.totalBlocks, false);

    // 标记系统区域
    for (int i = 0; i < superBlock.firstDataBlock; i++) {
        actualBlockUsage[i] = true;
    }
    // 遍历所有文件和目录的数据块
    markUsedBlocks(actualBlockUsage, ROOT_INODE_NO);

    // 检查位图一致性
    bool consistent = true;
    int actualFreeCount = 0;

    for (int i = 0; i < superBlock.totalBlocks; i++) {
        if (getBit(blockBitmap, i) != actualBlockUsage[i]) {
            cout << "数据块 " << i << " 位图标记不一致" << endl;
            consistent = false;
        }
        if (!actualBlockUsage[i]) {
            actualFreeCount++;
        }
    }

    if (actualFreeCount != superBlock.freeBlockCount) {
        cout << "空闲块计数不一致: 实际=" << actualFreeCount
            << ", 记录=" << superBlock.freeBlockCount << endl;
        consistent = false;
    }

    return consistent;
}

bool FileSystem::checkDirectoryStructure() {
    queue<int> dirQueue;
    dirQueue.push(ROOT_INODE_NO);
    bool consistent = true;

    while (!dirQueue.empty()) {
        int currentInodeNo = dirQueue.front();
        dirQueue.pop();

        Inode inode = readInode(currentInodeNo);
        if (inode.fileType != TYPE_DIR) continue;

        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        // 检查 "." 和 ".." 目录项
        bool hasDot = false, hasDotDot = false;

        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                if (strcmp(entries[i].fileName, ".") == 0) {
                    hasDot = true;
                    if (entries[i].inodeNo != currentInodeNo) {
                        cout << "错误: '.' 目录项指向错误的inode" << endl;
                        consistent = false;
                    }
                }
                else if (strcmp(entries[i].fileName, "..") == 0) {
                    hasDotDot = true;
                }
                else {
                    Inode entryInode = readInode(entries[i].inodeNo);
                    if (entryInode.fileType == TYPE_DIR) {
                        dirQueue.push(entries[i].inodeNo);
                    }
                }
            }
        }
        /*if (!hasDot || !hasDotDot) {
            cout << "错误: 目录缺少 '.' 或 '..' 项" << endl;
            consistent = false;
        }*/
    }

    return consistent;
}

void FileSystem::markUsedBlocks(vector<bool>& blockUsed, int inodeNo) {
    Inode inode = readInode(inodeNo);

    // 标记文件/目录使用的数据块
    for (int i = 0; i < 10; i++) {
        if (inode.blockAddresses[i] != 0) {
            if (inode.blockAddresses[i] < superBlock.totalBlocks) {
                blockUsed[inode.blockAddresses[i]] = true;
            }
        }
    }

    // 如果是目录，递归处理其中的文件
    if (inode.fileType == TYPE_DIR) {
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 &&
                strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                markUsedBlocks(blockUsed, entries[i].inodeNo);
            }
        }
    }
}

void FileSystem::repairFileSystem() {
    vector<bool> inodeUsed(superBlock.inodeCount, false);
    vector<bool> blockUsed(superBlock.totalBlocks, false);

    // 标记系统区域
    for (int i = 0; i < superBlock.firstDataBlock; i++) {
        blockUsed[i] = true;
    }

    // 从根目录开始标记所有正在使用的inode和数据块
    queue<int> queue;
    queue.push(ROOT_INODE_NO);
    inodeUsed[ROOT_INODE_NO] = true;

    while (!queue.empty()) {
        int currentInodeNo = queue.front();
        queue.pop();

        Inode inode = readInode(currentInodeNo);

        // 标记数据块
        for (int i = 0; i < 10; i++) {
            if (inode.blockAddresses[i] != 0 &&
                inode.blockAddresses[i] < superBlock.totalBlocks) {
                blockUsed[inode.blockAddresses[i]] = true;
            }
        }

        // 如果是目录，处理目录项
        if (inode.fileType == TYPE_DIR) {
            char buffer[BLOCK_SIZE];
            readBlock(inode.blockAddresses[0], buffer);
            DirEntry* entries = (DirEntry*)buffer;

            for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
                if (entries[i].inodeNo != 0 &&
                    entries[i].inodeNo < superBlock.inodeCount &&
                    strcmp(entries[i].fileName, ".") != 0 &&
                    strcmp(entries[i].fileName, "..") != 0) {
                    inodeUsed[entries[i].inodeNo] = true;
                    queue.push(entries[i].inodeNo);
                }
            }
        }
    }

    // 更新空闲计数
    int freeBlockCount = 0;
    int freeInodeCount = 0;

    // 更新inode位图和计数
    for (int i = 0; i < superBlock.inodeCount; i++) {
        setBit(inodeBitmap, i, inodeUsed[i]);
        if (!inodeUsed[i]) freeInodeCount++;
    }

    // 更新数据块位图和计数
    for (int i = 0; i < superBlock.totalBlocks; i++) {
        setBit(blockBitmap, i, blockUsed[i]);
        if (!blockUsed[i]) freeBlockCount++;
    }

    // 更新超级块中的空闲计数
    superBlock.freeBlockCount = freeBlockCount;
    superBlock.freeInodeCount = freeInodeCount;

    // 写回所有更改
    writeSuperBlock();
    writeInodeBitmap();
    writeBlockBitmap();
}


// 位图操作辅助方法
bool FileSystem::getBit(char* bitmap, int index) {
    int byteIndex = index / 8;
    int bitIndex = index % 8;
    return (bitmap[byteIndex] & (1 << bitIndex)) != 0;
}

void FileSystem::setBit(char* bitmap, int index, bool value) {
    int byteIndex = index / 8;
    int bitIndex = index % 8;
    if (value) {
        bitmap[byteIndex] |= (1 << bitIndex);
    }
    else {
        bitmap[byteIndex] &= ~(1 << bitIndex);
    }
}


void FileSystem::writeSuperBlock() {
    char buffer[BLOCK_SIZE];
    memset(buffer, 0, BLOCK_SIZE);
    memcpy(buffer, &superBlock, sizeof(SuperBlock));
    writeBlock(0, buffer);
}

void FileSystem::writeInodeBitmap() {
    int inodeBitmapBlocks = (superBlock.inodeCount + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    for (int i = 0; i < inodeBitmapBlocks; i++) {
        writeBlock(1 + i, inodeBitmap + i * BLOCK_SIZE);
    }
}

void FileSystem::writeBlockBitmap() {
    int blockBitmapBlocks = (superBlock.totalBlocks + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    int blockBitmapStart = 1 + (superBlock.inodeCount + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
    for (int i = 0; i < blockBitmapBlocks; i++) {
        writeBlock(blockBitmapStart + i, blockBitmap + i * BLOCK_SIZE);
    }
}


bool FileSystem::addUser(const UserInfo& user) {
    // 检查用户是否已存在
    for (const auto& existingUser : users) {
        if (existingUser.uid == user.uid ||
            strcmp(existingUser.username, user.username) == 0) {
            return false;
        }
    }
    users.push_back(user);
    return true;
}

bool FileSystem::login(const string& username, const string& password) {
    for (const auto& user : users) {
        if (strcmp(user.username, username.c_str()) == 0 &&
            strcmp(user.password, password.c_str()) == 0) {
            currentUid = user.uid;
            currentGid = user.gid;
            return true;
        }
    }
    return false;
}

bool FileSystem::checkAccess(int inodeNo, int accessType) {
    Inode inode = readInode(inodeNo);

    // 检查文件所有者权限
    if (inode.uid == currentUid) {
        if ((accessType == READ_PERMISSION && (inode.perm.owner & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.owner & WRITE_PERMISSION))) {
            return true;
        }
    }
    // 检查组权限
    else if (inode.gid == currentGid) {
        if ((accessType == READ_PERMISSION && (inode.perm.group & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.group & WRITE_PERMISSION))) {
            return true;
        }
    }
    // 检查其他用户权限
    else {
        if ((accessType == READ_PERMISSION && (inode.perm.other & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.other & WRITE_PERMISSION))) {
            return true;
        }
    }
    return false;
}

