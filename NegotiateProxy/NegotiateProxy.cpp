// NegotiateProxy.cpp

#include "stdafx.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Credui.lib")

// Define parameters
#define BUFF_SIZE	12*1024 // buffer size
#define MAXCONN     10 // The number of connections to the proxy - server

PSEC_WINNT_AUTH_IDENTITY_OPAQUE pAuthIdentityEx2 = NULL; // The structure for storing user data entered

SECURITY_STATUS getToken(char **negotiateToken, WCHAR *hostName)
{
	//----------------------------------------------------------------------
	unsigned long cPackages;
	PSecPkgInfo PackageInfo;
	SECURITY_STATUS stat;

	// Get the name of the package
	stat = EnumerateSecurityPackages(
		&cPackages,
		&PackageInfo
	);

	if (stat != SEC_E_OK)
	{
		wprintf(
			L"Security function failed with error: %#x\n",
			stat
		);
		return stat;
	}

	SEC_WCHAR *szPackage = PackageInfo->Name;
	unsigned long m_cbMaxToken = PackageInfo->cbMaxToken;

	PBYTE m_pOutBuf = (PBYTE)malloc(m_cbMaxToken);
	BOOL m_fHaveCtxtHandle = false;

	//--------------------------------------------------------------------

	CREDUI_INFO creduiInfo = { 0 };
	creduiInfo.cbSize = sizeof(creduiInfo);
	// Change the message text and caption to the actual text for your dialog.
	creduiInfo.pszCaptionText = L"Enter the network password";
	creduiInfo.pszMessageText = L"Enter your username and password to connect to the proxy-server";

	SEC_WCHAR targetName[NI_MAXHOST];

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

	_snwprintf_s(
		targetName,
		NI_MAXHOST,
		_TRUNCATE,
		L"HTTP/%s@%s",
		hostName,
		domain);

	delete[] pDomain;

	BOOL fSave = FALSE; // Checkmark "Remember user"

	// Pass user authentication
	if (pAuthIdentityEx2 == NULL)
	{
		stat = SspiPromptForCredentials(
			targetName,
			&creduiInfo,
			0,
			szPackage,
			NULL,
			&pAuthIdentityEx2,
			&fSave,
			0
		);

		if (stat != SEC_E_OK)
		{
			wprintf(
				L"Authentification failed with error: %#x\n",
				stat
			);
			return stat;
		}

	}

	//--------------------------------------------------------------------

	if (NULL != pAuthIdentityEx2)
	{
		//--------------------------------------------------------------------
		CredHandle hCredential;
		TimeStamp tsExpiry;

		stat = AcquireCredentialsHandle(
			NULL,
			szPackage,
			SECPKG_CRED_OUTBOUND,
			NULL,
			pAuthIdentityEx2,
			NULL,
			NULL,
			&hCredential,
			&tsExpiry);

		if (stat != SEC_E_OK)
		{
			wprintf(
				L"Credentials function failed with error: %#x\n",
				stat
			);
			return stat;
		}

		//--------------------------------------------------------------------

		CtxtHandle		m_hCtxt;
		SecBufferDesc	outSecBufDesc;
		SecBuffer		outSecBuf;

		unsigned long	fContextAttr;

		BOOL			done = false;

		// prepare output buffer
		outSecBufDesc.ulVersion = 0;
		outSecBufDesc.cBuffers = 1;
		outSecBufDesc.pBuffers = &outSecBuf;
		outSecBuf.cbBuffer = m_cbMaxToken;
		outSecBuf.BufferType = SECBUFFER_TOKEN;
		outSecBuf.pvBuffer = m_pOutBuf;

		//char *negotiateToken;

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
			&tsExpiry
		);

		switch (stat)
		{
		case SEC_E_OK:
		case SEC_I_CONTINUE_NEEDED:
		case SEC_I_COMPLETE_AND_CONTINUE: break;
		default: return stat;
		}


		if (outSecBuf.cbBuffer)
		{
			base64_encode(reinterpret_cast < char* > (m_pOutBuf), outSecBuf.cbBuffer, negotiateToken);
		}
	}
	FreeContextBuffer(PackageInfo);
	return SEC_E_OK;
}

