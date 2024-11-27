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
        // ��ȡ���߼���������
        WaitForSingleObject(hReaderCountMutex, INFINITE);
        readerCount++;
        if (readerCount == 1) {
            // ��һ��������Ҫ��ȡд������
            WaitForSingleObject(hWriteMutex, INFINITE);
        }
        ReleaseMutex(hReaderCountMutex);
        return true;
    }

    void endRead() {
        WaitForSingleObject(hReaderCountMutex, INFINITE);
        readerCount--;
        if (readerCount == 0) {
            // ���һ�������ͷ�д������
            ReleaseMutex(hWriteMutex);
        }
        ReleaseMutex(hReaderCountMutex);
    }

    bool startWrite() {
        // д������Ҫ��ռ����
        return WaitForSingleObject(hWriteMutex, INFINITE) == WAIT_OBJECT_0;
    }

    void endWrite() {
        ReleaseMutex(hWriteMutex);
    }

    // �������
    void executeCommand() {
        string cmdStr(pCommand->cmd);
        stringstream ss(cmdStr);
        string cmd;
        ss >> cmd;

        string result = "ִ�гɹ�";

        // �ж��Ƕ���������д����
        bool isReadOp = (cmd == "dir" || cmd == "ls" || cmd == "cat" || cmd == "info");
        bool isWriteOp = (cmd == "md" || cmd == "rd" || cmd == "newfile" ||
            cmd == "del" || cmd == "copy" || cmd == "format");


        try {
            // ����û�Ȩ��
            if (!fs.checkAccess(fs.findInodeByPath("/"),
                isWriteOp ? WRITE_PERMISSION : READ_PERMISSION)) {
                throw runtime_error("Ȩ�޲���");
            }

            // ���ݲ������ͻ�ȡ��Ӧ����
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
                    result = "��¼ʧ��";
                }
            }
            else if (cmd == "format") {
                if (!fs.format()) {
                    result = "��ʽ��ʧ��";
                }
            }
            else if (cmd == "info") {
                result = fs.getSystemInfo();
            }
            else if (cmd == "md" || cmd == "mkdir") {
                string dirName;
                ss >> dirName;
                if (!fs.createDir(dirName)) {
                    result = "����Ŀ¼ʧ��";
                }
            }
            else if (cmd == "cd") {
                string path;
                ss >> path;
                if (!fs.changeDir(path)) {
                    result = "�л�Ŀ¼ʧ�ܣ�Ŀ¼�����ڻ���Ч";
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

                // ����ɾ��Ŀ¼
                bool success = fs.deleteDir(path, false);
                if (!success) {
                    // ���Ŀ¼�Ƿ����
                    int inodeNo = fs.findInodeByPath(path);
                    if (inodeNo == -1) {
                        result = "����Ŀ¼������";
                    }
                    else if (!fs.isDirEmpty(inodeNo)) {
                        // ֻ����Ŀ¼�ǿ�ʱ����Ҫȷ��
                        pCommand->needConfirm = true;
                        result = "Ŀ¼��Ϊ�գ��Ƿ�ȷ��ɾ����(Y/N)";
                    }
                    else {
                        result = "ɾ��Ŀ¼ʧ��";
                    }
                }
            }
            else if (cmd == "rd_confirm") {  // ����ȷ��ɾ��������
                string path;
                ss >> path;
                if (!fs.deleteDir(path, true)) {
                    result = "ɾ��Ŀ¼ʧ��";
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
                    result = "�����ļ�ʧ��";
                }
            }
            else if (cmd == "cat") {
                string path;
                if (!(ss >> path)) {
                    result = "����: ȱ���ļ�·��";
                }
                else {
                    if (!fs.catFile(path)) {
                        result = "�鿴�ļ�ʧ��";
                    }
                }
            }
            else if (cmd == "copy") {
                string srcPath, destPath;
                if (!(ss >> srcPath >> destPath)) {
                    result = "����: �������㣬��ҪԴ�ļ���Ŀ���ļ�·��";
                }
                else {
                    if (!fs.copyFile(srcPath, destPath)) {
                        result = "�����ļ�ʧ��";
                    }
                }
            }
            else if (cmd == "del") {
                string path;
                if (!(ss >> path)) {
                    result = "����: ȱ���ļ�·��";
                }
                else {
                    if (!fs.deleteFile(path)) {
                        result = "ɾ���ļ�ʧ��";
                    }
                }
            }
            else if (cmd == "check") {
                if (!fs.checkFileSystem()) {
                    result = "�ļ�ϵͳ���ʧ��";
                }
            }
            else if (cmd == "exit") {
                pCommand->shouldExit = true;
                result = "�������ر�";
            }
            else {
                result = "δ֪����";
            }
            // �ͷ���
            if (isReadOp) {
                endRead();
            }
            else if (isWriteOp) {
                endWrite();
            }
        }
        catch (const std::exception& e) {
            result = string("����: ") + e.what();
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
        // ���������ڴ�
        hSharedMem = CreateFileMapping(
            INVALID_HANDLE_VALUE,   //ʹ��ҳ���ļ�
            NULL,                   //Ĭ�ϰ�ȫ����
            PAGE_READWRITE,         //��дȨ��
            0,                      //��λ��С
            sizeof(Command),        //��λ��С
            SHARED_MEM_NAME         //�����ڴ�����
        );

        if (!hSharedMem) {
            cout << "���������ڴ�ʧ��: " << GetLastError() << endl;
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

        // ����������
        hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
        if (!hMutex) {
            cout << "����������ʧ��: " << GetLastError() << endl;
            cleanup();
            return false;
        }

        // ��ʼ���ļ�ϵͳ
        if (!fs.mount("virtual_disk.img")) {
            cout << "�ļ�ϵͳ��ʼ��ʧ��" << endl;
            cleanup();
            return false;
        }

        // ��ʼ������ṹ
        memset(pCommand, 0, sizeof(Command));
        isInitialized = true;
        return true;
    }

    void run() {
        if (!isInitialized) {
            cout << "������δ��ʼ��" << endl;
            return;
        }

        cout << "SimDisk ������������..." << endl;

        while (true) {
            // �ȴ�������
            DWORD waitResult = WaitForSingleObject(hMutex, INFINITE);
            if (waitResult != WAIT_OBJECT_0) {
                cout << "�ȴ�������ʧ��: " << GetLastError() << endl;
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

            Sleep(100); // �������ռ��CPU
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
        cout << "��������ʼ��ʧ�ܣ�" << endl;
        return 1;
    }

    server.run();
    return 0;
}