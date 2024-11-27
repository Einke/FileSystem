#pragma once
#include <windows.h>
#include <string>
#include <ctime>

// �ļ�ϵͳ��������
#define BLOCK_SIZE 1024        // ���С��1KB
#define DISK_SIZE 104857600    // ���̴�С��100MB
#define INODE_SIZE 128         // i�ڵ��С��128�ֽ�
#define MAX_FILENAME 28        // ����ļ�������
#define MAX_PATH 256          // ���·������
#define MAX_COMMAND_LENGTH 256 // ��������
#define MAX_RESULT_LENGTH 1024 // ���������

// �����ڴ���أ��ڶ��׶�ʹ�ã�
#define SHARED_MEM_NAME L"Local\\SimDiskSharedMemory"
#define MUTEX_NAME L"Local\\SimDiskMutex"
#define READ_SEM_NAME L"Local\\SimDiskReadSemaphore"
#define WRITE_SEM_NAME L"Local\\SimDiskWriteSemaphore"

// �û�Ȩ�޳���
#define READ_PERMISSION  4  // 100
#define WRITE_PERMISSION 2  // 010
#define EXEC_PERMISSION  1  // 001

// ����ź�������
#define READER_COUNT_MUTEX_NAME L"Local\\ReaderCountMutex"
#define WRITE_MUTEX_NAME L"Local\\WriteMutex"

// �ļ�����
enum FileType {
    TYPE_FILE = 1,
    TYPE_DIR = 2
};

// �ļ�Ȩ�ޣ������׶�ʹ�ã�
struct FilePermission {
    unsigned char owner : 3;  // ������Ȩ�� (rwx)
    unsigned char group : 3;  // ��Ȩ��
    unsigned char other : 3;  // �����û�Ȩ��
};

// �û���Ϣ�ṹ
struct UserInfo {
    int uid;
    int gid;
    char username[32];
    char password[32];
};

// ����ṹ�����̼�ͨ��ʹ�ã�
struct Command {
    char cmd[MAX_COMMAND_LENGTH];    // ��������
    bool isExecuted;                 // ִ��״̬
    char result[MAX_RESULT_LENGTH];  // ִ�н��
    bool shouldExit;                 // �˳���־
    bool needConfirm;                //�Ƿ���Ҫ�û�ȷ��
    UserInfo userInfo;              // ����û���Ϣ
    int accessType;                 // ��������(��/д)
};

/*

SimDisk/
������ SimDisk/              # ����������Ŀ
��   ������ include/
��   ��   ������ filesystem.h   # �ļ�ϵͳ����
��   ��   ������ inode.h       # i�ڵ㶨��
��   ��   ������ superblock.h  # �����鶨��
��   ��   ������ common.h      # ��������
��   ��   ������ ipc.h         # ���̼�ͨ�ţ��ڶ��׶Σ�
��   ��   ������ user_auth.h   # �û���֤�������׶Σ�
��   ������ src/
��       ������ filesystem.cpp
��       ������ simdisk.cpp
��
������ Shell/                # �ͻ�����Ŀ
��   ������ src/
��       ������ shell.cpp
��
������ SimDisk.sln           # ��������ļ�

*/
