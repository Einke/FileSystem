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
    // ����root�û�
    // ����Ĭ���û�
    UserInfo defaultUsers[] = {
        {0, 0, "root", "root"},      // ��������Ա
        {1, 1, "user1", "123456"},   // ��ͨ�û�1
        {2, 1, "user2", "123456"}    // ��ͨ�û�2
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

    // �����µĴ����ļ�
    // ʹ�� fopen_s ��� fopen
    errno_t err;
    if (fopen_s(&diskFile, "virtual_disk.img", "wb+") != 0) {
        cout << "�޷�������������ļ�" << endl;
        return false;
    }

    // �����̶���С���ļ�
    char* zeroBuffer = new char[BLOCK_SIZE]();
    for (int i = 0; i < DISK_SIZE / BLOCK_SIZE; i++) {
        fwrite(zeroBuffer, BLOCK_SIZE, 1, diskFile);
    }
    delete[] zeroBuffer;

    // �ص��ļ���ʼ��
    fseek(diskFile, 0, SEEK_SET);

    // ��ʼ��������
    superBlock.blockSize = BLOCK_SIZE;
    superBlock.totalBlocks = DISK_SIZE / BLOCK_SIZE;
    superBlock.inodeCount = 1024; // 1024��inode
    superBlock.freeBlockCount = superBlock.totalBlocks - 145; // ��ȥϵͳռ�õĿ�
    superBlock.freeInodeCount = superBlock.inodeCount - 1; // ��ȥ��Ŀ¼inode
    superBlock.firstDataBlock = 145; // 1(������) + 16(λͼ) + 128(inode��)
    // д�볬����
    fwrite(&superBlock, sizeof(SuperBlock), 1, diskFile);

    // ��ʼ��λͼ
    blockBitmap = new char[16 * BLOCK_SIZE]();
    inodeBitmap = new char[BLOCK_SIZE]();

    // ���ϵͳ����Ϊ��ʹ��
    for (int i = 0; i < 145; i++) {
        int byteIndex = i / 8;
        int bitIndex = i % 8;
        blockBitmap[byteIndex] |= (1 << bitIndex);
    }

    // ��Ǹ�Ŀ¼inodeΪ��ʹ��
    inodeBitmap[0] |= 1;

    // д��λͼ
    fwrite(blockBitmap, 16 * BLOCK_SIZE, 1, diskFile);
    fwrite(inodeBitmap, BLOCK_SIZE, 1, diskFile);

    // ��ʼ����Ŀ¼��inode
    Inode rootInode;
    memset(&rootInode, 0, sizeof(Inode));
    rootInode.fileType = TYPE_DIR;
    rootInode.createTime = time(nullptr);
    rootInode.modifyTime = rootInode.createTime;
    rootInode.uid = 0;  // root�û�
    rootInode.gid = 0;  // root��
    rootInode.perm = { 7, 5, 5 }; // rwxr-xr-x

    // �����Ŀ¼�����ݿ�
    int rootDataBlock = allocateBlock();
    if (rootDataBlock == -1) {
        cout << "�����Ŀ¼���ݿ�ʧ��" << endl;
        return false;
    }
    rootInode.blockAddresses[0] = rootDataBlock;

    // д���Ŀ¼inode
    writeInode(0, rootInode);

    // ��ʼ����Ŀ¼����
    char dirBuffer[BLOCK_SIZE] = { 0 };
    DirEntry* entries = (DirEntry*)dirBuffer;

    // ��� . �� .. Ŀ¼��
    // ʹ�� strcpy_s ��� strcpy
    strcpy_s(entries[0].fileName, sizeof(entries[0].fileName), ".");
    entries[0].inodeNo = 0;
    strcpy_s(entries[1].fileName, sizeof(entries[1].fileName), "..");
    entries[1].inodeNo = 0;

    writeBlock(rootDataBlock, dirBuffer);

    return true;
}

bool FileSystem::mount(const char* filename) {
    errno_t err;
    // �����Զ�д��ʽ�������ļ�
    if (fopen_s(&diskFile, filename, "rb+") != 0) {
        cout << "�ļ�ϵͳ�����ڣ����ڸ�ʽ��..." << endl;
        // ����ļ������ڣ����ʽ��
        if (!format()) {
            cout << "��ʽ��ʧ�ܣ�" << endl;
            return false;
        }
        // ���´��ļ�
        if (fopen_s(&diskFile, filename, "rb+") != 0) {
            cout << "�޷����ļ�ϵͳ��" << endl;
            return false;
        }
    }

    // ��ȡ������
    fseek(diskFile, 0, SEEK_SET);
    //fread(&superBlock, sizeof(SuperBlock), 1, diskFile);
    if (fread(&superBlock, sizeof(SuperBlock), 1, diskFile) != 1) {
        cout << "��ȡ������ʧ�ܣ�" << endl;
        return false;
    }

    // ��֤�ļ�ϵͳ
    if (superBlock.blockSize != BLOCK_SIZE ||
        superBlock.totalBlocks != DISK_SIZE / BLOCK_SIZE) {
        cout << "�ļ�ϵͳ��ʽ����ȷ��" << endl;
        return false;
    }

    // ��ȡλͼ
    blockBitmap = new char[16 * BLOCK_SIZE];
    inodeBitmap = new char[BLOCK_SIZE];

    if (fread(blockBitmap, 16 * BLOCK_SIZE, 1, diskFile) != 1 ||
        fread(inodeBitmap, BLOCK_SIZE, 1, diskFile) != 1) {
        cout << "��ȡλͼʧ�ܣ�" << endl;
        return false;
    }

    currentPath = "/";
    currentInodeNo = 0;

    return true;
}

