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

    // �����ļ�ϵͳ����
    bool format();                      // ��ʽ��
    bool mount(const char* filename);   // �����ļ�ϵͳ
    bool unmount();                     // ж���ļ�ϵͳ
    string getSystemInfo() const;       //info����
    bool checkFileSystem();             //check����
    void initializeUserSystem();        //��ʼ���û�
    // Ŀ¼����
    bool createDir(const string& path);                     //����Ŀ¼
    bool deleteDir(const string& path, bool force = false); //ɾ��Ŀ¼
    bool changeDir(const string& path);                     //������ǰĿ¼
    vector<string> listDir(bool recursive = false);         //��ʾĿ¼��Ϣ
    // �ļ�����
    bool createFile(const string& path, bool inputContent = false); //�����ļ�
    bool writeFileContent(int inodeNo, const string& content);      //д���ļ�����
    bool catFile(const string& path);                               //���ļ�
    bool copyFile(const string& srcPath, const string& destPath);   //�����ļ�
    bool deleteFile(const string& path);                            //ɾ���ļ�
    // �����׶�ʹ�õĽӿ�
    bool addUser(const UserInfo& user);
    bool login(const string& username, const string& password);
    bool checkAccess(int inodeNo, int accessType);
    //��������
    int findInodeByPath(const string& path);        //����·������i���
    bool isDirEmpty(int inodeNo);                   //�Ƿ�Ϊ��Ŀ¼

private:
    FILE* diskFile;
    SuperBlock superBlock;
    char* blockBitmap;
    char* inodeBitmap;
    string currentPath;
    int currentInodeNo;

    int currentUid;
    int currentGid;
    vector<UserInfo> users;          // �û��б�
    int readerCount;                 // ��ǰ��������
    HANDLE hReaderCountMutex;        // ���߼���������
    HANDLE hWriteMutex;             // д�߻�����

    // �ڲ���������
    int allocateBlock();            //�������ݿ�
    void freeBlock(int blockNo);    //�ͷ����ݿ�
    int allocateInode();            //����i���
    void freeInode(int inodeNo);    //�ͷ�i���
    void readBlock(int blockNo, char* buffer);       //��ȡ���ݿ�
    void writeBlock(int blockNo, const char* buffer);//д�����ݿ�
    Inode readInode(int inodeNo);                    //��ȡi���
    void writeInode(int inodeNo, const Inode& inode);//д��i���
    vector<string> splitPath(const string& path);    //����·���ָ����ҵ�����Ŀ¼
    void recursiveDelete(int inodeNo);              //ɾ��Ŀ¼
    void listDirRecursive(int inodeNo, const string& path, vector<string>& result);
    string getFileInfo(const DirEntry& entry, const Inode& inode);  
    string getFormattedSize(size_t bytes) const;    
    int countUsedInodes() const;     //ͳ������i���
    int countUsedBlocks() const;     //ͳ���������ݿ�
    bool allocateFileBlocks(Inode& inode, size_t size);//���ݴ�С�������ݿ�
    bool copyWithinFS(const string& srcPath, const string& destPath);
    bool copyFromHostFile(const string& hostPath, const string& destPath);
    bool copyToHostFile(const string& srcPath, const string& hostPath);
    // ·������������
    string getFullPath(const string& path);
    string getFileName(const string& path);
    bool isDirectory(const string& path);

    // �����ص�˽�з���
    bool checkSuperBlock();
    bool checkInodeBitmap();
    bool checkBlockBitmap();
    bool checkDirectoryStructure();
    void markUsedBlocks(vector<bool>& blockUsed, int inodeNo);
    void repairFileSystem();

    // λͼ������������
    bool getBit(char* bitmap, int index);
    void setBit(char* bitmap, int index, bool value);

    // ��д��������
    void writeSuperBlock();
    void writeInodeBitmap();
    void writeBlockBitmap();
};
