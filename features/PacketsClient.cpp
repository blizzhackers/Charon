#include "headers/feature.h"
#include "headers/common.h"
#include "headers/hook.h"
#include "headers/remote.h"
#include "headers/script_game.h"

// SID (SERVER -> CLIENT)
ASMPTR NET_SID_CLIENT_IncomingPacketHandler_Call = 0x51c7af;
REMOTEFUNC(void __fastcall, NET_SID_CLIENT_IncomingPacketHandler, (BYTE nPacketId, BYTE* pBytes, size_t nSize), 0x521b00);

void __fastcall NET_SID_CLIENT_IncomingPacketHandler_Replacement(BYTE nPacketId, BYTE* pBytes, size_t nSize)
{
    NET_SID_CLIENT_IncomingPacketHandler(nPacketId, pBytes, nSize);

    if (Settings["debugPackets"]) {
        gamelog << COLOR(2) << "SID " << COLOR(4) << "(SERVER -> CLIENT)" << COLOR(0) << " 0x" << std::hex << nPacketId << std::dec << " size of " << nSize << std::endl;
    }
}

// SID (CLIENT -> SERVER)
ASMPTR NET_SID_CLIENT_Send_Original = 0x51c5c0, NET_SID_CLIENT_Send_Rejoin = 0x51c5c8;

__declspec(naked) void __fastcall NET_SID_CLIENT_Send_Relocated(INT nPacketId, BYTE* pBytes, size_t nSizeNoHeader) {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, 0x200C
        jmp NET_SID_CLIENT_Send_Rejoin
    }
}

void __fastcall NET_SID_CLIENT_Send_Hook(INT nPacketId, BYTE* pBytes, size_t nSizeNoHeader)
{
    NET_SID_CLIENT_Send_Relocated(nPacketId, pBytes, nSizeNoHeader);

    if (Settings["debugPackets"]) {
        void** puEBP = NULL;
        __asm { mov puEBP, ebp };
        void* pvReturn = puEBP[1];

        puEBP = (void**)puEBP[0];
        void* pvReturn2 = puEBP[1];

        printf("SID  (CLIENT -> SERVER) Packet ID: 0x%02x -> Caller: 0x%08x, Caller's Caller 0x%08x \n", (BYTE)nPacketId, (int)pvReturn, (int)pvReturn2);

        gamelog << COLOR(1) << "SID " << COLOR(4) << "(CLIENT -> SERVER)" << COLOR(0) << " 0x" << std::hex << (BYTE)nPacketId << std::dec << " size of " << nSizeNoHeader << std::endl;
    }
}

// MCP (SERVER -> CLIENT)
ASMPTR NET_MCP_CLIENT_IncomingPacketsHandler_Call = 0x449ba9;
REMOTEFUNC(void __fastcall, HandleIncomingPacket, (BYTE* pBytes, size_t nSize), 0x44b190);

void __fastcall NET_MCP_CLIENT_IncomingPacketsHandler_Replacement(BYTE* pBytes, size_t nSize)
{
    HandleIncomingPacket(pBytes, nSize);

    if (Settings["debugPackets"]) {
        gamelog << COLOR(2) << "MCP " << COLOR(4) << "(SERVER -> CLIENT)" << COLOR(0) << " 0x" << std::hex << pBytes[0] << std::dec << " size of " << nSize << std::endl;
    }
}

// MCP (CLIENT -> SERVER)
ASMPTR NET_MCP_CLIENT_Send_Original = 0x6bc8a0, NET_MCP_CLIENT_Send_Rejoin = 0x6bc8a8;

__declspec(naked) void __stdcall  NET_MCP_CLIENT_Send_Relocated(void* pQSocket, BYTE* szData, short nDataSizeNoHeader) {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, 0x2070
        jmp NET_MCP_CLIENT_Send_Rejoin
    }
}

