// NegotiateProxy.cpp

#include "stdafx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Credui.lib")

// Define parameters
#define BUFF_SIZE	12*1024 // buffer size

PSEC_WINNT_AUTH_IDENTITY_OPAQUE pAuthIdentity = NULL; // The structure for storing user data entered

SECURITY_STATUS getToken(char **negotiateToken, WCHAR *hostName)
{
	//----------------------------------------------------------------------
	unsigned long cPackages;
	PSecPkgInfo PackageInfo;
	SECURITY_STATUS stat;

	// Get the name of the package
	stat = EnumerateSecurityPackages(
		&cPackages,
		&PackageInfo);

	if (stat != SEC_E_OK)
	{
		wprintf(
			L"Security function failed with error: %#x\n",
			stat);
		return stat;
	}

	SEC_WCHAR *szPackage = PackageInfo->Name;
	unsigned long m_cbMaxToken = PackageInfo->cbMaxToken;

	PBYTE m_pOutBuf = (PBYTE)malloc(m_cbMaxToken);

	//--------------------------------------------------------------------

	SEC_WCHAR targetName[NI_MAXHOST];

	// Get domain name
	WCHAR *domain = new WCHAR[wcslen(hostName) + 1], *pDomain = domain;

	for (int i = wcslen(hostName), point = 0; i >= 0; i--)
	{
		point += (hostName[i] == L'.');
		if (point == 2)
		{
			domain = &domain[i + 1];
			break;
		}
		domain[i] = towupper(hostName[i]);
	}

	// Set target name
	_snwprintf_s(
		targetName,
		NI_MAXHOST,
		_TRUNCATE,
		L"HTTP/%s@%s",
		hostName,
		domain);

	delete[] pDomain;

	//--------------------------------------------------------------------
	CredHandle hCredential;
	TimeStamp tsExpiry;

	stat = AcquireCredentialsHandle(
		NULL,
		szPackage,
		SECPKG_CRED_OUTBOUND,
		NULL,
		pAuthIdentity,
		NULL,
		NULL,
		&hCredential,
		&tsExpiry);

	if (stat != SEC_E_OK)
		{
			wprintf(
				L"Credentials function failed with error: %#x\n",
				stat);
			return stat;
		}

	//--------------------------------------------------------------------

	CtxtHandle		m_hCtxt;
	SecBufferDesc	outSecBufDesc;
	SecBuffer		outSecBuf;

	unsigned long	fContextAttr;


	// prepare output buffer
	outSecBufDesc.ulVersion = 0;
	outSecBufDesc.cBuffers = 1;
	outSecBufDesc.pBuffers = &outSecBuf;
	outSecBuf.cbBuffer = m_cbMaxToken;
	outSecBuf.BufferType = SECBUFFER_TOKEN;
	outSecBuf.pvBuffer = m_pOutBuf;

	stat = InitializeSecurityContext(
		&hCredential,
		NULL,
		targetName,
		ISC_REQ_CONFIDENTIALITY,
		0,	// reserved1
		SECURITY_NATIVE_DREP,
		NULL,
		0,	// reserved2
		&m_hCtxt,
		&outSecBufDesc,
		&fContextAttr,
		&tsExpiry);

	FreeCredentialsHandle(&hCredential);

	switch (stat)
	{
	case SEC_I_CONTINUE_NEEDED: 
	case SEC_I_COMPLETE_AND_CONTINUE: CompleteAuthToken(&m_hCtxt, &outSecBufDesc); break;
	case SEC_E_OK: break;
	default: return stat;
	}

	DeleteSecurityContext(&m_hCtxt);
	FreeContextBuffer(PackageInfo);

	if (outSecBuf.cbBuffer)
		{
			base64_encode(reinterpret_cast < char* > (m_pOutBuf), outSecBuf.cbBuffer, negotiateToken);
		}
	else
	{
		return -1;
	}

	if (strlen(*negotiateToken) < 500)
	{
		CREDUI_INFO creduiInfo = { 0 };
		creduiInfo.cbSize = sizeof(creduiInfo);
		// Change the message text and caption to the actual text for your dialog.
		SEC_WCHAR message[NI_MAXHOST];

		_snwprintf_s(
			message,
			NI_MAXHOST,
			_TRUNCATE,
			L"Enter your username and password to connect to %s",
			hostName);

		creduiInfo.pszCaptionText = L"Connect to the proxy-server";
		creduiInfo.pszMessageText = message;

		BOOL fSave = TRUE; // Checkmark "Remember user"

		stat = SspiPromptForCredentials(
			targetName,
			&creduiInfo,
			0,
			szPackage,
			NULL,
			&pAuthIdentity,
			&fSave,
			0);

		if (stat != SEC_E_OK)
		{
			wprintf(
				L"Authentification failed with error: %u\n",
				stat);
			//return stat;
		}
			
	}

	return SEC_E_OK;
}

