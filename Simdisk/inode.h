#pragma once
#include "common.h"

#pragma pack(1)
struct Inode {
    int fileSize;           // 文件大小
    int fileType;           // 文件类型
    int blockAddresses[10]; // 直接块地址
    time_t createTime;      // 创建时间
    time_t modifyTime;      // 修改时间
    int uid;                // 所有者ID（第三阶段使用）
    int gid;                // 组ID（第三阶段使用）
    FilePermission perm;    // 文件权限（第三阶段使用）
    char padding[44];       // 填充至128字节
};

struct DirEntry {
    char fileName[MAX_FILENAME];  // 文件名
    int inodeNo;                  // i节点号
};
#pragma pack()
