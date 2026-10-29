#define _USE_MATH_DEFINES

#pragma once

#include "D2Structs.h"
#include "ghidra.h"
#include "custom_packet.h"
#include <map>
#include <cmath>

extern DPOINT xvector, yvector;
void DrawRectangle(POINT a, POINT b, DWORD dwColor);
void DrawLine(POINT a, POINT b, DWORD dwColor);
void DrawDot(POINT pos, DWORD dwColor = 30);
int randIntInRange(int low, int high);
double randDoubleInRange(double low, double high);

void __stdcall serverSendPacket(Ghidra::D2ClientStrc* pClient, char* pBytes, size_t nSize);
void clientSendPacket(char* pBytes, size_t nSize);
void ServerSendCustomData(Ghidra::D2ClientStrc* pClient, char* pBytes, size_t nSize);
void ClientSendCustomData(char* pBytes, size_t nSize);

template <class T> void ServerSendCustomData(Ghidra::D2ClientStrc* pClient, T data) {
    ServerSendCustomData(pClient, (char*)&data, sizeof(T));
}

template <class T> void ClientSendCustomData(T data) {
    ClientSendCustomData((char*)&data, sizeof(T));
}

const char FLYING_TEXT_PACKET_ID = 0x1;

struct FlyingTextPacket {
    char subPacketId = FLYING_TEXT_PACKET_ID;
    DPOINT pos{ 0, 0 };
    char color = 0;
    int value = 0;
};

struct FlyingText {
	ULONGLONG counter;
	char color;
	int value;
	DWORD font = 4;
	DPOINT pos{ 0, 0 };
	DPOINT delta{ 0, 0 };

	FlyingText(DPOINT pos, int value, char color);
	FlyingText(FlyingTextPacket *packet);
};

extern std::vector<FlyingText> FlyingTexts;
