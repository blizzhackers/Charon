// Based on code by Jaenster

#include "headers/feature.h"
#include "headers/common.h"
#include "headers/remote.h"
#include "headers/hook.h"
#include "headers/ghidra.h"

REMOTEFUNC(void __fastcall, NET_D2GS_SERVER_ProcessClientMessage_InGame, (char* pBytes, size_t nSize), 0x53f3d0)
REMOTEFUNC(void __fastcall, NET_D2GS_CLIENT_PacketHandle_to0xAF, (char* pBytes, size_t nSize), 0x45f7b0)

struct ServerIncomingD2GSPacketEntry {
    void* pHandler;
    DWORD unknown;
};

struct ClientIncomingD2GSPacketEntry {
    void* pHandler;
    long size;
    void* pUnitHandler;
};

struct ClientOutgoingD2GSPacketSizeEntry {
    long size;
};

enum class eD2ServerIncomingStatus {
    OK = 0x0,
    BAD_TARGET = 0x1,
    BAD_DATA = 0x2,
    BAD_PARAMS = 0x3,
};

// These are protected locations, so you need to use MemoryPatch to change them
REMOTEPTR(ServerIncomingD2GSPacketEntry, NET_D2GS_SERVER_INCOMING, 0x6e0d18)
REMOTEPTR(ClientIncomingD2GSPacketEntry, NET_D2GS_CLIENT_INCOMING, 0x7114d0)
REMOTEPTR(long, NET_D2GS_CLIENT_INCOMING_SIZE, 0x730ae8)
REMOTEPTR(long, NET_D2GS_CLIENT_OUTGOING_SIZE, 0x730dc0)

void __fastcall ServerIncomingPacketHook_InGame (char* pBytes, int nSize) {
    CustomPacketHeader* header = (CustomPacketHeader*)(pBytes + 4);

    if (header->id == CUSTOM_PACKET_ID) {
        int clientId = *(int*)pBytes;
        for (Feature* f = Features; f; f = f->next) {
            f->serverGetCustomData(clientId, (char*)(header + 1), nSize - sizeof(CustomPacketHeader));
        }
    }
    else {
        NET_D2GS_SERVER_ProcessClientMessage_InGame(pBytes, nSize);
    }
}

void __fastcall ClientIncomingPacketHook_InGame(char* pBytes, int nSize) {
    CustomPacketHeader* header = (CustomPacketHeader*)pBytes;

    if (header->id == CUSTOM_PACKET_ID) {
        for (Feature* f = Features; f; f = f->next) {
            f->clientGetCustomData((char*)(header + 1), header->size - sizeof(CustomPacketHeader));
        }
    }
    else {
        NET_D2GS_CLIENT_PacketHandle_to0xAF(pBytes, nSize);
    }
}

void SetClientPacketHandler(char packetId, long size, void* handler = nullptr, void* unitHandler = nullptr) {
    MemoryPatch((DWORD)&NET_D2GS_CLIENT_INCOMING[packetId]) << DATA(ClientIncomingD2GSPacketEntry{
        handler,
        size,
        unitHandler,
    });
    MemoryPatch((DWORD)&NET_D2GS_CLIENT_INCOMING_SIZE[packetId]) << size;
}

void SetServerPacketHandler(char packetId, long size, void* handler = nullptr) {
    MemoryPatch((DWORD)&NET_D2GS_SERVER_INCOMING[packetId]) << DATA(ServerIncomingD2GSPacketEntry{
        handler,
        1,
    });
    MemoryPatch((DWORD)&NET_D2GS_CLIENT_OUTGOING_SIZE[packetId]) << size;
}

const ASMPTR NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Original = 0x52b920, NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Rejoin = 0x52b926;

__declspec(naked) size_t __fastcall NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Relocated(char* pBytes, size_t pBytesSize, size_t* pExpectedSize) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        mov esi, edx
        jmp NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Rejoin
    }
}

size_t __fastcall NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Intercept(char* pBytes, size_t pBytesSize, size_t* pExpectedSize) {
    CustomPacketHeader* header = (CustomPacketHeader*)pBytes;

    if (header->id == CUSTOM_PACKET_ID) {
        return *pExpectedSize = header->size;
    }

    return NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Relocated(pBytes, pBytesSize, pExpectedSize);
}

void ServerSendCustomData(Ghidra::D2ClientStrc* pClient, char* pBytes, size_t nSize) {
    const size_t MAX_SIZE = 0x200 - sizeof(CustomPacketHeader);
    char pWrapped[MAX_SIZE + sizeof(CustomPacketHeader)];
    CustomPacketHeader* header = (CustomPacketHeader*)pWrapped;

    if (nSize > MAX_SIZE) {
        throw "Packet size too large!";
    }

    memcpy(pWrapped + sizeof(CustomPacketHeader), pBytes, nSize);
    header->id = CUSTOM_PACKET_ID;
    header->size = (short)(nSize + sizeof(CustomPacketHeader));
    serverSendPacket(pClient, pWrapped, header->size);
}

void ClientSendCustomData(char* pBytes, size_t nSize) {
    const size_t MAX_SIZE = 0x200 - sizeof(CustomPacketHeader);
    char pWrapped[MAX_SIZE + sizeof(CustomPacketHeader)];
    CustomPacketHeader* header = (CustomPacketHeader*)pWrapped;

    if (nSize > MAX_SIZE) {
        throw "Packet size too large!";
    }

    memcpy(pWrapped + sizeof(CustomPacketHeader), pBytes, nSize);
    header->id = CUSTOM_PACKET_ID;
    header->size = (short)(nSize + sizeof(CustomPacketHeader));
    clientSendPacket(pWrapped, header->size);
}

namespace CustomPacket {

    class : public Feature {
    public:
        void init() {
            MemoryPatch(NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Original) << JUMP(NET_D2GS_CLIENT_GetIncomingPacketSizeFromTableAndVariableSize_Intercept) << ASM::NOP;
            MemoryPatch(0x52d03e) << CALL(ServerIncomingPacketHook_InGame);
            MemoryPatch(0x44c73b) << CALL(ClientIncomingPacketHook_InGame);
            SetClientPacketHandler(CUSTOM_PACKET_ID, -1); // Even if there's no handler we have to enable the packet on client side since it's unused.
            SetServerPacketHandler(CUSTOM_PACKET_ID, 1); // We skip the size check, so any size above zero will do. This just enables the packet for sending.
        }
    } feature;

}
