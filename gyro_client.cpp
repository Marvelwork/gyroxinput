#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ViGEm/Client.h"
#include <process.h> 
#include <chrono>    

#pragma comment(lib, "setupapi.lib")

// Define DS4_BUTTON_SPECIAL if it's missing from the header
#ifndef DS4_BUTTON_SPECIAL
#define DS4_BUTTON_SPECIAL 0x1000 // PS Button bitmask for wButtons
#endif

// --- Added Shared Data Structure ---
#pragma pack(push, 1) // Ensure struct packing matches Rust's #[repr(C, packed)]
struct SharedGyroData {
    volatile LONG64 update_sequence; // Use volatile for sequence number
    SHORT gyro_x;
    SHORT gyro_y;
    SHORT gyro_z;
    SHORT accel_x;
    SHORT accel_y;
    SHORT accel_z;
    USHORT timestamp;
    LONG64 is_aiming; // 0 = false, non-zero = true
};
#pragma pack(pop)
// --- End Shared Data Structure ---

// Global handles for cleanup
HANDLE g_hMapFile = NULL;
volatile SharedGyroData* g_pSharedData = NULL; // pointer to shared memory, volatile
HANDLE g_hDataEvent = NULL;
PVIGEM_CLIENT g_client = NULL; // Make client global for cleanup
PVIGEM_TARGET g_ds4 = NULL;   // Make ds4 global for cleanup

// Cleanup function
void cleanup() {
    printf("\nCleaning up resources...\n");
    if (g_pSharedData) {
        UnmapViewOfFile((LPCVOID)g_pSharedData);
        g_pSharedData = NULL;
    }
    if (g_hMapFile) {
        CloseHandle(g_hMapFile);
        g_hMapFile = NULL;
    }
    if (g_hDataEvent) {
        CloseHandle(g_hDataEvent);
        g_hDataEvent = NULL;
    }
    if (g_ds4 && g_client) {
        vigem_target_remove(g_client, g_ds4);
        vigem_target_free(g_ds4);
        g_ds4 = NULL;
    }
    if (g_client) {
        vigem_disconnect(g_client);
        vigem_free(g_client);
        g_client = NULL;
    }
    printf("Cleanup complete.\n");
}

// Signal handler for Ctrl+C
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
        // Handle the CTRL-C signal.
        case CTRL_C_EVENT:
            printf("Ctrl+C detected, exiting...\n");
            cleanup();
            ExitProcess(0); // Force exit if needed
            return TRUE;

        // Handle other signals if necessary
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            cleanup();
            return FALSE; // Let the system handle it

        default:
            return FALSE;
    }
}

