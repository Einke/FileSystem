#pragma once
#include "common.h"
#include "superblock.h"
#include "inode.h"
#include <string>
#include <cstring>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <queue>

#define ROOT_INODE_NO 0

using namespace std;

class FileSystem {
public:
    FileSystem();
    ~FileSystem();

    // 基本文件系统操作
    bool format();                      // 格式化
    bool mount(const char* filename);   // 加载文件系统
    bool unmount();                     // 卸载文件系统
    string getSystemInfo() const;       //info命令
    bool checkFileSystem();             //check命令
    void initializeUserSystem();        //初始化用户
    // 目录操作
    bool createDir(const string& path);                     //创建目录
    bool deleteDir(const string& path, bool force = false); //删除目录
    bool changeDir(const string& path);                     //更换当前目录
    vector<string> listDir(bool recursive = false);         //显示目录信息
    // 文件操作
    bool createFile(const string& path, bool inputContent = false); //创建文件
    bool writeFileContent(int inodeNo, const string& content);      //写入文件内容
    bool catFile(const string& path);                               //打开文件
    bool copyFile(const string& srcPath, const string& destPath);   //复制文件
    bool deleteFile(const string& path);                            //删除文件
    // 第三阶段使用的接口
    bool addUser(const UserInfo& user);
    bool login(const string& username, const string& password);
    bool checkAccess(int inodeNo, int accessType);
    //其他操作
    int findInodeByPath(const string& path);        //根据路径查找i结点
    bool isDirEmpty(int inodeNo);                   //是否为空目录

private:
    FILE* diskFile;
    SuperBlock superBlock;
    char* blockBitmap;
    char* inodeBitmap;
    string currentPath;
    int currentInodeNo;

    int currentUid;
    int currentGid;
    vector<UserInfo> users;          // 用户列表
    int readerCount;                 // 当前读者数量
    HANDLE hReaderCountMutex;        // 读者计数互斥量
    HANDLE hWriteMutex;             // 写者互斥量

    // 内部辅助函数
    int allocateBlock();            //分配数据块
    void freeBlock(int blockNo);    //释放数据块
    int allocateInode();            //分配i结点
    void freeInode(int inodeNo);    //释放i结点
    void readBlock(int blockNo, char* buffer);       //读取数据块
    void writeBlock(int blockNo, const char* buffer);//写入数据块
    Inode readInode(int inodeNo);                    //读取i结点
    void writeInode(int inodeNo, const Inode& inode);//写入i结点
    vector<string> splitPath(const string& path);    //根据路径分隔开找到各级目录
    void recursiveDelete(int inodeNo);              //删除目录
    void listDirRecursive(int inodeNo, const string& path, vector<string>& result);
    string getFileInfo(const DirEntry& entry, const Inode& inode);  
    string getFormattedSize(size_t bytes) const;    
    int countUsedInodes() const;     //统计已用i结点
    int countUsedBlocks() const;     //统计已用数据块
    bool allocateFileBlocks(Inode& inode, size_t size);//根据大小分配数据块
    bool copyWithinFS(const string& srcPath, const string& destPath);
    bool copyFromHostFile(const string& hostPath, const string& destPath);
    bool copyToHostFile(const string& srcPath, const string& hostPath);
    // 路径处理辅助函数
    string getFullPath(const string& path);
    string getFileName(const string& path);
    bool isDirectory(const string& path);

    // 检查相关的私有方法
    bool checkSuperBlock();
    bool checkInodeBitmap();
    bool checkBlockBitmap();
    bool checkDirectoryStructure();
    void markUsedBlocks(vector<bool>& blockUsed, int inodeNo);
    void repairFileSystem();

    // 位图操作辅助方法
    bool getBit(char* bitmap, int index);
    void setBit(char* bitmap, int index, bool value);

    // 读写辅助方法
    void writeSuperBlock();
    void writeInodeBitmap();
    void writeBlockBitmap();
};