bool FileSystem::unmount() {
    if (!diskFile) return false;

    // ���泬����
    fseek(diskFile, 0, SEEK_SET);
    fwrite(&superBlock, sizeof(SuperBlock), 1, diskFile);

    // ����λͼ
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

// ����һ�����п�
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

// �ͷ�һ����
void FileSystem::freeBlock(int blockNo) {
    int byteIndex = blockNo / 8;
    int bitIndex = blockNo % 8;
    blockBitmap[byteIndex] &= ~(1 << bitIndex);
    superBlock.freeBlockCount++;
}

// ����һ��inode
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

// �ͷ�һ��inode
void FileSystem::freeInode(int inodeNo) {
    int byteIndex = inodeNo / 8;
    int bitIndex = inodeNo % 8;
    inodeBitmap[byteIndex] &= ~(1 << bitIndex);
    superBlock.freeInodeCount++;
}

// ��ȡ���ݿ�
void FileSystem::readBlock(int blockNo, char* buffer) {
    fseek(diskFile, blockNo * BLOCK_SIZE, SEEK_SET);
    fread(buffer, BLOCK_SIZE, 1, diskFile);
}

// д�����ݿ�
void FileSystem::writeBlock(int blockNo, const char* buffer) {
    fseek(diskFile, blockNo * BLOCK_SIZE, SEEK_SET);
    fwrite(buffer, BLOCK_SIZE, 1, diskFile);
}

// ��ȡinode
Inode FileSystem::readInode(int inodeNo) {
    Inode inode;
    long offset = sizeof(SuperBlock) + 17 * BLOCK_SIZE + inodeNo * sizeof(Inode);
    fseek(diskFile, offset, SEEK_SET);
    fread(&inode, sizeof(Inode), 1, diskFile);
    return inode;
}

// д��inode
void FileSystem::writeInode(int inodeNo, const Inode& inode) {
    long offset = sizeof(SuperBlock) + 17 * BLOCK_SIZE + inodeNo * sizeof(Inode);
    fseek(diskFile, offset, SEEK_SET);
    fwrite(&inode, sizeof(Inode), 1, diskFile);
}


// ����һ���ִ���...

// �ָ�·��
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

// ����·������inode��
int FileSystem::findInodeByPath(const string& path) {
    if (path == "/") return 0;

    vector<string> parts = splitPath(path);
    int currentInode = currentInodeNo;  // �ӵ�ǰĿ¼��ʼ

    if (path[0] == '/') {
        currentInode = 0;  // ����·���Ӹ�Ŀ¼��ʼ
    }

    for (const auto& part : parts) {
        if (part == ".") continue;
        if (part == "..") {
            // ��ȡ��ǰĿ¼��inode
            Inode inode = readInode(currentInode);
            // ��ȡĿ¼����
            char buffer[BLOCK_SIZE];
            readBlock(inode.blockAddresses[0], buffer);
            DirEntry* entries = (DirEntry*)buffer;
            // ��ȡ��Ŀ¼��inode�ţ��ڶ���Ŀ¼�
            currentInode = entries[1].inodeNo;
            continue;
        }

        bool found = false;
        Inode inode = readInode(currentInode);

        // ��ȡĿ¼����
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        // ����Ŀ¼��
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

// ����Ŀ¼
bool FileSystem::createDir(const string& path) {
    // ��ȡ��Ŀ¼·������Ŀ¼��
    size_t pos = path.find_last_of('/');    //����·�������һ�� '/' ��λ��
    string parentPath = (pos == std::string::npos) ? "." : path.substr(0, pos);//����Ҳ��� '/'��pos == npos��
    string dirName = (pos == std::string::npos) ? path : path.substr(pos + 1);

    if (dirName.length() >= MAX_FILENAME) {
        cout << "Ŀ¼������" << endl;
        return false;
    }

    // ���Ҹ�Ŀ¼��inode
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "��Ŀ¼������" << endl;
        return false;
    }

    // ��ȡ����·��
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "����·��: " << fullPath << endl;

    // ����ļ��Ƿ��Ѵ���
    if (findInodeByPath(fullPath) != -1) {
        cout << "����: ��Ŀ¼�Ѵ���" << endl;
        return false;
    }

    // �����µ�inode
    int newInodeNo = allocateInode();
    if (newInodeNo == -1) {
        cout << "�޷�����inode" << endl;
        return false;
    }

    // �������ݿ�
    int newBlockNo = allocateBlock();
    if (newBlockNo == -1) {
        freeInode(newInodeNo);
        cout << "�޷��������ݿ�" << endl;
        return false;
    }

    // ��ʼ����Ŀ¼��inode
    Inode newInode;
    memset(&newInode, 0, sizeof(Inode));
    newInode.fileType = TYPE_DIR;
    newInode.createTime = time(nullptr);
    newInode.modifyTime = newInode.createTime;
    newInode.blockAddresses[0] = newBlockNo;
    newInode.uid = currentUid;
    newInode.gid = currentGid;
    newInode.perm = { 7, 5, 5 }; // rwxr-xr-x

    // д����Ŀ¼��inode
    writeInode(newInodeNo, newInode);

    // ��ʼ����Ŀ¼�����ݣ�. �� ..��
    char dirBuffer[BLOCK_SIZE] = { 0 };
    DirEntry* entries = (DirEntry*)dirBuffer;

    strcpy_s(entries[0].fileName, sizeof(entries[0].fileName), ".");
    entries[0].inodeNo = newInodeNo;
    strcpy_s(entries[1].fileName, sizeof(entries[1].fileName), "..");
    entries[1].inodeNo = parentInodeNo;
   

    writeBlock(newBlockNo, dirBuffer);

    // �ڸ�Ŀ¼�������Ŀ¼��
    Inode parentInode = readInode(parentInodeNo);
    char parentBuffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], parentBuffer);
    DirEntry* parentEntries = (DirEntry*)parentBuffer;

    // ���ҿ���Ŀ¼��
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (parentEntries[i].inodeNo == 0) {
            strcpy_s(parentEntries[i].fileName, sizeof(parentEntries[i].fileName), dirName.c_str());
            parentEntries[i].inodeNo = newInodeNo;
            writeBlock(parentInode.blockAddresses[0], parentBuffer);
            return true;
        }
    }
    
    cout << "��Ŀ¼����" << endl;
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

    // �ļ�ϵͳ������Ϣ
    ss << "\nSimDisk �ļ�ϵͳ��Ϣ\n"
        << "----------------------------------------\n"
        << "���С: " << getFormattedSize(superBlock.blockSize) << "\n"
        << "������: " << getFormattedSize(static_cast<size_t>(superBlock.totalBlocks) * BLOCK_SIZE) << "\n"
        << "\n";

    // ʹ�����
    int usedInodes = countUsedInodes();
    int usedBlocks = countUsedBlocks();

    ss << "Inode ʹ�����:\n"
        << "  ����: " << superBlock.inodeCount << "\n"
        << "  ����: " << usedInodes << "\n"
        << "  ����: " << (superBlock.inodeCount - usedInodes) << "\n"
        << "  ʹ����: " << std::fixed << std::setprecision(2)
        << (usedInodes * 100.0 / superBlock.inodeCount) << "%\n"
        << "\n";

    ss << "���ݿ�ʹ�����:\n"
        << "  ����: " << superBlock.totalBlocks << "\n"
        << "  ����: " << usedBlocks << "\n"
        << "  ����: " << (superBlock.totalBlocks - usedBlocks) << "\n"
        << "  ʹ����: " << std::fixed << std::setprecision(2)
        << (usedBlocks * 100.0 / superBlock.totalBlocks) << "%\n"
        << "\n";

    // �ļ�ϵͳ����
    ss << "�ļ�ϵͳ����:\n"
        << "  ������: 0 - " << sizeof(SuperBlock) << " �ֽ�\n"
        << "  ��λͼ: " << sizeof(SuperBlock) << " - "
        << (sizeof(SuperBlock) + 16 * BLOCK_SIZE) << " �ֽ�\n"
        << "  Inodeλͼ: " << (sizeof(SuperBlock) + 16 * BLOCK_SIZE) << " - "
        << (sizeof(SuperBlock) + 17 * BLOCK_SIZE) << " �ֽ�\n"
        << "  Inode��: " << (sizeof(SuperBlock) + 17 * BLOCK_SIZE) << " - "
        << superBlock.firstDataBlock * BLOCK_SIZE << " �ֽ�\n"
        << "  ������: " << superBlock.firstDataBlock * BLOCK_SIZE << " - "
        << superBlock.totalBlocks * BLOCK_SIZE << " �ֽ�\n";

    return ss.str();
}


