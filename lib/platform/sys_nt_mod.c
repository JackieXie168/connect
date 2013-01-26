
/*
Kernel space sniffer.
Guidelines used: Rootkits, subverting the windows kernel
 NT Rootkit
Made by: DiabloHorn
Purpose: Sniff data and filter it on text.
Thanks to: n0limit,BackBon3
*/

#pragma warning(disable:4700)

#define NDIS50 1

#include "ndis.h"
#include "ntddk.h"
#include "stdio.h"
#include "string.h"
#include "ndsniff.h"

NTSTATUS DriverEntry(IN PDRIVER_OBJECT theDriverObject, IN PUNICODE_STRING theRegistryPath)
{
	UINT aMediumIndex = 0;
	NDIS_STATUS aStatus, anErrorStatus;
	NDIS_MEDIUM aMediumArray = NdisMedium802_3; // specifies a ethernet network
	UNICODE_STRING anAdapterName;
	NDIS_PROTOCOL_CHARACTERISTICS aProtocolChar;
	
	//This string must match that specified in the registery (under Services) when the protocol was installed
	NDIS_STRING aProtoName = NDIS_STRING_CONST("ndsniff");
	
	DbgPrint("Loading NDIS Sniffer\n");
	
	RtlInitUnicodeString(&anAdapterName, L"\\Device\\{BDB421B0-4B37-4AA2-912B-3AA05F8A0829}");
	//RtlInitUnicodeString(&anAdapterName, L"\\Device\\{2D2E989B-6153-4787-913D-807779793B27}");
	//HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\services\Tcpip\Parameters\Interfaces\{2D2E989B-6153-4787-913D-807779793B27}
	//{449F621A-04BC-4896-BBCB-7A93708EA9B8}
	NdisInitializeEvent(&gCloseWaitEvent);
	theDriverObject->DriverUnload = OnUnload;
	
	RtlZeroMemory(&aProtocolChar, sizeof(NDIS_PROTOCOL_CHARACTERISTICS));
	
	aProtocolChar.MajorNdisVersion = 5;
	aProtocolChar.MinorNdisVersion = 0;
	aProtocolChar.Reserved = 0;
	aProtocolChar.OpenAdapterCompleteHandler = OnOpenAdapterDone;
	aProtocolChar.CloseAdapterCompleteHandler = OnCloseAdapterDone;
	aProtocolChar.SendCompleteHandler = OnSendDone;
	aProtocolChar.TransferDataCompleteHandler = OnTransferDataDone;
	aProtocolChar.ResetCompleteHandler = OnResetDone;
	aProtocolChar.RequestCompleteHandler = OnRequestDone;
	aProtocolChar.ReceiveHandler = OnReceiveStub;
	aProtocolChar.ReceiveCompleteHandler = OnReceiveDoneStub;
	aProtocolChar.StatusHandler = OnStatus;
	aProtocolChar.StatusCompleteHandler = OnStatusDone;
	aProtocolChar.Name = aProtoName;
	aProtocolChar.BindAdapterHandler = OnBindAdapter;
	aProtocolChar.UnbindAdapterHandler = OnUnBindAdapter;
	aProtocolChar.UnloadHandler = OnProtocolUnload;
	aProtocolChar.ReceivePacketHandler = OnReceivePacket;
	aProtocolChar.PnPEventHandler = OnPNPEvent;
	
	DbgPrint("Going to register Protocol");
	
	NdisRegisterProtocol(&aStatus, &gNdisProtocolHandle, &aProtocolChar, sizeof(NDIS_PROTOCOL_CHARACTERISTICS));
	
	DbgPrint("ndSniff: after NdisRegisterProtocol");
	
	if (aStatus != NDIS_STATUS_SUCCESS) {
		char _t[255];
		_snprintf(_t, 253, "DriverEntry: ERROR NdisRegisterProtocol failed with error 0x%08X", aStatus);
		DbgPrint("%s\n",_t);
		return aStatus;
	}
	
	DbgPrint("ndSniff: NdisOpenAdapter ->");
	
	NdisOpenAdapter(&aStatus, &anErrorStatus, &gAdapterHandle, &aMediumIndex, &aMediumArray, 1, gNdisProtocolHandle, &gUserStruct, &anAdapterName, 0, NULL);
	
	DbgPrint("ndSniff: NdisOpenAdapter <-");
	
	if (aStatus != NDIS_STATUS_SUCCESS) {
		if (FALSE == NT_SUCCESS(aStatus)) {
			char _t[255];
			_snprintf(_t, 253, "ndSniff: NdisOpenAdapter returned an error 0x%08X", aStatus);
			DbgPrint("%s\n", _t);
			
			if(NDIS_STATUS_ADAPTER_NOT_FOUND == aStatus) {
				DbgPrint("Adapter Not Found\n");
			}
			
			NdisDeregisterProtocol(&aStatus, gNdisProtocolHandle);
			
			if(FALSE == NT_SUCCESS(aStatus)) {
				DbgPrint("Deregister Protocol Failed\n");
			}
			
			//use for win ce according to rootkit book
			
			//NdisFreeEvent(gCloseWaitEvent);
			
			return STATUS_UNSUCCESSFUL;
		}
	} else {
		DbgPrint("ndSniff: OnOpenAdapterDone ->");
		OnOpenAdapterDone(&gUserStruct, aStatus, NDIS_STATUS_SUCCESS);
		DbgPrint("ndSniff: OnOpenAdapterDone <-");
	}

	DbgPrint("ndSniff: driver entry return");
	return STATUS_SUCCESS;
}

