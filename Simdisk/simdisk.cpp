#include "filesystem.h"
#include "common.h"
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

class SimDiskServer {
private:
    HANDLE hSharedMem;
    HANDLE hMutex;
    Command* pCommand;
    FileSystem fs;
    bool isInitialized;

    HANDLE hReaderCountMutex;
    HANDLE hWriteMutex;
    int readerCount;

    void initializeSynchronization() {
        hReaderCountMutex = CreateMutex(NULL, FALSE, READER_COUNT_MUTEX_NAME);
        hWriteMutex = CreateMutex(NULL, FALSE, WRITE_MUTEX_NAME);
        readerCount = 0;
    }

    bool startRead() {
        // 获取读者计数互斥量
        WaitForSingleObject(hReaderCountMutex, INFINITE);
        readerCount++;
        if (readerCount == 1) {
            // 第一个读者需要获取写互斥量
            WaitForSingleObject(hWriteMutex, INFINITE);
        }
        ReleaseMutex(hReaderCountMutex);
        return true;
    }

    void endRead() {
        WaitForSingleObject(hReaderCountMutex, INFINITE);
        readerCount--;
        if (readerCount == 0) {
            // 最后一个读者释放写互斥量
            ReleaseMutex(hWriteMutex);
        }
        ReleaseMutex(hReaderCountMutex);
    }

    bool startWrite() {
        // 写操作需要独占访问
        return WaitForSingleObject(hWriteMutex, INFINITE) == WAIT_OBJECT_0;
    }

    void endWrite() {
        ReleaseMutex(hWriteMutex);
    }

    // 命令处理函数
    void executeCommand() {
        string cmdStr(pCommand->cmd);
        stringstream ss(cmdStr);
        string cmd;
        ss >> cmd;

        string result = "执行成功";

        // 判断是读操作还是写操作
        bool isReadOp = (cmd == "dir" || cmd == "ls" || cmd == "cat" || cmd == "info");
        bool isWriteOp = (cmd == "md" || cmd == "rd" || cmd == "newfile" ||
            cmd == "del" || cmd == "copy" || cmd == "format");


        try {
            // 检查用户权限
            if (!fs.checkAccess(fs.findInodeByPath("/"),
                isWriteOp ? WRITE_PERMISSION : READ_PERMISSION)) {
                throw runtime_error("权限不足");
            }

            // 根据操作类型获取相应的锁
            if (isReadOp) {
                startRead();
            }
            else if (isWriteOp) {
                startWrite();
            }

            if (cmd == "login") {
                string username, password;
                ss >> username >> password;
                if (!fs.login(username, password)) {
                    result = "登录失败";
                }
            }
            else if (cmd == "format") {
                if (!fs.format()) {
                    result = "格式化失败";
                }
            }
            else if (cmd == "info") {
                result = fs.getSystemInfo();
            }
            else if (cmd == "md" || cmd == "mkdir") {
                string dirName;
                ss >> dirName;
                if (!fs.createDir(dirName)) {
                    result = "创建目录失败";
                }
            }
            else if (cmd == "cd") {
                string path;
                ss >> path;
                if (!fs.changeDir(path)) {
                    result = "切换目录失败：目录不存在或无效";
                }
            }
            else if (cmd == "dir" || cmd == "ls") {
                string param;
                bool recursive = false;
                if (ss >> param) {
                    recursive = (param == "/s");
                }

                vector<string> entries = fs.listDir(recursive);
                std::stringstream resultSS;
                for (const auto& entry : entries) {
                    resultSS << entry << "\n";
                }
                result = resultSS.str();
            }
            else if (cmd == "rd") {
                string path;
                ss >> path;

                // 尝试删除目录
                bool success = fs.deleteDir(path, false);
                if (!success) {
                    // 检查目录是否存在
                    int inodeNo = fs.findInodeByPath(path);
                    if (inodeNo == -1) {
                        result = "错误：目录不存在";
                    }
                    else if (!fs.isDirEmpty(inodeNo)) {
                        // 只有在目录非空时才需要确认
                        pCommand->needConfirm = true;
                        result = "目录不为空，是否确认删除？(Y/N)";
                    }
                    else {
                        result = "删除目录失败";
                    }
                }
            }
            else if (cmd == "rd_confirm") {  // 处理确认删除的命令
                string path;
                ss >> path;
                if (!fs.deleteDir(path, true)) {
                    result = "删除目录失败";
                }
            }
            else if (cmd == "newfile") {
                string path;
                ss >> path;

                string option;
                bool inputContent = false;
                if (ss >> option && option == "-i") {
                    inputContent = true;
                }

                if (!fs.createFile(path, inputContent)) {
                    result = "创建文件失败";
                }
            }
            else if (cmd == "cat") {
                string path;
                if (!(ss >> path)) {
                    result = "错误: 缺少文件路径";
                }
                else {
                    if (!fs.catFile(path)) {
                        result = "查看文件失败";
                    }
                }
            }
            else if (cmd == "copy") {
                string srcPath, destPath;
                if (!(ss >> srcPath >> destPath)) {
                    result = "错误: 参数不足，需要源文件和目标文件路径";
                }
                else {
                    if (!fs.copyFile(srcPath, destPath)) {
                        result = "复制文件失败";
                    }
                }
            }
            else if (cmd == "del") {
                string path;
                if (!(ss >> path)) {
                    result = "错误: 缺少文件路径";
                }
                else {
                    if (!fs.deleteFile(path)) {
                        result = "删除文件失败";
                    }
                }
            }
            else if (cmd == "check") {
                if (!fs.checkFileSystem()) {
                    result = "文件系统检查失败";
                }
            }
            else if (cmd == "exit") {
                pCommand->shouldExit = true;
                result = "服务器关闭";
            }
            else {
                result = "未知命令";
            }
            // 释放锁
            if (isReadOp) {
                endRead();
            }
            else if (isWriteOp) {
                endWrite();
            }
        }
        catch (const std::exception& e) {
            result = string("错误: ") + e.what();
        }

        strncpy_s(pCommand->result, MAX_RESULT_LENGTH, result.c_str(), MAX_RESULT_LENGTH - 1);

        pCommand->result[MAX_RESULT_LENGTH - 1] = '\0';
    }

public:
    SimDiskServer() : hSharedMem(NULL), hMutex(NULL), pCommand(NULL), isInitialized(false) {}