int setAuthBuf(char(&buff)[BUFF_SIZE], char *destHost, WCHAR *hostName)
{
	char *negotiateToken;
	if (getToken(&negotiateToken, hostName) == SEC_E_OK)
	{
		memset(buff, 0, sizeof(buff));

		_snprintf_s(
			buff,
			BUFF_SIZE,
			_TRUNCATE,
			"CONNECT %s HTTP/1.0\r\n%s\r\n%s %s\r\n\r\n",
			destHost,
			"Proxy-Connection: Keep-Alive",
			"Proxy-Authorization: Negotiate",
			negotiateToken
		);

		return strlen(buff);
	}
	else
		return -1;
}

int transmit(char(&buff)[BUFF_SIZE], SOCKET sockFrom, SOCKET sockTo, bool toProxy, bool *needAuth, WCHAR *hostName)
{
	memset(buff, 0, BUFF_SIZE);
	char destHost[255];
	memset(destHost, 0, 255);

	int len = recv(sockFrom, buff, BUFF_SIZE, 0);
	if (len < 0) return len;

	if (toProxy & *needAuth)
	{
		getSecondWord(buff, destHost, len);
		len = setAuthBuf(buff, destHost, hostName);
		if (len < 0) { return -1; }
		*needAuth = false;
	}

	len = send(sockTo, buff, len, 0);
	wprintf(L"%s %i bytes%s\n", toProxy ? L"--> Send to proxy" : L"<-- Send to client", len, len<0 ? L". Transmission failed" : L"");
	return len;
}

int _tmain(int argc, _TCHAR* argv[])
{
	// Get the command line options
	WCHAR *lPort, *rPort, *rAddr;
	int i = 1;
	argv[i] ? NULL : info(argv[0]);

	if (!wcscmp(argv[i], L"LP"))
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

	//SEC_CHAR *targetName = "HTTP/prox.apmes.ru@APMES.RU";

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
	iResult = listen(ListenSocket, MAXCONN);
	if (iResult != 0)
	{
		wprintf(L"listen failed with error: %#x\n", WSAGetLastError());
		return -1;
	}
	wprintf(L"MyProxy listen on port %s\n", lPort);

	//-------------------------
	// Add the socket and event to the arrays, increment number of events
	SOCKET SocketArray[WSA_MAXIMUM_WAIT_EVENTS];
	WSAEVENT EventArray[WSA_MAXIMUM_WAIT_EVENTS];

	DWORD EventTotal = 0;
	SocketArray[EventTotal] = ListenSocket;
	EventArray[EventTotal] = NewEvent;
	EventTotal++;

	bool needAuth[WSA_MAXIMUM_WAIT_EVENTS];
	memset(needAuth, true, sizeof(needAuth));
	bool toProxy;

	DWORD Index;
	SOCKET ClientSocket, ProxySocket;
	WSANETWORKEVENTS NetworkEvents;

	// The buffers for the reception - transmission
	char prBuff[BUFF_SIZE], clBuff[BUFF_SIZE];

	while (1)
	{

		// Wait for network events on all sockets
		Index = WSAWaitForMultipleEvents(EventTotal, EventArray, FALSE, WSA_INFINITE, FALSE);
		Index -= WSA_WAIT_EVENT_0;

		if ((Index != WSA_WAIT_FAILED) && (Index != WSA_WAIT_TIMEOUT))
		{
			WSAEnumNetworkEvents(SocketArray[Index], EventArray[Index], &NetworkEvents);
			WSAResetEvent(EventArray[Index]);

			toProxy = (Index % 2) ? true : false;

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
				iResult = transmit(toProxy ? prBuff : clBuff,
					SocketArray[Index],
					toProxy ? SocketArray[Index + 1] : SocketArray[Index - 1],
					toProxy,
					&needAuth[Index],
					host);
				if (iResult < 0) return -1;
			}

			if (NetworkEvents.lNetworkEvents & FD_CLOSE)
			{
				shutdown(SocketArray[toProxy ? Index + 1 : Index - 1], SD_BOTH);
				shutdown(SocketArray[Index], SD_BOTH);
				closesocket(SocketArray[toProxy ? Index + 1 : Index - 1]);
				closesocket(SocketArray[Index]);
				needAuth[toProxy ? Index : Index - 1] = true;
				wprintf(L"Connection closed\n");
			}
		}
	}
	return 0;
}