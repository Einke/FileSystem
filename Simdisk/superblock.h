#pragma once

#pragma pack(1)
struct SuperBlock {
    int blockSize;          // 块大小 (1KB)
    int totalBlocks;        // 总块数
    int inodeCount;         // i节点数量
    int freeBlockCount;     // 空闲块数量
    int freeInodeCount;     // 空闲i节点数量
    int firstDataBlock;     // 第一个数据块位置
    char padding[488];      // 填充至512字节
};
#pragma pack() 