VOID OnOpenAdapterDone(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status, IN NDIS_STATUS OpenErrorStatus)
{
	NDIS_STATUS aStatus;
	NDIS_REQUEST anNdisRequest;
	NDIS_STATUS anotherStatus;
	ULONG aMode = NDIS_PACKET_TYPE_ALL_LOCAL; //NDIS_PACKET_TYPE_PROMISCUOUS;
	
	DbgPrint("ndSniff: OnOpenAdapterDone called\n");
	
	if (NT_SUCCESS(OpenErrorStatus)) {
		//k card goes into aMode
		anNdisRequest.RequestType = NdisRequestSetInformation;
		anNdisRequest.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
		anNdisRequest.DATA.SET_INFORMATION.InformationBuffer = &aMode;
		anNdisRequest.DATA.SET_INFORMATION.InformationBufferLength = sizeof(ULONG);
		
		NdisRequest(&anotherStatus, gAdapterHandle, &anNdisRequest);
		
		NdisAllocatePacketPool(&aStatus, &gPacketPoolH, TRANSMIT_PACKETS, sizeof(PACKET_RESERVED));
		
		if (aStatus != NDIS_STATUS_SUCCESS) {
			return;
		}
		
		NdisAllocateBufferPool(&aStatus, &gBufferPoolH, TRANSMIT_PACKETS);
		
		if (aStatus != NDIS_STATUS_SUCCESS) {
			return;
		}
	} else {
		char _t[255];
		_snprintf(_t,253, "ndSniff: OpenAdapterDone called with error 0x%08X", OpenErrorStatus);
		DbgPrint("%s\n", _t);
	}
}

VOID OnCloseAdapterDone(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status)
{
	DbgPrint("ndSniff: OnClosAdapterDone called\n");
	NdisSetEvent(&gCloseWaitEvent);
}

VOID OnSendDone(IN NDIS_HANDLE ProtocolBindingContext, IN PNDIS_PACKET pPacket, IN NDIS_STATUS Status)
{
	DbgPrint("ndSniff OnSendDone called\n");
}