int main() {
    // --- Initialize Shared Memory and Event ---
    const char* sharedMemoryName = "Global\\GyroSharedMemory";
    const char* eventName = "Global\\GyroDataUpdatedEvent";

    // Set Ctrl+C handler
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        printf("ERROR: Could not set control handler; cleanup might not run on exit.\n");
        // Continue execution, but warn the user
    }

    g_hMapFile = OpenFileMappingA(
        FILE_MAP_READ,          // Read access
        FALSE,                  // Do not inherit the name
        sharedMemoryName);      // Name of mapping object

    if (g_hMapFile == NULL) {
        // Try creating if it doesn't exist (though Rust should create it)
        g_hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,   // Use paging file
            NULL,                   // Default security
            PAGE_READWRITE,         // Read/write access
            0,                      // Maximum object size (high-order DWORD)
            sizeof(SharedGyroData), // Maximum object size (low-order DWORD)
            sharedMemoryName);      // Name of mapping object

        if (g_hMapFile == NULL) {
            printf("Could not create or open file mapping object (%d).\n", GetLastError());
            return -1;
        }
        printf("Created file mapping object: %s\n", sharedMemoryName);
    } else {
        printf("Opened existing file mapping object: %s\n", sharedMemoryName);
    }

    g_pSharedData = (volatile SharedGyroData*)MapViewOfFile(
        g_hMapFile,             // Handle to map object
        FILE_MAP_READ,          // Read access
        0,                      // High-order DWORD of file offset
        0,                      // Low-order DWORD of file offset
        sizeof(SharedGyroData)); // Map the entire structure

    if (g_pSharedData == NULL) {
        printf("Could not map view of file (%d).\n", GetLastError());
        CloseHandle(g_hMapFile);
        return -1;
    }
    printf("Mapped view of file.\n");

    g_hDataEvent = OpenEventA(
        EVENT_MODIFY_STATE | SYNCHRONIZE, // Request modify state + wait
        FALSE,                            // Do not inherit the handle
        eventName);                       // Name of the event object

    if (g_hDataEvent == NULL) {
        // Try creating if it doesn't exist
        g_hDataEvent = CreateEventA(
            NULL,           // Default security attributes
            FALSE,          // Auto-reset
            FALSE,          // Initial state is nonsignaled
            eventName);     // Name of the event

        if (g_hDataEvent == NULL) {
            printf("Could not create or open event object (%d).\n", GetLastError());
            UnmapViewOfFile((LPCVOID)g_pSharedData);
            CloseHandle(g_hMapFile);
            return -1;
        }
        printf("Created event object: %s\n", eventName);
    } else {
        printf("Opened existing event object: %s\n", eventName);
    }
    // --- End Shared Memory and Event Init ---

    // Allocate client
    g_client = vigem_alloc();
    if (!g_client) {
        printf("ViGEm client allocation failed!\n");
        cleanup();
        return -1;
    }

    // Connect to bus
    VIGEM_ERROR err_connect = vigem_connect(g_client);
    if (!VIGEM_SUCCESS(err_connect)) {
        printf("ViGEm bus connection failed! Error code: 0x%X\n", err_connect);
        cleanup();
        return -1;
    }

    // Allocate DS4 target
    g_ds4 = vigem_target_ds4_alloc();
    if (!g_ds4) {
        printf("DS4 target allocation failed!\n");
        cleanup();
        return -1;
    }

    // Add DS4 target
    VIGEM_ERROR err_add = vigem_target_add(g_client, g_ds4);
    if (!VIGEM_SUCCESS(err_add)) {
        printf("DS4 target add failed! Error code: 0x%X\n", err_add);
        cleanup();
        return -1;
    }

    printf("Virtual DS4 controller added successfully. Sending initial button press...\n");

    // --- Send Initial Button Press (e.g., PS Button) ---
    DS4_REPORT_EX initialReport = { 0 };
    initialReport.Report.wButtons = DS4_BUTTON_SPECIAL; // Press PS button
    initialReport.Report.bThumbLX = 128; // Keep thumbs centered
    initialReport.Report.bThumbLY = 128;
    initialReport.Report.bThumbRX = 128;
    initialReport.Report.bThumbRY = 128;
    vigem_target_ds4_update_ex(g_client, g_ds4, initialReport);
    Sleep(100); // Wait 100ms
    initialReport.Report.wButtons = 0; // Release PS button
    vigem_target_ds4_update_ex(g_client, g_ds4, initialReport);
    printf("Initial button press sent.\n");
    // --- End Initial Button Press ---

    printf("Virtual DS4 controller active. Waiting for data from Rust via shared memory.\n");
    printf("Press Ctrl+C to exit...\n");

    // Initialize extended report structure
    DS4_REPORT_EX ds4ReportEx = { 0 };
    ds4ReportEx.Report.bThumbLX = 128; 
    ds4ReportEx.Report.bThumbLY = 128;
    ds4ReportEx.Report.bThumbRX = 128;
    ds4ReportEx.Report.bThumbRY = 128;
    ds4ReportEx.Report.wButtons = (USHORT)DS4_BUTTON_DPAD_NONE;
    ds4ReportEx.Report.bTriggerL = 0;
    ds4ReportEx.Report.bTriggerR = 0;

    // Variable to track the last time gyro data was logged for diagnostics
    auto last_diag_log_time = std::chrono::steady_clock::now();

    while (true) {
        // 每次循环都等待事件，超时时间可根据需要自行调整（这里为 10ms）
        DWORD waitResult = WaitForSingleObject(g_hDataEvent, 10); 

        if (waitResult == WAIT_OBJECT_0) {
            // Event signaled, try reading data
            LONG64 seq1, seq2;
            SharedGyroData localData;

            seq1 = g_pSharedData->update_sequence; // Read sequence number first
            // 若序列号为奇数，说明写进程尚未完成写入
            if (seq1 % 2 != 0) {
                Sleep(1);
                continue;
            }

            // 拷贝数据到本地
            localData.gyro_x    = g_pSharedData->gyro_x;
            localData.gyro_y    = g_pSharedData->gyro_y;
            localData.gyro_z    = g_pSharedData->gyro_z;
            localData.accel_x   = g_pSharedData->accel_x;
            localData.accel_y   = g_pSharedData->accel_y;
            localData.accel_z   = g_pSharedData->accel_z;
            localData.timestamp = g_pSharedData->timestamp;
            localData.is_aiming = g_pSharedData->is_aiming;

            seq2 = g_pSharedData->update_sequence; // Read sequence number again

            // 如果两次序列号相等且为偶数，说明本次读取有效
            if (seq1 == seq2) {
                // 更新 DS4 报告
                // --- Map Standard Tablet Coords (from Rust Shared Mem) to DS4 Coords --- 
                // Assumes Rust writes: gyro_x=StdPitch(+Up), gyro_y=StdYaw(+Right), gyro_z=StdRoll(+Right)
                // DS4 Expects: wGyroX=Pitch(+Down), wGyroY=Yaw(+Right), wGyroZ=Roll(+Right)
                ds4ReportEx.Report.wGyroX = -localData.gyro_x; // DS4 Pitch (+Down) = -StdPitch (+Up)
                ds4ReportEx.Report.wGyroY = localData.gyro_z;  // DS4 Yaw (+Right) = StdRoll (+Right)
                ds4ReportEx.Report.wGyroZ = localData.gyro_y; // DS4 Roll/Aim (+Right) = StdYaw (+Right)
                // --- End Mapping --- 

                ds4ReportEx.Report.wAccelX = localData.accel_x;
                ds4ReportEx.Report.wAccelY = localData.accel_y;
                ds4ReportEx.Report.wAccelZ = localData.accel_z;

                ds4ReportEx.Report.wTimestamp = localData.timestamp;

                // 根据是否在瞄准状态设置 L2 扳机（模拟或全按下）
                ds4ReportEx.Report.bTriggerL = (localData.is_aiming != 0) ? 255 : 0;

                // --- Log Gyro Data every 5 seconds ---
                auto current_time = std::chrono::steady_clock::now();
                // Use a shorter interval for diagnostics
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_diag_log_time).count();

                // Log roughly every 200ms when data is valid
                if (elapsed_ms >= 200) {
                    // Only log RAW data for diagnosis
                    printf("[DIAG LOG] Seq: %lld, Raw X=%d, Y=%d, Z=%d\n",
                           seq1, // Include sequence number for context
                           localData.gyro_x,
                           localData.gyro_y,
                           localData.gyro_z);
                    // Remove the DS4 log during diagnosis to reduce noise
                    // printf("[DS4 GYRO LOG] Sending Gyro X=%d, Y=%d, Z=%d\n",
                    //        ds4ReportEx.Report.wGyroX,
                    //        ds4ReportEx.Report.wGyroY,
                    //        ds4ReportEx.Report.wGyroZ);
                    last_diag_log_time = current_time; // Update last log time
                }
                // --- End Log Gyro Data ---
            }
            // 即使数据无效或序列号不匹配，也可不更新 ds4ReportEx
            // 下面的 update_ex 会发出上一次保持的有效数据
        }
        else if (waitResult == WAIT_TIMEOUT) {
            // 超时：Rust 在 10ms 内未写新数据
            // 可以选择什么都不做，或者继续发送上一次的 ds4ReportEx
            // 以下示例：直接使用上一次的有效数据保持输出
        }
        else {
            // WaitForSingleObject 发生错误或被系统终止
            printf("WaitForSingleObject failed (%d). Exiting loop.\n", GetLastError());
            break;
        }

        // 不管是否得到新数据，都发送一次 DS4 报告来"保活"，确保手柄在游戏中持续有效
        vigem_target_ds4_update_ex(g_client, g_ds4, ds4ReportEx);

        // 稍作休眠（参考正确代码在 100Hz 左右）
        Sleep(10);
    }

    cleanup();
    return 0; 
}
