#pragma once

#include <JuceHeader.h>
#if JUCE_WINDOWS
    #include <windows.h>
#endif


class WriteProcess {
public:

    WriteProcess() {}

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

        // Mark the write handle as inheritable
        if (!SetHandleInformation(hWritePipe, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            errorExit(TEXT("SetHandleInformation"));
            return;
        }

        STARTUPINFO si = { 0 };

        si.cb = sizeof(STARTUPINFO);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hReadPipe; // Child process reads from here
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); // Forward to parent stdout
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);  // Forward to parent stderr

        // Create the process
        if (!CreateProcess(NULL, const_cast<LPSTR>(cmd.toStdString().c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            errorExit(TEXT("CreateProcess"));
            return;
        }
#else
        process = popen(cmd.toStdString().c_str(), "w");
        if (process == nullptr) {
            DBG("popen failed: " + juce::String(std::strerror(errno)));
            jassertfalse;
        }
#endif
    }

    void write(void* data, size_t size) {
#if JUCE_WINDOWS
        DWORD bytesWritten;
        if (!WriteFile(hWritePipe, data, size, &bytesWritten, NULL)) {
            errorExit(TEXT("WriteFile"));
        }
#else
        fwrite(data, size, 1, process);
#endif
    }

    void close() {
        if (isRunning()) {
#if JUCE_WINDOWS
            // Close the write handle to signal EOF to the process
            CloseHandle(hWritePipe);

            // Wait for the process to finish
            WaitForSingleObject(pi.hProcess, INFINITE);

            // Clean up
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hReadPipe);

            hWritePipe = INVALID_HANDLE_VALUE;
            hReadPipe = INVALID_HANDLE_VALUE;
#else
            pclose(process);
            process = nullptr;
#endif
        }
    }

    bool isRunning() {
#if JUCE_WINDOWS
        return hWritePipe != INVALID_HANDLE_VALUE;
#else
        return process != nullptr;
#endif
    }

#if JUCE_WINDOWS
    // from https://learn.microsoft.com/en-us/windows/win32/ProcThread/creating-a-child-process-with-redirected-input-and-output
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

    ~WriteProcess() {
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
