#include "network.h"
#include "serverinstance.h"
#include "serverconfig.h"

CNetwork::CNetwork(void)
{
	m_TCPSocket = INVALID_SOCKET;
	m_UDPSocket = INVALID_SOCKET;

	FD_ZERO(&m_Read_fds);
	FD_ZERO(&m_Write_fds);
	FD_ZERO(&m_Master_u);
	FD_ZERO(&m_Read_fds_u);

	m_nFDmax = 0;
	m_nFDmax_u = 0;
	m_iResult = 0;
}

bool CNetwork::ServerInit(void)
{
#ifdef WIN32
	// create WSADATA object
	WSADATA wsaData;

	// Initialize Winsock
	m_iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (m_iResult != 0)
	{
		g_pConsole->FatalError("WSAStartup() failed with error: %d\n%s\n", m_iResult, WSAGetLastErrorString());
		return false;
	}
#endif

	// address info for the server to listen to
	struct addrinfo* result = NULL;
	struct addrinfo hints;

	// set address information
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;    // TCP connection!!!
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	m_iResult = getaddrinfo(NULL, g_pServerConfig->tcpPort.c_str(), &hints, &result);
	if (m_iResult != 0)
	{
		g_pConsole->FatalError("getaddrinfo() failed with error: %d\n%s\n", m_iResult, WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	// Create a SOCKET for connecting to server
	m_TCPSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (m_TCPSocket == INVALID_SOCKET)
	{
		g_pConsole->FatalError("socket() failed with error: %ld\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		freeaddrinfo(result);
		Cleanup();
		return false;
	}

	// Set the mode of the socket to be nonblocking
	u_long iMode = 1;
	m_iResult = ioctlsocket(m_TCPSocket, FIONBIO, &iMode);
	if (m_iResult == SOCKET_ERROR)
	{
		g_pConsole->FatalError("ioctlsocket() failed with error: %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	// Setup the TCP listening socket
	m_iResult = ::bind(m_TCPSocket, result->ai_addr, (int)result->ai_addrlen);
	if (m_iResult == SOCKET_ERROR)
	{
		g_pConsole->FatalError("bind failed with error: %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		freeaddrinfo(result);
		Cleanup();
		return false;
	}

	// no longer need address information
	freeaddrinfo(result);

	// start listening for new clients attempting to connect
	m_iResult = listen(m_TCPSocket, SOMAXCONN);
	if (m_iResult == SOCKET_ERROR)
	{
		g_pConsole->FatalError("listen() failed with error: %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	int sendBuffer = g_pServerConfig->tcpSendBufferSize;
	m_iResult = setsockopt(m_TCPSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sendBuffer, sizeof(int));
	if (m_iResult == SOCKET_ERROR)
	{
		g_pConsole->FatalError("setsockopt failed with error %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	FD_ZERO(&m_Read_fds);
	FD_ZERO(&m_Write_fds);

	m_nFDmax = m_TCPSocket;

	return true;
}

bool CNetwork::UDPInit(void)
{
	int fromlen = sizeof(sockaddr_in);
	struct addrinfo* result = NULL;
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	int iResult = getaddrinfo(NULL, g_pServerConfig->udpPort.c_str(), &hints, &result);
	if (iResult != 0)
	{
		g_pConsole->FatalError("getaddrinfo() failed with error: %d\n%s\n", iResult, WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	m_UDPSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (m_UDPSocket == INVALID_SOCKET)
	{
		g_pConsole->FatalError("socket() failed with error: %ld\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		Cleanup();
		return false;
	}

	u_long iMode = 1;
	iResult = ioctlsocket(m_UDPSocket, FIONBIO, &iMode);

	iResult = ::bind(m_UDPSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		g_pConsole->FatalError("bind failed with error: %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
		freeaddrinfo(result);
		Cleanup();
		return false;
	}

	FD_ZERO(&m_Master_u);
	FD_ZERO(&m_Read_fds_u);

	FD_SET(m_UDPSocket, &m_Master_u);

	m_nFDmax_u = m_UDPSocket;

	return true;
}

CNetwork::~CNetwork(void)
{
	for (auto socket : m_Sessions)
	{
		delete socket;
	}

	Cleanup();
}

void CNetwork::Cleanup()
{
	if (m_TCPSocket)
		closesocket(m_TCPSocket);
	
	if (m_UDPSocket)
		closesocket(m_UDPSocket);
	
#ifdef WIN32
	WSACleanup();
#endif
}

int CNetwork::SendMessage(SOCKET curSocket, const char* message, int messageSize)
{
	return send(curSocket, message, messageSize, 0);
}

int CNetwork::ReceiveMessage(SOCKET curSocket, char* buffer, int bufSize)
{
	return recv(curSocket, buffer, bufSize, 0);
}

IExtendedSocket* CNetwork::AcceptNewClient(unsigned int& id)
{
	sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);

	SOCKET clientSocket = accept(m_TCPSocket, (sockaddr*)&addr, &addrlen);
	if (clientSocket != INVALID_SOCKET)
	{
		char value = 1;
		//setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value));
		setsockopt(clientSocket, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value));

		//char* ip = inet_ntoa(addr.sin_addr);
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &(addr.sin_addr), ip, INET_ADDRSTRLEN);

		CExtendedSocket* newSocket = new CExtendedSocket(id);
		newSocket->ResetSeq();
		newSocket->SetIP(ip);
		newSocket->SetSocket(clientSocket);

		m_Sessions.push_back(newSocket);

		SendMessage(clientSocket, "~SERVERCONNECTED\n\0", sizeof("~SERVERCONNECTED\n\0"));

		return newSocket;
	}
	else
	{
		g_pConsole->Error("accept() failed with error: %d\n%s\n", GetNetworkError(), WSAGetLastErrorString());
	}

	return NULL;
}

IExtendedSocket* CNetwork::GetExSocketBySocket(SOCKET socket)
{
	for (auto s : m_Sessions)
	{
		if (s->GetSocket() == socket)
			return s;
	}

	return NULL;
}

void CNetwork::RemoveSocket(IExtendedSocket* socket)
{
	m_Sessions.erase(remove(begin(m_Sessions), end(m_Sessions), socket), end(m_Sessions));

	closesocket(socket->GetSocket());

	delete socket;
}