void __stdcall NET_MCP_CLIENT_Send_Hook(void* pQSocket, BYTE* pBytes, short nDataSizeNoHeader)
{
    NET_MCP_CLIENT_Send_Relocated(pQSocket, pBytes, nDataSizeNoHeader);

    if (Settings["debugPackets"]) {
        void** puEBP = NULL;
        __asm { mov puEBP, ebp };
        void* pvReturn = puEBP[1];

        puEBP = (void**)puEBP[0];
        void* pvReturn2 = puEBP[1];

        printf("MCP  (CLIENT -> SERVER) Packet ID: 0x%02x -> Caller: 0x%08x, Caller's Caller 0x%08x \n", pBytes[0], (int)pvReturn, (int)pvReturn2);

        gamelog << COLOR(1) << "MCP " << COLOR(4) << "(CLIENT -> SERVER)" << COLOR(0) << " 0x" << std::hex << pBytes[0] << std::dec << " size of " << nDataSizeNoHeader << std::endl;
    }
}

// D2GS (SERVER -> CLIENT)
ASMPTR NET_D2GS_CLIENT_PacketHandle_to0xAF_Call = 0x44c73b;
REMOTEFUNC(void __fastcall, NET_D2GS_CLIENT_PacketHandle_to0xAF, (BYTE* pBytes, size_t nSize), 0x45f7b0);

ASMPTR NET_D2GS_CLIENT_PacketHandle_from0xAF_Call1 = 0x44c70c;
ASMPTR NET_D2GS_CLIENT_PacketHandle_from0xAF_Call2 = 0x44bb4e;
ASMPTR NET_D2GS_CLIENT_PacketHandle_from0xAF_Call3 = 0x44bd5a;
REMOTEFUNC(void __fastcall, NET_D2GS_CLIENT_PacketHandle_from0xAF, (BYTE* pBytes, size_t nSize), 0x45c850);

namespace {

struct ClientPacketEntry {
    void(__fastcall* handler)(BYTE*); // 00 per-packet callback, packet in ECX
    uint32_t size;                   // 04 validated by the native dispatcher
    void* unitHandler;               // 08 optional unit callback
};
static_assert(sizeof(ClientPacketEntry) == 12 && offsetof(ClientPacketEntry, handler) == 0
    && offsetof(ClientPacketEntry, size) == 4 && offsetof(ClientPacketEntry, unitHandler) == 8,
    "1.14d incoming client packet table layout");

// 0x45D370 reads type@1, GUID@2, action@6, and 96 quest bytes@7; ECX input, plain RET.
// Type 6 calls 0x4B6DD0, which applies the record to client buffer [0x7C0D43] before RET 8.
void __fastcall QuestRecordPacket(BYTE* packet) {
    const bool refreshed = packet[1] == 6;
    reinterpret_cast<void(__fastcall*)(BYTE*)>(0x45D370)(packet);
    if (refreshed) ++Game::QuestRecordRevision;
}

}

void __fastcall NET_D2GS_CLIENT_PacketHandler_Replacement(BYTE* pBytes, size_t nSize)
{
    // The native low-opcode dispatcher treats -1 as a no-op, without reading pBytes.
    if (nSize == 0xffffffff || !pBytes || nSize == 0) {
        NET_D2GS_CLIENT_PacketHandle_to0xAF(pBytes, nSize);
        return;
    }
    if (Settings["debugPackets"])
        gamelog << COLOR(2) << "D2GS " << COLOR(4) << "(SERVER -> CLIENT)" << COLOR(0) << " 0x" << std::hex << pBytes[0] << std::dec << " size of " << nSize << std::endl;

    if (CustomPackets::DispatchClientPacket(reinterpret_cast<char*>(pBytes), nSize)) return;

    if (pBytes[0] >= 0xAF) {
        NET_D2GS_CLIENT_PacketHandle_from0xAF(pBytes, nSize);
    }
    else
    {
        NET_D2GS_CLIENT_PacketHandle_to0xAF(pBytes, nSize);
    }
}