    ~SimDiskServer() {
        cleanup();
    }

    bool init() {
        // 创建共享内存
        hSharedMem = CreateFileMapping(
            INVALID_HANDLE_VALUE,   //使用页面文件
            NULL,                   //默认安全属性
            PAGE_READWRITE,         //读写权限
            0,                      //高位大小
            sizeof(Command),        //低位大小
            SHARED_MEM_NAME         //共享内存名称
        );

        if (!hSharedMem) {
            cout << "创建共享内存失败: " << GetLastError() << endl;
            return false;
        }

        // 映射共享内存
        pCommand = (Command*)MapViewOfFile(
            hSharedMem,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(Command)
        );

        if (!pCommand) {
            cout << "映射共享内存失败: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        // 创建互斥量
        hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
        if (!hMutex) {
            cout << "创建互斥量失败: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        // 初始化文件系统
        if (!fs.mount("virtual_disk.img")) {
            cout << "文件系统初始化失败" << endl;
            cleanup();
            return false;
        }

        // 初始化命令结构
        memset(pCommand, 0, sizeof(Command));
        isInitialized = true;
        return true;
    }

    void run() {
        if (!isInitialized) {
            cout << "服务器未初始化" << endl;
            return;
        }

        cout << "SimDisk 服务器已启动..." << endl;

        while (true) {
            // 等待互斥量
            DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                cout << "等待互斥量失败: " << GetLastError() << endl;
                break;
            }

            if (!pCommand->isExecuted) {
                executeCommand();
                pCommand->isExecuted = true;
            }

            ReleaseMutex(hMutex);

            if (pCommand->shouldExit) {
                break;
            }

            Sleep(100); // 避免过度占用CPU
        }

        cleanup();
    }

private:
    void cleanup() {
        if (pCommand) {
            UnmapViewOfFile(pCommand);
            pCommand = NULL;
        }
        if (hSharedMem) {
            CloseHandle(hSharedMem);
            hSharedMem = NULL;
        }
        if (hMutex) {
            CloseHandle(hMutex);
            hMutex = NULL;
        }
        isInitialized = false;
    }
};

int main() {
    SimDiskServer server;

    if (!server.init()) {
        cout << "服务器初始化失败！" << endl;
        return 1;
    }

    server.run();
    return 0;
}