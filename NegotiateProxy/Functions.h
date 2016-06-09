#pragma once

size_t base64_encode(const char *inp, size_t insize, char **outptr);

int WSAInit();

int createEv(WSAEVENT *Event);

SOCKET newSock();

void info(_TCHAR *progName);

int getSecondWord(char *inBuff, char *outBuff, int inBuffLen);