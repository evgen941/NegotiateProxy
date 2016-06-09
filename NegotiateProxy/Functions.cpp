#include "stdafx.h"

size_t base64_encode(const char *inp, size_t insize, char **outptr)
{
	// Base64 Encoding/Decoding Table
	static const char table64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	unsigned char ibuf[3];
	unsigned char obuf[4];
	int i;
	int inputparts;
	char *output;
	char *base64data;

	char *indata = (char *)inp;

	*outptr = NULL; // set to NULL in case of failure before we reach the end

	if (0 == insize)
		insize = strlen(indata);

	base64data = output = (char*)malloc(insize * 4 / 3 + 4);
	if (NULL == output)
		return 0;

	while (insize > 0)
	{
		for (i = inputparts = 0; i < 3; i++)
		{
			if (insize > 0)
			{
				inputparts++;
				ibuf[i] = *indata;
				indata++;
				insize--;
			}
			else
				ibuf[i] = 0;
		}

		obuf[0] = (unsigned char)((ibuf[0] & 0xFC) >> 2);
		obuf[1] = (unsigned char)(((ibuf[0] & 0x03) << 4) | \
			((ibuf[1] & 0xF0) >> 4));
		obuf[2] = (unsigned char)(((ibuf[1] & 0x0F) << 2) | \
			((ibuf[2] & 0xC0) >> 6));
		obuf[3] = (unsigned char)(ibuf[2] & 0x3F);

		switch (inputparts)
		{
		case 1: // only one byte read
			_snprintf_s(output, 5, _TRUNCATE, "%c%c==",
				table64[obuf[0]],
				table64[obuf[1]]);
			break;
		case 2: // two bytes read
			_snprintf_s(output, 5, _TRUNCATE, "%c%c%c=",
				table64[obuf[0]],
				table64[obuf[1]],
				table64[obuf[2]]);
			break;
		default:
			_snprintf_s(output, 5, _TRUNCATE, "%c%c%c%c",
				table64[obuf[0]],
				table64[obuf[1]],
				table64[obuf[2]],
				table64[obuf[3]]);
			break;
		}
		output += 4;
	}
	*output = 0;
	*outptr = base64data; // make it return the actual data memory

	return strlen(base64data); // return the length of the new data
}

int WSAInit()
{
	int iResult;
	WSADATA wsaData;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);

	if (iResult != 0)
	{
		wprintf(L"WSAStartup failed with error: %d\n", iResult);
	}

	return iResult;
}

int createEv(WSAEVENT *Event)
{
	*Event = WSACreateEvent();
	if (*Event == NULL)
	{
		wprintf(L"WSACreateEvent failed with error: %d\n", GetLastError());
		WSACleanup();
		return -1;
	}

	return 0;
}

SOCKET newSock()
{
	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
	{
		wprintf(L"socket function failed with error: %d\n", WSAGetLastError());
		WSACleanup();
	}

	return sock;
}

void info(_TCHAR *progName)
{
	wprintf(L"usage:  \n%s [LP <lPort>] <rAddr> <rPort>\n\n", progName);
	wprintf(L"lPort - the port on which the server listens, by default 3128\n");
	wprintf(L"rAddr - address of the proxy-server to which you are connecting\n");
	wprintf(L"rPort - proxy-server port to which you are connecting\n\n");
	wprintf(L"example: \n%s LP 8080 proxy.host.ru 3128\n", progName);
	exit(0);
}

int getSecondWord(char *inBuff, char *outBuff, int inBuffLen)
{
	int i, n, m;

	for (m = 0; (inBuff[m] != ' ') && (m<inBuffLen); m++) {}

	for (n = ++m; (inBuff[n] != ' ') && (n<inBuffLen); n++) {}

	for (i = 0; m < n; outBuff[i] = inBuff[m], i++, m++) {} outBuff[i] = 0;

	return 0;
}
