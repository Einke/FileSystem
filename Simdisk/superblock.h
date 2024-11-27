#pragma once

#pragma pack(1)
struct SuperBlock {
    int blockSize;          // ���С (1KB)
    int totalBlocks;        // �ܿ���
    int inodeCount;         // i�ڵ�����
    int freeBlockCount;     // ���п�����
    int freeInodeCount;     // ����i�ڵ�����
    int firstDataBlock;     // ��һ�����ݿ�λ��
    char padding[488];      // �����512�ֽ�
};
#pragma pack() 
