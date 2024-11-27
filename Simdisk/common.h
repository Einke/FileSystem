#pragma once
#include <windows.h>
#include <string>
#include <ctime>

// 文件系统常量定义
#define BLOCK_SIZE 1024        // 块大小：1KB
#define DISK_SIZE 104857600    // 磁盘大小：100MB
#define INODE_SIZE 128         // i节点大小：128字节
#define MAX_FILENAME 28        // 最大文件名长度
#define MAX_PATH 256          // 最大路径长度
#define MAX_COMMAND_LENGTH 256 // 最大命令长度
#define MAX_RESULT_LENGTH 1024 // 最大结果长度

// 共享内存相关（第二阶段使用）
#define SHARED_MEM_NAME L"Local\\SimDiskSharedMemory"
#define MUTEX_NAME L"Local\\SimDiskMutex"
#define READ_SEM_NAME L"Local\\SimDiskReadSemaphore"
#define WRITE_SEM_NAME L"Local\\SimDiskWriteSemaphore"

// 用户权限常量
#define READ_PERMISSION  4  // 100
#define WRITE_PERMISSION 2  // 010
#define EXEC_PERMISSION  1  // 001

// 添加信号量名称
#define READER_COUNT_MUTEX_NAME L"Local\\ReaderCountMutex"
#define WRITE_MUTEX_NAME L"Local\\WriteMutex"

// 文件类型
enum FileType {
    TYPE_FILE = 1,
    TYPE_DIR = 2
};

// 文件权限（第三阶段使用）
struct FilePermission {
    unsigned char owner : 3;  // 所有者权限 (rwx)
    unsigned char group : 3;  // 组权限
    unsigned char other : 3;  // 其他用户权限
};

// 用户信息结构
struct UserInfo {
    int uid;
    int gid;
    char username[32];
    char password[32];
};

// 命令结构（进程间通信使用）
struct Command {
    char cmd[MAX_COMMAND_LENGTH];    // 命令内容
    bool isExecuted;                 // 执行状态
    char result[MAX_RESULT_LENGTH];  // 执行结果
    bool shouldExit;                 // 退出标志
    bool needConfirm;                //是否需要用户确认
    UserInfo userInfo;              // 添加用户信息
    int accessType;                 // 访问类型(读/写)
};

/*

SimDisk/
├── SimDisk/              # 服务器端项目
│   ├── include/
│   │   ├── filesystem.h   # 文件系统核心
│   │   ├── inode.h       # i节点定义
│   │   ├── superblock.h  # 超级块定义
│   │   ├── common.h      # 公共定义
│   │   ├── ipc.h         # 进程间通信（第二阶段）
│   │   └── user_auth.h   # 用户认证（第三阶段）
│   └── src/
│       ├── filesystem.cpp
│       └── simdisk.cpp
│
├── Shell/                # 客户端项目
│   └── src/
│       └── shell.cpp
│
└── SimDisk.sln           # 解决方案文件

*/