int setAuthBuf(char(&buff)[BUFF_SIZE], char *previousMessage, WCHAR *hostName)
{
	char *negotiateToken = new char[2048];
	if (getToken(&negotiateToken, hostName) == SEC_E_OK)
	{
		memset(buff, 0, sizeof(buff));
		previousMessage[strlen(previousMessage)-2] = 0;
		_snprintf_s(
			buff,
			BUFF_SIZE,
			_TRUNCATE,
			"%s%s\r\n%s %s\r\n\r\n",
			previousMessage,
			"Proxy-Connection: Keep-Alive",
			"Proxy-Authorization: Negotiate",
			negotiateToken
		);
		delete[] negotiateToken;
		return strlen(buff);
	}
	else
	{
		delete[] negotiateToken;
		return -1;
	}
}

int transmit(SOCKET *sockFrom, SOCKET *sockTo, bool toProxy, char **previousMessages, DWORD index, WCHAR *hostName, sockaddr *remoteAddr, WSAEVENT *Event)//char(&buff)[BUFF_SIZE]
{
	char buff[BUFF_SIZE];
	memset(buff, 0, BUFF_SIZE);

	char str[NI_MAXHOST];
	char firstWord[NI_MAXHOST];
	char secondWord[NI_MAXHOST];
	
	int len = recv(*sockFrom, buff, BUFF_SIZE, 0);
	if (len < 0) return len;

	if (toProxy)
	{
		if (previousMessages[index] == NULL)
		{
			previousMessages[index] = new char[BUFF_SIZE];
		}
		else
		{
			memset(previousMessages[index], 0, BUFF_SIZE);
		}
		memcpy_s(previousMessages[index], BUFF_SIZE, buff, len+1);
	}
	
	else // if From Proxy
	{
		int strOffset = 0;
		int wordOffset = 0;

		getWord(buff, len, firstWord, 4, wordOffset);

		if (!_stricmp(firstWord, "http"))
		{
			wordOffset = 9;// getWord(buff, len, firstWord, len, wordOffset);
			getWord(buff, len, secondWord, 3, wordOffset);

			if (!_stricmp(secondWord, "407"))
			{
				// Get proxy-authentification method
				//-----------------------------------------
				do
				{
					strOffset = getString(buff, len, str, len, strOffset);
					wordOffset = getWord(str, strlen(str), firstWord, strlen(str), 0);

					if (strOffset < 0)
					{
						printf("Authentication method is not defined\n");
						return -1;
					}
				} while (_stricmp(firstWord, "proxy-authenticate:"));

				getWord(str, strlen(str), secondWord, strlen(str), wordOffset);
				//-----------------------------------------

				if (!_stricmp(secondWord, "negotiate"))
				{
					len = setAuthBuf(buff, previousMessages[index - 1], hostName);
					if (len < 0)
					{
						printf("setAuthBuf error\n");
						return -1;
					}

					closesocket(*sockFrom);
					WSACloseEvent(*Event);

					*sockFrom = socket(AF_INET, SOCK_STREAM, 0);
					if (INVALID_SOCKET == *sockFrom) wprintf(L"INVALID_SOCKET ERROR!!!\n");

					int iResult = connect(*sockFrom, remoteAddr, sizeof(SOCKADDR));
					if (iResult != 0)
					{
						wprintf(L"Connection failed with error: %d\n", WSAGetLastError());
						return -1;
					}


					if (createEv(Event)) return -1;

					iResult = WSAEventSelect(*sockFrom, *Event, FD_READ | FD_CLOSE);
					if (iResult != 0)
					{
						wprintf(L"WSAEventSelect failed with error: %d\n", WSAGetLastError());
						return -1;
					}

					len = send(*sockFrom, buff, len, 0);
					if (len < 0)
					{
						printf("send error %d\n", WSAGetLastError());
						return -1;
					}
					memset(buff, 0, BUFF_SIZE);
					return 0;

				}
				else
				{
					printf("Proxy authentification method is not negotiate\n");
					return -1;
				}
			}
		}
	}

	len = send(*sockTo, buff, len, 0);
	//wprintf(L"\r%s %i bytes%s      ", toProxy ? L"--> Send to proxy" : L"<-- Send to client", len, len<0 ? L". Transmission failed\n" : L"");
	len<0 ? printf("\nTransmission failed\n") : NULL;
	return len;
}