VOID OnTransferDataDone(IN NDIS_HANDLE thePBindingContext, IN PNDIS_PACKET thePacketp, IN NDIS_STATUS theStatus, IN UINT theBytesTransfered)
{
	PNDIS_BUFFER aNdisBufP;
	PVOID aBufferP;
	ULONG aBufferLen;
	PVOID aHeaderBufferP;
	ULONG aHeaderBufferLen;
	
	DbgPrint("ndSniff OnTransferDataDone called\n");
	
	aBufferP = RESERVED(thePacketp)->pBuffer;
	aBufferLen = theBytesTransfered;
	aHeaderBufferP = RESERVED(thePacketp)->pHeaderBufferP;
	aHeaderBufferLen = RESERVED(thePacketp)->pHeaderBufferLen;
	
	/*
	aHeaderBufferP = Ethernet Header
	aBufferP tcp/ip
	*/
	
	if (aBufferP && aHeaderBufferP) {
		ULONG aPos = 0;
		unsigned char *aPtr = NULL;
		
		aPtr = ExAllocatePool(NonPagedPool, (aHeaderBufferLen + aBufferLen));
		
		if (aPtr) {
			memcpy(aPtr, aHeaderBufferP, aHeaderBufferLen);
			memcpy(aPtr + aHeaderBufferLen, aBufferP, aBufferLen);
			
			/* woei complete packet, parse it*/
			OnSniffedPacket(aPtr, (aHeaderBufferLen + aBufferLen));
			ExFreePool(aPtr);
		}
		
		ExFreePool(aBufferP);
		ExFreePool(aHeaderBufferP);
	}
	
	NdisUnchainBufferAtFront(thePacketp, &aNdisBufP);
	
	if (aNdisBufP) {
		NdisFreeBuffer(aNdisBufP);
	}
	
	NdisReinitializePacket(thePacketp);
	NdisFreePacket(thePacketp);
	
	return;
}

NDIS_STATUS OnReceiveStub(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_HANDLE MacReceiveContext, IN PVOID HeaderBuffer, IN UINT HeaderBufferSize, IN PVOID LookAheadBuffer, IN UINT LookaheadBufferSize, UINT PacketSize)
{
	PNDIS_PACKET pPacket;
	PNDIS_BUFFER pBuffer;
	ULONG SizeToTransfer = 0;
	NDIS_STATUS Status;
	UINT BytesTransfered;
	ULONG BufferLength;
	PPACKET_RESERVED Reserved;
	NDIS_HANDLE BufferPool;
	PVOID aTemp;
	UINT Frame_Type = 0;
	
	DbgPrint("ndSniff OnReceiveStub called\n");
	
	SizeToTransfer = PacketSize;
	
	if (HeaderBufferSize > ETHERNET_HEADER_LENGTH || (SizeToTransfer > (1514 - ETHERNET_HEADER_LENGTH))) {
		DbgPrint("OnReceiveStub: packet not accepted\n");
		return NDIS_STATUS_NOT_ACCEPTED;
	}
	
	memcpy(&Frame_Type, (((char *)HeaderBuffer) + 12), 2);
	
	if (Frame_Type != 0x0008) {
		DbgPrint("NON EthernetHeader\n");
		return NDIS_STATUS_NOT_ACCEPTED;
	}
	
	aTemp = ExAllocatePool(NonPagedPool, (1514 - ETHERNET_HEADER_LENGTH));
	
	if (aTemp) {
		RtlZeroMemory(aTemp, (1514 - ETHERNET_HEADER_LENGTH));
		NdisAllocatePacket(&Status, &pPacket, gPacketPoolH);
		
		if(NDIS_STATUS_SUCCESS == Status) {
			RESERVED(pPacket)->pHeaderBufferP = ExAllocatePool(NonPagedPool, ETHERNET_HEADER_LENGTH);
			
			if (RESERVED(pPacket)->pHeaderBufferP) {
				RtlZeroMemory(RESERVED(pPacket)->pHeaderBufferP, ETHERNET_HEADER_LENGTH);
				memcpy(RESERVED(pPacket)->pHeaderBufferP, (char *)HeaderBuffer, ETHERNET_HEADER_LENGTH);
				RESERVED(pPacket)->pHeaderBufferLen = ETHERNET_HEADER_LENGTH;
				NdisAllocateBuffer(&Status, &pBuffer, gBufferPoolH, aTemp, (1514 - ETHERNET_HEADER_LENGTH));
				
				if(NDIS_STATUS_SUCCESS == Status) {
					RESERVED(pPacket)->pBuffer = aTemp;
					
					/*this is important here we attach the buffer to the packet*/
					
					NdisChainBufferAtFront(pPacket, pBuffer);
					NdisTransferData(&(gUserStruct.mStatus), gAdapterHandle, MacReceiveContext, 0, SizeToTransfer, pPacket, &BytesTransfered);
					
					if (Status != NDIS_STATUS_PENDING) {
						/*important to call the complete routine since it's not pending*/
						OnTransferDataDone(&gUserStruct, pPacket, Status, BytesTransfered);
					}
					
					return NDIS_STATUS_SUCCESS;
				}
				
				ExFreePool(RESERVED(pPacket)->pHeaderBufferP);
				
			} else {
				DbgPrint("pHeaderBufferP allocate failed\n");
			}
			
			NdisFreePacket(pPacket);
			
		}
		
		ExFreePool(aTemp);
	}
	
	return NDIS_STATUS_SUCCESS;
}

