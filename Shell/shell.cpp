#include "../SimDisk/common.h"
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <vector>
#include <limits>

using namespace std;

class Shell {
private:
    HANDLE hSharedMem;
    HANDLE hMutex;
    Command* pCommand;
    bool isInitialized;
    string currentPath;
    UserInfo currentUser;
    bool isLoggedIn;

    bool login() {
        string username, password;
        cout << "用户名: ";
        getline(cin, username);
        cout << "密码: ";
        getline(cin, password);

        // 发送登录命令
        string loginCmd = "login " + username + " " + password;
        if (sendCommand(loginCmd)) {
            if (waitAndShowResult()) {
                isLoggedIn = true;
                return true;
            }
        }
        return false;
    }

    // 显示命令提示符
    void showPrompt() {
        cout << "simdisk:" << currentPath << "$ ";
    }

    // 发送命令到服务器
    bool sendCommand(const string& cmdStr, bool isExit = false) {
        DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            cout << "等待互斥量失败: " << GetLastError() << endl;
            return false;
        }

        strncpy_s(pCommand->cmd, MAX_COMMAND_LENGTH, cmdStr.c_str(), -1);
        pCommand->cmd[MAX_COMMAND_LENGTH - 1] = '\0';
        pCommand->isExecuted = false;
        pCommand->shouldExit = isExit;

        ReleaseMutex(hMutex);
        return true;
    }

    // 更新当前路径（基于cd命令的结果）
    void updatePath(const string& cmd, const string& result) {
        if (cmd.substr(0, 3) == "cd ") {
            // 只有当命令执行成功时才更新路径
            if (result == "执行成功") {
                string path = cmd.substr(3);
                if (path == "/") {
                    currentPath = "/";
                }
                else if (path == "..") {
                    size_t pos = currentPath.find_last_of('/');
                    if (pos == 0) currentPath = "/";
                    else if (pos != string::npos) {
                        currentPath = currentPath.substr(0, pos);
                    }
                }
                else if (path != ".") {
                    if (currentPath == "/") currentPath += path;
                    else currentPath += "/" + path;
                    if (path[0] == '/') currentPath = path;
                }
            }
        }
    }

    // 等待并显示结果
    bool waitAndShowResult() {
        bool resultReceived = false;
        bool waitingForConfirmResult = false;  //标记是否在等待确认命令的结果

        while (!resultReceived) {
            DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                cout << "等待互斥量失败: " << GetLastError() << endl;
                return false;
            }

            if (pCommand->isExecuted) {
                string result(pCommand->result);
                if (!result.empty()) {
                    cout << result << endl;
                }

                // 如果正在等待确认命令的结果
                if (waitingForConfirmResult) {
                    resultReceived = true;  // 确认命令执行完毕，直接结束
                }
                // 如果需要确认
                else if (pCommand->needConfirm) {
                    char choice;
                    cout << "请输入选择 (Y/N): ";
                    cin >> choice;
                    cin.ignore((numeric_limits<streamsize>::max)(), '\n');

                    choice = toupper(choice);
                    if (choice == 'Y' || choice == 'y') {
                        // 获取原始命令中的路径
                        string originalCmd(pCommand->cmd);
                        size_t pos = originalCmd.find(' ');
                        string path = originalCmd.substr(pos + 1);

                        // 发送确认删除命令
                        string confirmCmd = "rd_confirm " + path;

                        // 重置命令状态
                        pCommand->isExecuted = false;
                        pCommand->needConfirm = false;

                        // 设置等待确认结果标志
                        waitingForConfirmResult = true;

                        // 释放互斥量
                        ReleaseMutex(hMutex);

                        // 发送新命令
                        sendCommand(confirmCmd);
                        continue;  // 继续等待新命令的结果
                    }
                    else if (choice == 'N' || choice == 'n') {
                        // 用户取消操作
                        cout << "操作已取消" << endl;
                        pCommand->needConfirm = false;  // 重置确认标志
                        resultReceived = true;  // 结束等待
                    }
                    else {
                        cout << "无效的选择，操作已取消" << endl;
                        pCommand->needConfirm = false;  // 重置确认标志
                        resultReceived = true;  // 结束等待
                    }
                }
                else {
                    resultReceived = true;  // 普通命令执行完毕
                }


                // 更新路径状态（传入命令和结果）
                updatePath(string(pCommand->cmd), result);
                resultReceived = true;
            }

            ReleaseMutex(hMutex);

            if (!resultReceived) {
                Sleep(100);
            }
        }
        return true;
    }

    // 显示帮助信息
    void showHelp() {
        cout << "\n可用命令：\n"
            << "  format            格式化文件系统\n"
            << "  info              显示系统信息\n"
            << "  cd                切换目录\n"
            << "  dir/ls [/s]       显示目录内容,加/s显示所有子目录\n"
            << "  md/mkdir          创建目录\n"
            << "  rd                删除目录\n"
            << "  newfile - [-i]    创建文件，加-i表示追加文件内容\n"
            << "  cat               打开文件\n"
            << "  copy              复制文件，支持系统内复制\n"
            << "                    同时支持从host复制到该系统，或从该系统复制到host\n"
            << "  del               删除文件\n"
            << "  check             检查并恢复文件系统\n"
            << "  help              显示此帮助信息\n"
            << "  clear             清屏\n"
            << "  exit              退出程序\n"
            << endl;
    }

    // 清屏
    void clearScreen() {
        system("cls");
    }


public:
    Shell() : hSharedMem(NULL), hMutex(NULL), pCommand(NULL),
        isInitialized(false), currentPath("/") {}

    ~Shell() {
        cleanup();
    }

    bool init() {
        // 打开共享内存
        hSharedMem = OpenFileMapping(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            SHARED_MEM_NAME
        );

        if (!hSharedMem) {
            cout << "无法连接到SimDisk服务器: " << GetLastError() << endl;
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

        // 打开互斥量
        hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
        if (!hMutex) {
            cout << "打开互斥量失败: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        isInitialized = true;
        return true;
    }

    void run() {
        if (!isInitialized) {
            cout << "Shell未初始化" << endl;
            return;
        }

        cout << "SimDisk Shell\n"
            << "输入 'help' 获取命令列表\n" << endl;

        // 要求用户登录
        while (!isLoggedIn) {
            if (!login()) {
                cout << "登录失败，请重试" << endl;
            }
        }

        string input;
        while (true) {
            showPrompt();
            getline(cin, input);

            // 处理空命令
            if (input.empty()) {
                continue;
            }

            // 处理本地命令
            if (input == "help") {
                showHelp();
                continue;
            }
            else if (input == "clear") {
                clearScreen();
                continue;
            }
            else if (input == "exit") {
                if (sendCommand(input, true)) {
                    waitAndShowResult();
                }
                break;
            }

            // 发送命令到服务器
            if (sendCommand(input)) {
                waitAndShowResult();
            }
        }
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
    // 等待服务器启动
    Sleep(1000);  // 等待1秒

    Shell shell;

    if (!shell.init()) {
        cout << "按回车退出..." << endl;
        getchar();
        return 1;
    }

    shell.run();
    return 0;
}