int _tmain(int argc, _TCHAR* argv[])
{
	// Get the command line options
	WCHAR *lPort, *rPort, *rAddr;
	int i = 1;
	argv[i] ? NULL : info(argv[0]);

	if (!_wcsicmp(argv[i], L"lp"))
	{
		(lPort = argv[++i]) ? NULL : info(argv[0]);
		(rAddr = argv[++i]) ? NULL : info(argv[0]);
		(rPort = argv[++i]) ? NULL : info(argv[0]);
	}
	else
	{
		lPort = L"3128";
		(rAddr = argv[i++]) ? NULL : info(argv[0]);
		(rPort = argv[i++]) ? NULL : info(argv[0]);
	}

	if (WSAInit()) return -1;
	//----------------------------------------------------------------------------------------------------

	ADDRINFOT hints, *remoteAddr = NULL, *inetAddr = NULL;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int iResult = GetAddrInfo(rAddr, rPort, &hints, &remoteAddr);
	if (iResult != 0)
	{
		switch (iResult)
		{
		case WSAHOST_NOT_FOUND: wprintf(L"Host not found\n"); break;
		case WSANO_DATA: wprintf(L"No data record found\n"); break;
		default: wprintf(L"Gethost function failed with error: %i\n", iResult);
		}

		return -1;
	}

	WCHAR host[NI_MAXHOST];
	iResult = GetNameInfo(remoteAddr->ai_addr, sizeof(SOCKADDR), host, NI_MAXHOST, NULL, 0, NI_NAMEREQD);
	if (iResult != 0)
	{
		switch (iResult)
		{
		case EAI_AGAIN:		wprintf(L"The name can not be determined at the moment\n"); break;
		case EAI_BADFLAGS:	wprintf(L"The flags parameter has an invalid value\n"); break;
		case EAI_FAIL:		wprintf(L"Name info failed\n"); break;
		case EAI_FAMILY:	wprintf(L"It does not recognize the address family\n"); break;
		case EAI_MEMORY:	wprintf(L"Not enough memory\n"); break;
		case EAI_NONAME:	wprintf(L"The name can not be determined\n"); break;
		default:			wprintf(L"Nameinfo function failed with error: %i\n", iResult);
		}

		return -1;
	}

	// Create a listening socket
	SOCKET ListenSocket = newSock();
	if (ListenSocket == INVALID_SOCKET) return -1;

	// Listen address
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	iResult = GetAddrInfo(NULL, lPort, &hints, &inetAddr);
	if (iResult != 0)
	{
		switch (iResult)
		{
		case WSAHOST_NOT_FOUND: wprintf(L"Host not found\n"); break;
		case WSANO_DATA: wprintf(L"No data record found\n"); break;
		default: wprintf(L"Gethost function failed with error: %i\n", iResult);
		}

		return -1;
	}

	//-------------------------
	// Bind the listening socket
	iResult = bind(ListenSocket, inetAddr->ai_addr, sizeof(SOCKADDR));
	if (iResult != 0)
	{
		wprintf(L"bind failed with error: %#x\n", WSAGetLastError());
		return -1;
	}

	//-------------------------
	// Create a new event
	WSAEVENT NewEvent;
	if (createEv(&NewEvent)) return -1;


	//-------------------------
	// Associate event types FD_ACCEPT and FD_CLOSE
	// with the listening socket and NewEvent
	iResult = WSAEventSelect(ListenSocket, NewEvent, FD_ACCEPT);
	if (iResult != 0)
	{
		wprintf(L"WSAEventSelect failed with error: %#x\n", WSAGetLastError());
		return -1;
	}

	//-------------------------
	// Start listening on the socket
	iResult = listen(ListenSocket, 0);
	if (iResult != 0)
	{
		wprintf(L"listen failed with error: %#x\n", WSAGetLastError());
		return -1;
	}
	wprintf(L"NegotiateProxy listen on port %s\n", lPort);

	//-------------------------
	// Add the socket and event to the arrays, increment number of events
	SOCKET SocketArray[WSA_MAXIMUM_WAIT_EVENTS];
	WSAEVENT EventArray[WSA_MAXIMUM_WAIT_EVENTS];

	DWORD EventTotal = 0;
	SocketArray[EventTotal] = ListenSocket;
	EventArray[EventTotal++] = NewEvent;

	//bool needAuth[WSA_MAXIMUM_WAIT_EVENTS];
	//memset(needAuth, true, sizeof(needAuth));
	bool toProxy;
	DWORD secondIndex;

	DWORD index;
	SOCKET ClientSocket, ProxySocket;
	WSANETWORKEVENTS NetworkEvents;

	// The buffers for the reception - transmission
	//char prBuff[BUFF_SIZE], clBuff[BUFF_SIZE];
	int connections = 0;

	char* previousMessagesArray[WSA_MAXIMUM_WAIT_EVENTS];
	memset(previousMessagesArray, NULL, WSA_MAXIMUM_WAIT_EVENTS);
	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; previousMessagesArray[i] = NULL, i++) {}
	
	while (1)
	{

		// Wait for network events on all sockets
		index = WSAWaitForMultipleEvents(EventTotal, EventArray, FALSE, WSA_INFINITE, FALSE);
		index -= WSA_WAIT_EVENT_0;

		if ((index != WSA_WAIT_FAILED) && (index != WSA_WAIT_TIMEOUT))
		{
			WSAEnumNetworkEvents(SocketArray[index], EventArray[index], &NetworkEvents);
			WSAResetEvent(EventArray[index]);

			toProxy = (index % 2) ? true : false;
			secondIndex = toProxy ? index + 1 : index - 1;

			if ((NetworkEvents.lNetworkEvents & FD_ACCEPT) && (NetworkEvents.iErrorCode[FD_ACCEPT_BIT] == 0))
			{

				ClientSocket = accept(ListenSocket, NULL, NULL);
				if (INVALID_SOCKET == ClientSocket) wprintf(L"Invalid socket\n");

				ProxySocket = socket(AF_INET, SOCK_STREAM, 0);
				if (INVALID_SOCKET == ProxySocket) wprintf(L"INVALID_SOCKET ERROR!!!\n");

				iResult = connect(ProxySocket, remoteAddr->ai_addr, sizeof(SOCKADDR));
				if (iResult != 0)
				{
					wprintf(L"Connection failed with error: %d\n", WSAGetLastError());
					return -1;
				}

				printConnections(++connections);

				if (createEv(&NewEvent)) return -1;

				SocketArray[EventTotal] = ClientSocket;
				EventArray[EventTotal] = NewEvent;

				//-------------------------
				// Associate event types FD_READ and FD_CLOSE
				// with the client socket and NewEvent
				iResult = WSAEventSelect(SocketArray[EventTotal], EventArray[EventTotal], FD_READ | FD_CLOSE);
				if (iResult != 0)
				{
					wprintf(L"WSAEventSelect failed with error: %d\n", WSAGetLastError());
					return -1;
				}

				EventTotal++;

				if (createEv(&NewEvent)) return -1;

				SocketArray[EventTotal] = ProxySocket;
				EventArray[EventTotal] = NewEvent;

				//-------------------------
				// Associate event types FD_READ and FD_CLOSE
				// with the proxy socket and NewEvent
				iResult = WSAEventSelect(SocketArray[EventTotal], EventArray[EventTotal], FD_READ | FD_CLOSE);
				if (iResult != 0)
				{
					wprintf(L"WSAEventSelect failed with error: %d\n", WSAGetLastError());
					return -1;
				}

				EventTotal++;
			}

			if ((NetworkEvents.lNetworkEvents & FD_READ) && (NetworkEvents.iErrorCode[FD_READ_BIT] == 0))
			{
				// transfers data
				iResult = transmit(
					&SocketArray[index],
					&SocketArray[secondIndex],
					toProxy,
					previousMessagesArray,
					index,
					host,
					remoteAddr->ai_addr,
					&EventArray[index]);
				if (iResult < 0) return -1;
			}

			if (NetworkEvents.lNetworkEvents & FD_CLOSE)
			{
				closesocket(SocketArray[secondIndex]);
				closesocket(SocketArray[index]);

				WSACloseEvent(EventArray[secondIndex]);
				WSACloseEvent(EventArray[index]);

				// Move from the top to the free place
				SocketArray[index] = toProxy ? SocketArray[EventTotal - 2] : SocketArray[EventTotal - 1];
				SocketArray[secondIndex] = toProxy ? SocketArray[EventTotal - 1] : SocketArray[EventTotal - 2];

				EventArray[index] = toProxy ? EventArray[EventTotal - 2] : EventArray[EventTotal - 1];
				EventArray[secondIndex] = toProxy ? EventArray[EventTotal - 1] : EventArray[EventTotal - 2];
				EventTotal -= 2;

				printConnections(--connections);
			}
		}
	}
	return 0;
}