VOID OnReceiveDoneStub(IN NDIS_HANDLE ProtocolBindingContext)
{
	DbgPrint("ndSniff OnReceiveStubDone called\n");
	return;
}

VOID OnStatus(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status, IN PVOID StatusBuffer, IN UINT StatusBufferSize)
{
	DbgPrint("ndSniff OnStatus called\n");
	return;
}

VOID OnStatusDone(IN NDIS_HANDLE ProtocolBindingContext)
{
	DbgPrint("ndSniff OnStatusDone called\n");
	return;
}

VOID OnResetDone(IN NDIS_HANDLE ProtocolBindingContext, IN NDIS_STATUS Status)
{
	DbgPrint("ndSniff OnResetDone called\n");
	return;
}

VOID OnRequestDone(IN NDIS_HANDLE ProtocolBindingContext, IN PNDIS_REQUEST NdisRequest, IN NDIS_STATUS Status)
{
	DbgPrint("ndSniff OnRequestDone called\n");
	return;
}

VOID OnBindAdapter(OUT PNDIS_STATUS theStatus, IN NDIS_HANDLE theBindContext, IN PNDIS_STRING theDeviceNameP, IN PVOID theSS1, IN PVOID theSS2)
{
	DbgPrint("ndSniff OnBindAdapter called\n");
	return;
}

VOID OnUnBindAdapter(OUT PNDIS_STATUS theStatus, IN NDIS_HANDLE theBindContext, IN PNDIS_HANDLE theUnbindContext)
{
	DbgPrint("ndSniff OnUnBindAdapter called\n");
	return;
}

NDIS_STATUS OnPNPEvent(IN NDIS_HANDLE ProtocolBindingContext, IN PNET_PNP_EVENT pNetPnPEvent)
{
	DbgPrint("ndSniff OnPNPEvent called\n");
	return NDIS_STATUS_SUCCESS;
}

VOID OnProtocolUnload(VOID)
{
	DbgPrint("ndSniff OnProtocolUnload called\n");
	return;
}

INT OnReceivePacket(IN NDIS_HANDLE ProtocolBindingContext, IN PNDIS_PACKET Packet)
{
	DbgPrint("ndSniff OnReceivePacket called\n");
	return 0;
}

VOID OnUnload(IN PDRIVER_OBJECT DriverObject)
{
	NDIS_STATUS Status;
	DbgPrint("ndSniff: OnUnload called\n");
	NdisResetEvent(&gCloseWaitEvent);
	
	NdisCloseAdapter(&Status, gAdapterHandle);
	
	if (Status == NDIS_STATUS_PENDING) {
		DbgPrint("NDIS BUSY\n");
		NdisWaitEvent(&gCloseWaitEvent, 0);
	}
	
	NdisDeregisterProtocol(&Status, gNdisProtocolHandle);
	
	if (FALSE == NT_SUCCESS(Status)) {
		DbgPrint("Deregister failed\n");
	}
	
	DbgPrint("NdisDeregisterProtocol done\n");
}

