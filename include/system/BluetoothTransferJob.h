#pragma once

#include <stdint.h>

/** Future Bluetooth Classic SPP file transfer job (chunked SD writes). */
enum class BtTransferPhase : uint8_t {
    Idle = 0,
    AwaitingConnection,
    Receiving,
    QueuedForSd,
    Writing,
    Complete,
    Error,
    PausedForAzan,
};

struct BluetoothTransferJob {
    char destPath[64]{"/azan/"};
    uint32_t totalBytes = 0;
    uint32_t receivedBytes = 0;
    uint16_t chunkSeq = 0;
    BtTransferPhase phase = BtTransferPhase::Idle;
    uint8_t progressPct = 0;
    char errorMsg[32]{};
};
