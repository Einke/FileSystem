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
        cout << "�û���: ";
        getline(cin, username);
        cout << "����: ";
        getline(cin, password);

        // ���͵�¼����
        string loginCmd = "login " + username + " " + password;
        if (sendCommand(loginCmd)) {
            if (waitAndShowResult()) {
                isLoggedIn = true;
                return true;
            }
        }
        return false;
    }

    // ��ʾ������ʾ��
    void showPrompt() {
        cout << "simdisk:" << currentPath << "$ ";
    }

    // �������������
    bool sendCommand(const string& cmdStr, bool isExit = false) {
        DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            cout << "�ȴ�������ʧ��: " << GetLastError() << endl;
            return false;
        }

        strncpy_s(pCommand->cmd, MAX_COMMAND_LENGTH, cmdStr.c_str(), -1);
        pCommand->cmd[MAX_COMMAND_LENGTH - 1] = '\0';
        pCommand->isExecuted = false;
        pCommand->shouldExit = isExit;

        ReleaseMutex(hMutex);
        return true;
    }

    // ���µ�ǰ·��������cd����Ľ����
    void updatePath(const string& cmd, const string& result) {
        if (cmd.substr(0, 3) == "cd ") {
            // ֻ�е�����ִ�гɹ�ʱ�Ÿ���·��
            if (result == "ִ�гɹ�") {
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

    // �ȴ�����ʾ���
    bool waitAndShowResult() {
        bool resultReceived = false;
        bool waitingForConfirmResult = false;  //����Ƿ��ڵȴ�ȷ������Ľ��

        while (!resultReceived) {
            DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                cout << "�ȴ�������ʧ��: " << GetLastError() << endl;
                return false;
            }

            if (pCommand->isExecuted) {
                string result(pCommand->result);
                if (!result.empty()) {
                    cout << result << endl;
                }

                // ������ڵȴ�ȷ������Ľ��
                if (waitingForConfirmResult) {
                    resultReceived = true;  // ȷ������ִ����ϣ�ֱ�ӽ���
                }
                // �����Ҫȷ��
                else if (pCommand->needConfirm) {
                    char choice;
                    cout << "������ѡ�� (Y/N): ";
                    cin >> choice;
                    cin.ignore((numeric_limits<streamsize>::max)(), '\n');

                    choice = toupper(choice);
                    if (choice == 'Y' || choice == 'y') {
                        // ��ȡԭʼ�����е�·��
                        string originalCmd(pCommand->cmd);
                        size_t pos = originalCmd.find(' ');
                        string path = originalCmd.substr(pos + 1);

                        // ����ȷ��ɾ������
                        string confirmCmd = "rd_confirm " + path;

                        // ��������״̬
                        pCommand->isExecuted = false;
                        pCommand->needConfirm = false;

                        // ���õȴ�ȷ�Ͻ����־
                        waitingForConfirmResult = true;

                        // �ͷŻ�����
                        ReleaseMutex(hMutex);

                        // ����������
                        sendCommand(confirmCmd);
                        continue;  // �����ȴ�������Ľ��
                    }
                    else if (choice == 'N' || choice == 'n') {
                        // �û�ȡ������
                        cout << "������ȡ��" << endl;
                        pCommand->needConfirm = false;  // ����ȷ�ϱ�־
                        resultReceived = true;  // �����ȴ�
                    }
                    else {
                        cout << "��Ч��ѡ�񣬲�����ȡ��" << endl;
                        pCommand->needConfirm = false;  // ����ȷ�ϱ�־
                        resultReceived = true;  // �����ȴ�
                    }
                }
                else {
                    resultReceived = true;  // ��ͨ����ִ�����
                }


                // ����·��״̬����������ͽ����
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

    // ��ʾ������Ϣ
    void showHelp() {
        cout << "\n�������\n"
            << "  format            ��ʽ���ļ�ϵͳ\n"
            << "  info              ��ʾϵͳ��Ϣ\n"
            << "  cd                �л�Ŀ¼\n"
            << "  dir/ls [/s]       ��ʾĿ¼����,��/s��ʾ������Ŀ¼\n"
            << "  md/mkdir          ����Ŀ¼\n"
            << "  rd                ɾ��Ŀ¼\n"
            << "  newfile - [-i]    �����ļ�����-i��ʾ׷���ļ�����\n"
            << "  cat               ���ļ�\n"
            << "  copy              �����ļ���֧��ϵͳ�ڸ���\n"
            << "                    ͬʱ֧�ִ�host���Ƶ���ϵͳ����Ӹ�ϵͳ���Ƶ�host\n"
            << "  del               ɾ���ļ�\n"
            << "  check             ��鲢�ָ��ļ�ϵͳ\n"
            << "  help              ��ʾ�˰�����Ϣ\n"
            << "  clear             ����\n"
            << "  exit              �˳�����\n"
            << endl;
    }

    // ����
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
        // �򿪹����ڴ�
        hSharedMem = OpenFileMapping(
            FILE_MAP_ALL_ACCESS,
            FALSE,
            SHARED_MEM_NAME
        );

        if (!hSharedMem) {
            cout << "�޷����ӵ�SimDisk������: " << GetLastError() << endl;
            return false;
        }

        // ӳ�乲���ڴ�
        pCommand = (Command*)MapViewOfFile(
            hSharedMem,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(Command)
        );

        if (!pCommand) {
            cout << "ӳ�乲���ڴ�ʧ��: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        // �򿪻�����
        hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
        if (!hMutex) {
            cout << "�򿪻�����ʧ��: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        isInitialized = true;
        return true;
    }

    void run() {
        if (!isInitialized) {
            cout << "Shellδ��ʼ��" << endl;
            return;
        }

        cout << "SimDisk Shell\n"
            << "���� 'help' ��ȡ�����б�\n" << endl;

        // Ҫ���û���¼
        while (!isLoggedIn) {
            if (!login()) {
                cout << "��¼ʧ�ܣ�������" << endl;
            }
        }

        string input;
        while (true) {
            showPrompt();
            getline(cin, input);

            // ���������
            if (input.empty()) {
                continue;
            }

            // ����������
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

            // �������������
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
    // �ȴ�����������
    Sleep(1000);  // �ȴ�1��

    Shell shell;

    if (!shell.init()) {
        cout << "���س��˳�..." << endl;
        getchar();
        return 1;
    }

    shell.run();
    return 0;
}