/*
From here on we analyze the packets
*/
VOID OnSniffedPacket(const unsigned char* theData, int theLen)
{
	int i;
	int l;
	PACKET_TCP rPacketT;
	PACKET_UDP rPacketU;
	PACKET_ICMP rPacketI;
	
	IP_HDR *ip = (IP_HDR *) ((unsigned char *) theData + sizeof(ETH_HDR));
	DbgPrint("PROTOBEFORE:: %i %d 0x%x\n", ip->proto, ip->proto, ip->proto);
	
	DbgPrint("PACKDUMP --->:\n");
	l = 0;
	for (l = 0; l < theLen; l++){
		DbgPrint(" %0.2X ", (unsigned char *) theData[l]);
	}
	DbgPrint("\nPACKDUMP <---:\n");
	#if 0
	if (IPPROTO_TCP == ip->proto) {
		int i;
		rPacketT.ethHdr = (ETH_HDR *) theData;
		rPacketT.ipHdr = (IP_HDR *) (theData + sizeof(ETH_HDR));
		rPacketT.tcpHdr = (TCP_HDR *) (theData + (sizeof(ETH_HDR) + sizeof(IP_HDR)));
		rPacketT.data = (unsigned char *) (theData + sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(TCP_HDR));
		rPacketT.dataLen = (theLen - (sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(TCP_HDR)));
		
		DbgPrint("TCP\n");
		
		if (findStr(rPacketT.data, "hacker")) {
			DbgPrint("TRUE\n");
		} else {
			DbgPrint("FALSE\n");
		}
		
		return;
		
	} else if (IPPROTO_UDP == ip->proto) {
		int i;
		rPacketU.ethHdr = (ETH_HDR *) theData;
		rPacketU.ipHdr = (IP_HDR *) (theData + sizeof(ETH_HDR));
		rPacketU.udpHdr = (UDP_HDR *) (theData + (sizeof(ETH_HDR) + sizeof(IP_HDR)));
		rPacketU.data = (unsigned char *) (theData + sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(UDP_HDR));
		rPacketU.dataLen = (theLen - (sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(UDP_HDR)));
		
		DbgPrint("UDP\n");
		if (findStr(rPacketT.data, "hacker")) {
			DbgPrint("TRUE\n");
		} else {
			DbgPrint("FALSE\n");
		}
		
		return;
		
	} else if (IPPROTO_ICMP == ip->proto) {
		int i;
		rPacketI.ethHdr = (ETH_HDR *) theData;
		rPacketI.ipHdr = (IP_HDR *) (theData + sizeof(ETH_HDR));
		rPacketI.icmpHdr = (ICMP_HDR *) (theData + (sizeof(ETH_HDR) + sizeof(IP_HDR)));
		rPacketI.data = (unsigned char *) (theData + sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(ICMP_HDR));
		rPacketI.dataLen = (theLen - (sizeof(ETH_HDR) + sizeof(IP_HDR) + sizeof(ICMP_HDR)));
		
		DbgPrint("ICMP\n");
		
		if (findStr(rPacketI.data, "ZXX")) {
			DbgPrint("TRUE\n");
		} else {
			DbgPrint("FALSE\n");
		}
		
		return;
		
	} else {
		/*
		you can implement the rest of the protocols yourself.
		*/
		DbgPrint("UNDEFINED\n");
	}
	#endif
	return;
}

/*thx to BackBon3 spared me the fiddling around*/
BOOLEAN findStr(const char *psz,const char *tofind)
{
	const char *ptr = psz;
	const char *ptr2;
	
	while (1) {
		ptr = strchr(psz, toupper(*tofind));
		ptr2 = strchr(psz, tolower(*tofind));
		
		if (!ptr) {
			ptr = ptr2; /* was ptr2 = ptr.  Big bug fixed 10/22/99 */
		}
		if (!ptr) {
			break;
		}
		
		if (ptr2 && (ptr2 < ptr)) {
			ptr = ptr2;
		}
		
		if (!_strnicmp(ptr, tofind, strlen(tofind))) {
			return TRUE;
		}
		
		psz = ptr + 1;
	}
	
	return FALSE;
} /* stristr */

/* TODO: AUTOGET ADAPTERNAME
HKLM\SYSTEM\CurrentControlSet\Service\Tcpip\Parame  ters\Interfaces\{number}

PWCHAR GetAdapterName()
{
PWCHAR AdapterName;
NTSTATUS ntStatus;
PHANDLE oHandle;

ntStatus = ZwOpenKey(&oHandle,FILE_READ_DATA,

}*/