// �ı䵱ǰĿ¼
bool FileSystem::changeDir(const string& path) {
    // ����ǵ�ǰĿ¼��.����ֱ�ӷ��سɹ�
    if (path == ".") {
        return true;  // �����ڵ�ǰĿ¼
    }

    int inodeNo = findInodeByPath(path);
    if (inodeNo == -1) {
        cout << "Ŀ¼������" << endl;
        return false;
    }

    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_DIR) {
        cout << "����Ŀ¼" << endl;
        return false;
    }

    // ����ɵ�״̬
    int oldInodeNo = currentInodeNo;
    string oldPath = currentPath;

    // ���Ը���״̬
    currentInodeNo = inodeNo;

    // ���µ�ǰ·��
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

    // ��֤��·���Ƿ���Ч
    if (findInodeByPath(currentPath) == -1) {
        // �����Ч���ָ���״̬
        currentInodeNo = oldInodeNo;
        currentPath = oldPath;
        return false;
    }

    return true;
}

// �г�Ŀ¼����
vector<string> FileSystem::listDir(bool recursive) {
    vector<string> result;

    if (!recursive) {
        // �ǵݹ�ʵ��
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
        // �ݹ��г�������Ŀ¼
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
    modTime = modTime.substr(0, modTime.length() - 1);  // �Ƴ����з�

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

    // ���Ŀ¼·������
    result.push_back("\n" + path + " ��Ŀ¼");
    result.push_back("----------------------------------------");

    //// �г���ǰĿ¼������
    //for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
    //    if (entries[i].inodeNo != 0) {
    //        Inode entryInode = readInode(entries[i].inodeNo);
    //        result.push_back(getFileInfo(entries[i], entryInode));

    //        // �ݹ鴦����Ŀ¼
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


    // ���г���ǰĿ¼�µ���������
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            Inode entryInode = readInode(entries[i].inodeNo);
            result.push_back(getFileInfo(entries[i], entryInode));
        }
    }
    // Ȼ�󵥶�������Ŀ¼�ĵݹ�
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            Inode entryInode = readInode(entries[i].inodeNo);
            // ֻ��Ŀ¼���ͽ��еݹ飬������ . �� ..
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
    // ����Ƿ�ֻ�� . �� .. ������Ŀ
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo != 0) {
            count++;
            if (strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                return false;
            }
        }
    }
    // ȷ��ֻ�� . �� .. ������Ŀ
    return count == 2;
}

