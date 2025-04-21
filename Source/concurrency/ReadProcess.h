#pragma once

#include <JuceHeader.h>
#if JUCE_WINDOWS
    #include <windows.h>
#endif

class ReadProcess {
public:
    ReadProcess() {}

    void start(juce::String cmd) {
        if (isRunning()) {
            close();
        }
#if JUCE_WINDOWS
        cmd = "cmd /c \"" + cmd + "\"";

        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            errorExit(TEXT("CreatePipe"));
            return;
        }

        // Mark the read handle as inheritable
        if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            errorExit(TEXT("SetHandleInformation"));
            return;
        }

        STARTUPINFO si = { 0 };

        si.cb = sizeof(STARTUPINFO);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWritePipe;  // Child process writes here
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE); 

        // Create the process
        if (!CreateProcess(NULL, const_cast<LPSTR>(cmd.toStdString().c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            errorExit(TEXT("CreateProcess"));
            return;
        }
        
        // Close the write end of the pipe as we don't need it
        CloseHandle(hWritePipe);
        hWritePipe = INVALID_HANDLE_VALUE;
#else
        process = popen(cmd.toStdString().c_str(), "r");
        if (process == nullptr) {
            DBG("popen failed: " + juce::String(std::strerror(errno)));
            jassertfalse;
        }
#endif
    }

    size_t read(void* buffer, size_t size) {
#if JUCE_WINDOWS
        DWORD bytesRead = 0;
        if (!ReadFile(hReadPipe, buffer, size, &bytesRead, NULL) && GetLastError() != ERROR_BROKEN_PIPE) {
            errorExit(TEXT("ReadFile"));
        }
        return bytesRead;
#else
        return fread(buffer, 1, size, process);
#endif
    }

    void close() {
        if (isRunning()) {
#if JUCE_WINDOWS
            // Clean up
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            
            if (hWritePipe != INVALID_HANDLE_VALUE)
                CloseHandle(hWritePipe);

            hReadPipe = INVALID_HANDLE_VALUE;
            hWritePipe = INVALID_HANDLE_VALUE;
#else
            pclose(process);
            process = nullptr;
#endif
        }
    }

    bool isRunning() {
#if JUCE_WINDOWS
        return hReadPipe != INVALID_HANDLE_VALUE;
#else
        return process != nullptr;
#endif
    }

#if JUCE_WINDOWS
    void errorExit(PCTSTR lpszFunction) { 
        LPVOID lpMsgBuf;
        DWORD dw = GetLastError(); 

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | 
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR) &lpMsgBuf,
            0, NULL );
        
        DBG("Error: " + juce::String((LPCTSTR)lpMsgBuf));

        LocalFree(lpMsgBuf);
        jassertfalse;
    }
#endif

    ~ReadProcess() {
        close();
    }

private:
#if JUCE_WINDOWS
    HANDLE hReadPipe = INVALID_HANDLE_VALUE;
    HANDLE hWritePipe = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi;
#else
    FILE* process = nullptr;
#endif
};