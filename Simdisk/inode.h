#pragma once
#include "common.h"

#pragma pack(1)
struct Inode {
    int fileSize;           // �ļ���С
    int fileType;           // �ļ�����
    int blockAddresses[10]; // ֱ�ӿ��ַ
    time_t createTime;      // ����ʱ��
    time_t modifyTime;      // �޸�ʱ��
    int uid;                // ������ID�������׶�ʹ�ã�
    int gid;                // ��ID�������׶�ʹ�ã�
    FilePermission perm;    // �ļ�Ȩ�ޣ������׶�ʹ�ã�
    char padding[44];       // �����128�ֽ�
};

struct DirEntry {
    char fileName[MAX_FILENAME];  // �ļ���
    int inodeNo;                  // i�ڵ��
};
#pragma pack()