// D2GS (CLIENT -> SERVER)
ASMPTR NET_D2GS_CLIENT_SendPacketAfterChecks_Original = 0x52ae50, NET_D2GS_CLIENT_Send_Rejoin = 0x52ae62;
REMOTEREF(void*, pD2PacketsClientBuffer, 0x882b34);

__declspec(naked) void* __stdcall NET_D2GS_CLIENT_Send_Relocated(size_t nSize, int Unused, BYTE* pBytes) {
    // The reference stores the game's address; compare the game-owned pointer there.
    __asm {
        push ebp
        mov ebp, esp
        mov eax, pD2PacketsClientBuffer
        cmp dword ptr[eax], 0x0
        jnz back
        xor eax, eax
        pop ebp
        ret 0xc

        back:
        jmp NET_D2GS_CLIENT_Send_Rejoin
    }
}

void* __stdcall NET_D2GS_CLIENT_Send_Hook(size_t nSize, int Unused, BYTE* pBytes)
{
    void* ret = NET_D2GS_CLIENT_Send_Relocated(nSize, Unused, pBytes);

    if (Settings["debugPackets"]) {
        void** puEBP = NULL;
        __asm { mov puEBP, ebp };
        void* pvReturn = puEBP[1];

        puEBP = (void**)puEBP[0];
        void* pvReturn2 = puEBP[1];

        printf("D2GS (CLIENT -> SERVER) Packet ID: 0x%02x -> Caller: 0x%08x, Caller's Caller 0x%08x \n", pBytes[0], (int)pvReturn, (int)pvReturn2);

        gamelog << COLOR(1) << "D2GS " << COLOR(4) << "(CLIENT -> SERVER)" << COLOR(0) << " 0x" << std::hex << pBytes[0] << std::dec << " size of " << nSize << std::endl;
    }

    return ret;
}

namespace Template {

    class : public Feature {
    public:
        void init() {
            // SERVER -> CLIENT
                // SID
                MemoryPatch(NET_SID_CLIENT_IncomingPacketHandler_Call) << CALL(NET_SID_CLIENT_IncomingPacketHandler_Replacement);

                // MCP
                MemoryPatch(NET_MCP_CLIENT_IncomingPacketsHandler_Call) << CALL(NET_MCP_CLIENT_IncomingPacketsHandler_Replacement);

                // D2GS
                // Own the shared call site: logging and custom delivery use one dispatcher.
                MemoryPatch(NET_D2GS_CLIENT_PacketHandle_to0xAF_Call) << CALL(NET_D2GS_CLIENT_PacketHandler_Replacement);
                MemoryPatch(NET_D2GS_CLIENT_PacketHandle_from0xAF_Call1) << CALL(NET_D2GS_CLIENT_PacketHandler_Replacement);
                MemoryPatch(NET_D2GS_CLIENT_PacketHandle_from0xAF_Call2) << CALL(NET_D2GS_CLIENT_PacketHandler_Replacement);
                MemoryPatch(NET_D2GS_CLIENT_PacketHandle_from0xAF_Call3) << CALL(NET_D2GS_CLIENT_PacketHandler_Replacement);
                // 0x7116B0 is table[0x28].handler; preserve native 0x67-byte validation and batch dispatch.
                auto* incoming = reinterpret_cast<ClientPacketEntry*>(0x7114D0);
                MemoryPatch(reinterpret_cast<DWORD>(&incoming[0x28].handler)) << DATA(&QuestRecordPacket);

            // CLIENT -> SERVER
                // SID
                MemoryPatch(NET_SID_CLIENT_Send_Original) << JUMP(NET_SID_CLIENT_Send_Hook);
                
                // MCP
                MemoryPatch(NET_MCP_CLIENT_Send_Original) << JUMP(NET_MCP_CLIENT_Send_Hook);

                // D2GS
                MemoryPatch(NET_D2GS_CLIENT_SendPacketAfterChecks_Original) << JUMP(NET_D2GS_CLIENT_Send_Hook);
        }
    } feature;

}