void FileSystem::recursiveDelete(int inodeNo) {
    Inode inode = readInode(inodeNo);

    if (inode.fileType == TYPE_DIR) {
        char buffer[BLOCK_SIZE];
        readBlock(inode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        // �ݹ�ɾ��������Ŀ
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 &&
                strcmp(entries[i].fileName, ".") != 0 &&
                strcmp(entries[i].fileName, "..") != 0) {
                recursiveDelete(entries[i].inodeNo);
            }
        }
    }

    // �ͷ����ݿ�
    for (int i = 0; i < 10; i++) {
        if (inode.blockAddresses[i] != 0) {
            freeBlock(inode.blockAddresses[i]);
        }
    }

    // �ͷ�inode
    freeInode(inodeNo);
}

bool FileSystem::deleteDir(const string& path, bool force) {
    cout << "\n��ʼɾ��Ŀ¼: " << path << endl;

    // �������
    if (path == "/" || path == "." || path == "..") {
        cout << "����: ����ɾ������Ŀ¼" << endl;
        return false;
    }

    // ��ȡ����·��
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "����·��: " << fullPath << endl;

    int inodeNo = findInodeByPath(fullPath);
    if (inodeNo == -1) {
        cout << "����: Ŀ¼������: " << fullPath << endl;
        return false;
    }
    cout << "�ҵ�Ŀ¼inode��: " << inodeNo << endl;

    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_DIR) {
        cout << "����: ����Ŀ¼" << endl;
        return false;
    }

    // ����Ƿ�Ϊ��Ŀ¼
    bool isEmpty = isDirEmpty(inodeNo);
    cout << "Ŀ¼�Ƿ�Ϊ��: " << (isEmpty ? "��" : "��") << endl;

    if (!isEmpty && !force) {
        cout << "Ŀ¼��Ϊ��" << endl;
        return false;
    }

    try {
        // ��ȡ��Ŀ¼·��
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

        cout << "��Ŀ¼·��: " << parentPath << endl;
        cout << "Ŀ¼��: " << dirName << endl;

        int parentInodeNo = findInodeByPath(parentPath);
        if (parentInodeNo == -1) {
            cout << "����: �Ҳ�����Ŀ¼" << endl;
            return false;
        }
        cout << "��Ŀ¼inode��: " << parentInodeNo << endl;

        // �Ӹ�Ŀ¼��ɾ����Ŀ¼��
        Inode parentInode = readInode(parentInodeNo);
        char buffer[BLOCK_SIZE];
        readBlock(parentInode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        bool found = false;
        cout << "��ʼ�ڸ�Ŀ¼�в���Ŀ¼��..." << endl;
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                cout << "���Ŀ¼�� " << i << ": " << entries[i].fileName
                    << " (inode: " << entries[i].inodeNo << ")" << endl;

                if (strcmp(entries[i].fileName, dirName.c_str()) == 0) {
                    cout << "�ҵ�Ŀ��Ŀ¼�λ��: " << i << endl;
                    int targetInodeNo = entries[i].inodeNo;

                    // ���Ŀ¼��
                    entries[i].inodeNo = 0;
                    memset(entries[i].fileName, 0, MAX_FILENAME);
                    found = true;

                    // д�ظ�Ŀ¼�ĸ���
                    writeBlock(parentInode.blockAddresses[0], buffer);
                    cout << "�ѴӸ�Ŀ¼�Ƴ�Ŀ¼��" << endl;

                    // �ݹ�ɾ��Ŀ¼����
                    recursiveDelete(targetInodeNo);
                    cout << "�ݹ�ɾ�����" << endl;
                    break;
                }
            }
        }

        if (!found) {
            cout << "����: �ڸ�Ŀ¼��δ�ҵ�Ŀ¼��" << endl;
            return false;
        }

        cout << "Ŀ¼ɾ���ɹ�" << endl;
        return true;
    }
    catch (const std::exception& e) {
        cout << "ɾ��Ŀ¼ʱ��������: " << e.what() << endl;
        return false;
    }
}


bool FileSystem::createFile(const string& path, bool inputContent) {
    cout << "��ʼ�����ļ�: " << path << endl;

    // �������
    if (path.empty()) {
        cout << "����: �ļ�·��Ϊ��" << endl;
        return false;
    }

    // ����ļ����Ƿ�Ϸ������ܰ��������ַ���
    string invalidChars = "\\/:*?\"<>|";
    if (path.find_first_of(invalidChars) != string::npos) {
        cout << "����: �ļ��������Ƿ��ַ�" << endl;
        return false;
    }

    // ��ȡ����·��
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "����·��: " << fullPath << endl;

    // ����ļ��Ƿ��Ѵ���
    if (findInodeByPath(fullPath) != -1) {
        cout << "����: �ļ��Ѵ���" << endl;
        return false;
    }

    // ��ȡ��Ŀ¼·�����ļ���
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

    // ����ļ�������
    if (fileName.length() >= MAX_FILENAME) {
        cout << "����: �ļ�������" << endl;
        return false;
    }

    // ��鸸Ŀ¼�Ƿ����
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "����: ��Ŀ¼������" << endl;
        return false;
    }

    // ��鸸Ŀ¼�Ƿ���Ŀ¼
    Inode parentInode = readInode(parentInodeNo);
    if (parentInode.fileType != TYPE_DIR) {
        cout << "����: ��·������Ŀ¼" << endl;
        return false;
    }

    // ����Ƿ����㹻��inode 
    int newInodeNo = allocateInode();
    if (newInodeNo == -1) {
        cout << "����: û�п��е�inode" << endl;
        return false;
    }

    // ��ʼ��inode
    Inode newInode;
    newInode.fileType = TYPE_FILE;
    newInode.fileSize = 0;
    newInode.createTime = time(nullptr);
    newInode.modifyTime = newInode.createTime;
    memset(newInode.blockAddresses, 0, sizeof(newInode.blockAddresses));

    // д����inode
    writeInode(newInodeNo, newInode);
    cout << "�ѷ���inode��: " << newInodeNo << endl;

    // �ڸ�Ŀ¼�����Ŀ¼��
    char buffer[BLOCK_SIZE];
    readBlock(parentInode.blockAddresses[0], buffer);
    DirEntry* entries = (DirEntry*)buffer;

    // ���ҿ���Ŀ¼��
    bool entryAdded = false;
    for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
        if (entries[i].inodeNo == 0) {
            entries[i].inodeNo = newInodeNo;
            strncpy_s(entries[i].fileName, fileName.c_str(), MAX_FILENAME - 1);
            entries[i].fileName[MAX_FILENAME - 1] = '\0';
            entryAdded = true;
            cout << "���ڸ�Ŀ¼���Ŀ¼��" << endl;
            break;
        }
    }

    if (!entryAdded) {
        cout << "����: ��Ŀ¼����" << endl;
        freeInode(newInodeNo);
        return false;
    }

    // д�ظ�Ŀ¼��
    writeBlock(parentInode.blockAddresses[0], buffer);

    // �����Ҫ��������
    if (inputContent) {
        cout << "�������ļ����ݣ����� :wq ���沢�˳�����" << endl;
        string content, line;
        while (getline(cin, line)) {
            if (line == ":wq") {
                break;
            }
            content += line + "\n";
        }

        if (!writeFileContent(newInodeNo, content)) {
            cout << "����: д���ļ�����ʧ��" << endl;
            // �����Ѵ������ļ�
            entries[entryAdded].inodeNo = 0;
            memset(entries[entryAdded].fileName, 0, MAX_FILENAME);
            writeBlock(parentInode.blockAddresses[0], buffer);
            freeInode(newInodeNo);
            return false;
        }
        cout << "�ļ�����д��ɹ�" << endl;
    }

    cout << "�ļ������ɹ�" << endl;
    return true;
}

