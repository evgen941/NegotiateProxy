#pragma once

WCHAR* buildTargetName(WCHAR *hostName);

size_t base64_encode(const char *inp, size_t insize, char **outptr);

int WSAInit();

int createEv(WSAEVENT *Event);

SOCKET newSock();

void info(_TCHAR *progName);

int getString(char *inBuff, int inBuffLen, char *outBuff, int len, int offset);

int getWord(char *inBuff, int inBuffLen, char *outBuff, int len, int offset);

//void getSecondWord(char *inBuff, char *outBuff, int inBuffLen);

void printConnections(int connections);