bool FileSystem::writeFileContent(int inodeNo, const string& content) {
    Inode inode = readInode(inodeNo);

    // ������Ҫ�Ŀ���
    size_t contentSize = content.length();
    size_t blocksNeeded = (contentSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // ��������Ŀ�
    if (!allocateFileBlocks(inode, contentSize)) {
        return false;
    }

    // д������
    size_t written = 0;
    for (size_t i = 0; i < blocksNeeded && written < contentSize; i++) {
        char buffer[BLOCK_SIZE] = { 0 };
        size_t toWrite = std::min<size_t>(BLOCK_SIZE, contentSize - written);
        memcpy(buffer, content.c_str() + written, toWrite);
        writeBlock(inode.blockAddresses[i], buffer);
        written += toWrite;
    }

    // ����inode
    inode.fileSize = contentSize;
    inode.modifyTime = time(nullptr);
    writeInode(inodeNo, inode);

    return true;
}

bool FileSystem::allocateFileBlocks(Inode& inode, size_t size) {
    size_t blocksNeeded = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocksNeeded > 10) {  // ��ʱֻ֧��ֱ�ӿ�
        return false;
    }

    // �����¿�
    for (size_t i = 0; i < blocksNeeded; i++) {
        if (inode.blockAddresses[i] == 0) {
            int newBlock = allocateBlock();
            if (newBlock == -1) {
                // ����ʧ�ܣ��ͷ��ѷ���Ŀ�
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
    // ��ȡ����·��
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "����·��: " << fullPath << endl;

    // �����ļ�
    int inodeNo = findInodeByPath(fullPath);
    cout << "�ҵ�inode��: " << inodeNo << endl;

    if (inodeNo == -1) {
        cout << "����: �ļ�������: " << path << endl;
        return false;
    }

    // ��ȡinode
    Inode inode = readInode(inodeNo);
    cout << "�ļ�����: " << inode.fileType << endl;
    cout << "�ļ���С: " << inode.fileSize << " �ֽ�" << endl;

    if (inode.fileType != TYPE_FILE) {
        cout << "����: " << path << " �����ļ�" << endl;
        return false;
    }

    // ������Ҫ��ȡ�Ŀ���
    int blocksNeeded = (inode.fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // ��ȡ����ʾ�ļ�����
    cout << "\n�ļ� " << path << " �����ݣ�" << endl;
    cout << std::string(40, '-') << endl;

    size_t totalRead = 0;
    char buffer[BLOCK_SIZE];

    for (int i = 0; i < blocksNeeded && totalRead < inode.fileSize; i++) {
        if (inode.blockAddresses[i] == 0) {
            cout << "����: �ļ����ݿ���" << endl;
            return false;
        }

        // ��ȡ���ݿ�
        readBlock(inode.blockAddresses[i], buffer);

        // �����������Ҫ��ʾ�����ֽ�
        size_t bytesToShow = std::min<size_t>(BLOCK_SIZE, inode.fileSize - totalRead);

        // ��ʾ����
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
    cout << "\n��ʼ�����ļ�..." << endl;
    cout << "Դ�ļ�: " << srcPath << endl;
    cout << "Ŀ���ļ�: " << destPath << endl;

    bool isFromHost = srcPath.substr(0, 6) == "<host>";
    bool isToHost = destPath.substr(0, 6) == "<host>";

    try {
        if (isFromHost && isToHost) {
            cout << "����: ��֧���������ļ�ϵͳ�临��" << endl;
            return false;
        }
        else if (isFromHost) {
            // ���������Ƶ�ģ���ļ�ϵͳ
            return copyFromHostFile(srcPath.substr(6), destPath);
        }
        else if (isToHost) {
            // ��ģ���ļ�ϵͳ���Ƶ�����
            return copyToHostFile(srcPath, destPath.substr(6));
        }
        else {
            // ��ģ���ļ�ϵͳ�ڲ�����
            return copyWithinFS(srcPath, destPath);
        }
    }
    catch (const std::exception& e) {
        cout << "���ƹ����з�������: " << e.what() << endl;
        return false;
    }
}

bool FileSystem::copyWithinFS(const string& srcPath, const string& destPath) {
    // ��ȡ����·��
    string fullSrcPath = getFullPath(srcPath);
    string fullDestPath = getFullPath(destPath);

    // ���Դ�ļ�
    int srcInodeNo = findInodeByPath(fullSrcPath);
    if (srcInodeNo == -1) {
        cout << "����: Դ�ļ�������: " << srcPath << endl;
        return false;
    }

    // ��ȡԴ�ļ�inode
    Inode srcInode = readInode(srcInodeNo);
    if (srcInode.fileType != TYPE_FILE) {
        cout << "����: Դ·�������ļ�" << endl;
        return false;
    }

    // ����Ŀ��·��
    if (isDirectory(fullDestPath)) {
        fullDestPath += "/" + getFileName(fullSrcPath);
    }

    // ���Ŀ���ļ��Ƿ��Ѵ���
    if (findInodeByPath(fullDestPath) != -1) {
        cout << "����: Ŀ���ļ��Ѵ���" << endl;
        return false;
    }

    // ����Ŀ���ļ�
    int destInodeNo = allocateInode();
    if (destInodeNo == -1) {
        cout << "����: �޷�����inode" << endl;
        return false;
    }

    // ��ʼ��Ŀ���ļ�inode
    Inode destInode;
    destInode.fileType = TYPE_FILE;
    destInode.fileSize = 0;
    destInode.createTime = time(nullptr);
    destInode.modifyTime = time(nullptr);
    memset(destInode.blockAddresses, 0, sizeof(destInode.blockAddresses));

    // �����ļ�����
    char buffer[BLOCK_SIZE];
    int blocksNeeded = (srcInode.fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (int i = 0; i < blocksNeeded; i++) {
        // ��ȡԴ��
        readBlock(srcInode.blockAddresses[i], buffer);

        // ΪĿ���ļ������¿�
        int newBlock = allocateBlock();
        if (newBlock == -1) {
            cout << "����: �޷��������ݿ�" << endl;
            // �����ѷ������Դ
            for (int j = 0; j < i; j++) {
                freeBlock(destInode.blockAddresses[j]);
            }
            freeInode(destInodeNo);
            return false;
        }

        destInode.blockAddresses[i] = newBlock;
        writeBlock(newBlock, buffer);
    }

    // ����Ŀ���ļ�inode
    destInode.fileSize = srcInode.fileSize;
    writeInode(destInodeNo, destInode);

    // �ڸ�Ŀ¼�����Ŀ���ļ���Ŀ¼��
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
        cout << "����: Ŀ¼����" << endl;
        // �����ѷ������Դ
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
    // �������ļ�
    ifstream hostFile(hostPath, ios::binary);
    if (!hostFile) {
        cout << "����: �޷��������ļ�: " << hostPath << endl;
        return false;
    }

    // ��ȡ�ļ���С
    hostFile.seekg(0, ios::end);
    size_t fileSize = hostFile.tellg();
    hostFile.seekg(0, ios::beg);

    // ����ļ���С
    if (fileSize > BLOCK_SIZE * 10) {
        cout << "����: �ļ�̫��" << endl;
        return false;
    }

    // ��ȡ�ļ�����
    vector<char> content(fileSize);
    hostFile.read(content.data(), fileSize);
    hostFile.close();

    // ׼��Ŀ��·��
    string fullDestPath = getFullPath(destPath);
    if (isDirectory(fullDestPath)) {
        fullDestPath += "/" + getFileName(hostPath);
    }

    // �������ļ�
    int destInodeNo = allocateInode();
    if (destInodeNo == -1) {
        cout << "����: �޷�����inode" << endl;
        return false;
    }

    // ��ʼ��inode
    Inode destInode;
    destInode.fileType = TYPE_FILE;
    destInode.fileSize = fileSize;
    destInode.createTime = time(nullptr);
    destInode.modifyTime = time(nullptr);
    memset(destInode.blockAddresses, 0, sizeof(destInode.blockAddresses));

    // ���䲢д�����ݿ�
    int blocksNeeded = (fileSize + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t remainingSize = fileSize;

    for (int i = 0; i < blocksNeeded; i++) {
        int newBlock = allocateBlock();
        if (newBlock == -1) {
            cout << "����: �޷��������ݿ�" << endl;
            // �����ѷ������Դ
            for (int j = 0; j < i; j++) {
                freeBlock(destInode.blockAddresses[j]);
            }
            freeInode(destInodeNo);
            return false;
        }

        destInode.blockAddresses[i] = newBlock;

        // ׼��������
        char buffer[BLOCK_SIZE] = { 0 };
        size_t blockSize = std::min<size_t>(remainingSize, static_cast<size_t>(BLOCK_SIZE));
        memcpy(buffer, content.data() + i * BLOCK_SIZE, blockSize);

        writeBlock(newBlock, buffer);
        remainingSize -= blockSize;
    }

    // д��inode
    writeInode(destInodeNo, destInode);

    // �ڸ�Ŀ¼�����Ŀ¼��
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
        cout << "����: Ŀ¼����" << endl;
        // �����ѷ������Դ
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
    // ���Դ�ļ�
    string fullSrcPath = getFullPath(srcPath);
    int srcInodeNo = findInodeByPath(fullSrcPath);
    if (srcInodeNo == -1) {
        cout << "����: Դ�ļ�������" << endl;
        return false;
    }

    Inode srcInode = readInode(srcInodeNo);
    if (srcInode.fileType != TYPE_FILE) {
        cout << "����: Դ·�������ļ�" << endl;
        return false;
    }

    // ���������ļ�
    ofstream hostFile(hostPath, ios::binary);
    if (!hostFile) {
        cout << "����: �޷����������ļ�: " << hostPath << endl;
        return false;
    }

    // ��������
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

// ��������ʵ��
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
    cout << "\n��ʼɾ���ļ�: " << path << endl;

    // ��ȡ����·��
    string fullPath = path;
    if (path[0] != '/') {
        if (currentPath == "/") {
            fullPath = "/" + path;
        }
        else {
            fullPath = currentPath + "/" + path;
        }
    }
    cout << "����·��: " << fullPath << endl;

    // ����ļ��Ƿ����
    int inodeNo = findInodeByPath(fullPath);
    if (inodeNo == -1) {
        cout << "����: �ļ�������: " << path << endl;
        return false;
    }

    // ����Ƿ����ļ�
    Inode inode = readInode(inodeNo);
    if (inode.fileType != TYPE_FILE) {
        cout << "����: " << path << " �����ļ�" << endl;
        return false;
    }

    // ��ȡ��Ŀ¼·��
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

    cout << "��Ŀ¼: " << parentPath << endl;
    cout << "�ļ���: " << fileName << endl;

    // ��ȡ��Ŀ¼inode
    int parentInodeNo = findInodeByPath(parentPath);
    if (parentInodeNo == -1) {
        cout << "����: �޷����ʸ�Ŀ¼" << endl;
        return false;
    }

    try {
        // �Ӹ�Ŀ¼��ɾ��Ŀ¼��
        Inode parentInode = readInode(parentInodeNo);
        char buffer[BLOCK_SIZE];
        readBlock(parentInode.blockAddresses[0], buffer);
        DirEntry* entries = (DirEntry*)buffer;

        bool found = false;
        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0 && strcmp(entries[i].fileName, fileName.c_str()) == 0) {
                // ���Ŀ¼��
                entries[i].inodeNo = 0;
                memset(entries[i].fileName, 0, MAX_FILENAME);
                found = true;
                cout << "�ѴӸ�Ŀ¼�Ƴ�Ŀ¼��" << endl;
                break;
            }
        }

        if (!found) {
            cout << "����: �ڸ�Ŀ¼��δ�ҵ��ļ���" << endl;
            return false;
        }

        // д�ظ�Ŀ¼��
        writeBlock(parentInode.blockAddresses[0], buffer);

        // �ͷ��ļ�ռ�õ����ݿ�
        for (int i = 0; i < 10; i++) {
            if (inode.blockAddresses[i] != 0) {
                freeBlock(inode.blockAddresses[i]);
                cout << "�ͷ����ݿ�: " << inode.blockAddresses[i] << endl;
            }
        }

        // �ͷ�inode
        freeInode(inodeNo);
        cout << "�ͷ�inode: " << inodeNo << endl;

        cout << "�ļ�ɾ���ɹ�" << endl;
        return true;
    }
    catch (const std::exception& e) {
        cout << "ɾ���ļ�ʱ��������: " << e.what() << endl;
        return false;
    }
}


bool FileSystem::checkFileSystem() {
    cout << "\n��ʼ����ļ�ϵͳ..." << endl;
    bool needRepair = false;

    // 1. ��鳬����
    cout << "��鳬����..." << endl;
    if (!checkSuperBlock()) {
        cout << "����: ���������𻵣��޷��޸�" << endl;
        return false;
    }

    // 2. ���inodeλͼ
    cout << "���inodeλͼ..." << endl;
    if (!checkInodeBitmap()) {
        cout << "����inodeλͼ��һ��" << endl;
        needRepair = true;
    }

    // 3. ������ݿ�λͼ
    cout << "������ݿ�λͼ..." << endl;
    if (!checkBlockBitmap()) {
        cout << "�������ݿ�λͼ��һ��" << endl;
        needRepair = true;
    }

    // 4. ���Ŀ¼�ṹ
    cout << "���Ŀ¼�ṹ..." << endl;
    if (!checkDirectoryStructure()) {
        cout << "����Ŀ¼�ṹ�쳣" << endl;
        needRepair = true;
    }

    // ����������⣬�����޸�
    if (needRepair) {
        cout << "\n��ʼ�޸��ļ�ϵͳ..." << endl;
        repairFileSystem();
        cout << "�ļ�ϵͳ�޸����" << endl;
    }
    else {
        cout << "�ļ�ϵͳ�����ɣ�δ��������" << endl;
    }

    return true;
}

bool FileSystem::checkSuperBlock() {
    // �����������ĺ�����
    if (superBlock.blockSize != BLOCK_SIZE) {
        cout << "����: ���С����ȷ" << endl;
        return false;
    }

    if (superBlock.totalBlocks <= 0) {
        cout << "����: �ܿ�����Ч" << endl;
        return false;
    }

    if (superBlock.inodeCount <= 0) {
        cout << "����: inode������Ч" << endl;
        return false;
    }

    if (superBlock.freeBlockCount < 0 ||
        superBlock.freeBlockCount > superBlock.totalBlocks) {
        cout << "����: ���п�������Ч" << endl;
        return false;
    }

    if (superBlock.freeInodeCount < 0 ||
        superBlock.freeInodeCount > superBlock.inodeCount) {
        cout << "����: ����inode������Ч" << endl;
        return false;
    }

    if (superBlock.firstDataBlock <= 0) {
        cout << "����: ��һ�����ݿ�λ����Ч" << endl;
        return false;
    }

    return true;
}

bool FileSystem::checkInodeBitmap() {
    vector<bool> actualInodeUsage(superBlock.inodeCount, false);

    // �Ӹ�Ŀ¼��ʼ���������ļ�ϵͳ
    queue<int> dirQueue;
    dirQueue.push(ROOT_INODE_NO);       //���ڵ�Ϊ0
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
                    cout << "����: ��Ч��inode��: " << entries[i].inodeNo << endl;
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

    // ���λͼһ����
    bool consistent = true;
    int actualFreeCount = 0;

    for (int i = 0; i < superBlock.inodeCount; i++) {
        if (getBit(inodeBitmap, i) != actualInodeUsage[i]) {
            cout << "inode " << i << " λͼ��ǲ�һ��" << endl;
            consistent = false;
        }
        if (!actualInodeUsage[i]) {
            actualFreeCount++;
        }
    }

    if (actualFreeCount != superBlock.freeInodeCount) {
        cout << "����inode������һ��: ʵ��=" << actualFreeCount
            << ", ��¼=" << superBlock.freeInodeCount << endl;
        consistent = false;
    }

    return consistent;
}

bool FileSystem::checkBlockBitmap() {
    vector<bool> actualBlockUsage(superBlock.totalBlocks, false);

    // ���ϵͳ����
    for (int i = 0; i < superBlock.firstDataBlock; i++) {
        actualBlockUsage[i] = true;
    }
    // ���������ļ���Ŀ¼�����ݿ�
    markUsedBlocks(actualBlockUsage, ROOT_INODE_NO);

    // ���λͼһ����
    bool consistent = true;
    int actualFreeCount = 0;

    for (int i = 0; i < superBlock.totalBlocks; i++) {
        if (getBit(blockBitmap, i) != actualBlockUsage[i]) {
            cout << "���ݿ� " << i << " λͼ��ǲ�һ��" << endl;
            consistent = false;
        }
        if (!actualBlockUsage[i]) {
            actualFreeCount++;
        }
    }

    if (actualFreeCount != superBlock.freeBlockCount) {
        cout << "���п������һ��: ʵ��=" << actualFreeCount
            << ", ��¼=" << superBlock.freeBlockCount << endl;
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

        // ��� "." �� ".." Ŀ¼��
        bool hasDot = false, hasDotDot = false;

        for (int i = 0; i < BLOCK_SIZE / sizeof(DirEntry); i++) {
            if (entries[i].inodeNo != 0) {
                if (strcmp(entries[i].fileName, ".") == 0) {
                    hasDot = true;
                    if (entries[i].inodeNo != currentInodeNo) {
                        cout << "����: '.' Ŀ¼��ָ������inode" << endl;
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
            cout << "����: Ŀ¼ȱ�� '.' �� '..' ��" << endl;
            consistent = false;
        }*/
    }

    return consistent;
}

void FileSystem::markUsedBlocks(vector<bool>& blockUsed, int inodeNo) {
    Inode inode = readInode(inodeNo);

    // ����ļ�/Ŀ¼ʹ�õ����ݿ�
    for (int i = 0; i < 10; i++) {
        if (inode.blockAddresses[i] != 0) {
            if (inode.blockAddresses[i] < superBlock.totalBlocks) {
                blockUsed[inode.blockAddresses[i]] = true;
            }
        }
    }

    // �����Ŀ¼���ݹ鴦�����е��ļ�
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

    // ���ϵͳ����
    for (int i = 0; i < superBlock.firstDataBlock; i++) {
        blockUsed[i] = true;
    }

    // �Ӹ�Ŀ¼��ʼ�����������ʹ�õ�inode�����ݿ�
    queue<int> queue;
    queue.push(ROOT_INODE_NO);
    inodeUsed[ROOT_INODE_NO] = true;

    while (!queue.empty()) {
        int currentInodeNo = queue.front();
        queue.pop();

        Inode inode = readInode(currentInodeNo);

        // ������ݿ�
        for (int i = 0; i < 10; i++) {
            if (inode.blockAddresses[i] != 0 &&
                inode.blockAddresses[i] < superBlock.totalBlocks) {
                blockUsed[inode.blockAddresses[i]] = true;
            }
        }

        // �����Ŀ¼������Ŀ¼��
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

    // ���¿��м���
    int freeBlockCount = 0;
    int freeInodeCount = 0;

    // ����inodeλͼ�ͼ���
    for (int i = 0; i < superBlock.inodeCount; i++) {
        setBit(inodeBitmap, i, inodeUsed[i]);
        if (!inodeUsed[i]) freeInodeCount++;
    }

    // �������ݿ�λͼ�ͼ���
    for (int i = 0; i < superBlock.totalBlocks; i++) {
        setBit(blockBitmap, i, blockUsed[i]);
        if (!blockUsed[i]) freeBlockCount++;
    }

    // ���³������еĿ��м���
    superBlock.freeBlockCount = freeBlockCount;
    superBlock.freeInodeCount = freeInodeCount;

    // д�����и���
    writeSuperBlock();
    writeInodeBitmap();
    writeBlockBitmap();
}


// λͼ������������
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
    // ����û��Ƿ��Ѵ���
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

    // ����ļ�������Ȩ��
    if (inode.uid == currentUid) {
        if ((accessType == READ_PERMISSION && (inode.perm.owner & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.owner & WRITE_PERMISSION))) {
            return true;
        }
    }
    // �����Ȩ��
    else if (inode.gid == currentGid) {
        if ((accessType == READ_PERMISSION && (inode.perm.group & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.group & WRITE_PERMISSION))) {
            return true;
        }
    }
    // ��������û�Ȩ��
    else {
        if ((accessType == READ_PERMISSION && (inode.perm.other & READ_PERMISSION)) ||
            (accessType == WRITE_PERMISSION && (inode.perm.other & WRITE_PERMISSION))) {
            return true;
        }
    }
    return false;
}

