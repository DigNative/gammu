
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <conio.h>
#include <process.h>
#include <ctype.h>
#include <locale.h>
#include <signal.h>

#include "../../common/gsmstate.h"
#include "../../common/gsmcomon.h"
#include "../../common/misc/coding/coding.h"

typedef struct {
	GSM_StateMachine 	s;
	void 			(**PhoneCallBack) 	(int x, int s, boolean connected);
	void 			(**SecurityCallBack) 	(int x, int s, GSM_SecurityCodeType State);
	void 			(**SMSCallBack) 	(int x, int s);
	HANDLE       		hCommWatchThread;
	DWORD         		dwThreadID;
    	HANDLE 		 	Mutex;
	bool			ThreadTerminate;
	bool			Used;
	int 		 	number;
	int			errors;
	GSM_SecurityCodeType 	SecurityStatus;
	GSM_Error		SendSMSStatus;
} GSM_StateMachineExt;

GSM_StateMachineExt s[10];

bool SetErrorCounter(int i, GSM_Error error)
{
	switch (error) {
	case ERR_TIMEOUT:
		s[i].errors++;
		break;
	case ERR_NOTSUPPORTED:
	case ERR_NOTIMPLEMENTED:
		break;
	default:
		if (s[i].errors > 0) s[i].errors--;
	}
	if (s[i].errors > 1) return true;
	return false;
}

BOOL LoopProc(int *i)
{
	GSM_Error		error;
	GSM_SecurityCodeType 	SecurityStatus;
	GSM_SMSMemoryStatus	SMSStatus;
	void 			(*PhoneCall) 	(int x, int s, boolean connected);
	void 			(*SecurityCall) (int x, int s, GSM_SecurityCodeType State);
	void 			(*SMSCall) 	(int x, int s);

	while(1) {
		if (s[*i].errors > 1) {
			if (s[*i].ThreadTerminate) break;
			if (*s[*i].PhoneCallBack != NULL && s[*i].errors < 250) {
				PhoneCall = *s[*i].PhoneCallBack;
				PhoneCall(1,*i,false);
			}
			if (s[*i].errors != 250) {
			        WaitForSingleObject(s[*i].Mutex, INFINITE );
				GSM_TerminateConnection(&s[*i].s);
			        ReleaseMutex(s[*i].Mutex);
			}
			s[*i].errors = 251;
			if (s[*i].ThreadTerminate) break;
		        WaitForSingleObject(s[*i].Mutex, INFINITE );
			error=GSM_InitConnection(&s[*i].s,2);
		        ReleaseMutex(s[*i].Mutex);
			if (s[*i].ThreadTerminate) break;
//			if (error == ERR_DEVICEOPENERROR) break;
			if (error == ERR_NONE) {
				s[*i].errors = 0;
				if (*s[*i].PhoneCallBack != NULL) {
					PhoneCall = *s[*i].PhoneCallBack;
					PhoneCall(1,*i,true);
				}
				if (s[*i].ThreadTerminate) break;
			        WaitForSingleObject(s[*i].Mutex, INFINITE );
				error=s[*i].s.Phone.Functions->SetAutoNetworkLogin(&s[*i].s);
			        ReleaseMutex(s[*i].Mutex);
				SetErrorCounter(*i, error);
			}
		} else {
			if (s[*i].ThreadTerminate) break;
			if (*s[*i].SecurityCallBack != NULL) {
			        WaitForSingleObject(s[*i].Mutex, INFINITE );
				error=s[*i].s.Phone.Functions->GetSecurityStatus(&s[*i].s,&SecurityStatus);
			        ReleaseMutex(s[*i].Mutex);
				if (SetErrorCounter(*i, error)) continue;
				switch (error) {
				case ERR_NONE:
					if (s[*i].ThreadTerminate) break;
					if (SecurityStatus != s[*i].SecurityStatus) {
						if (*s[*i].SecurityCallBack != NULL) {
							s[*i].SecurityStatus = SecurityStatus;
							SecurityCall = *s[*i].SecurityCallBack;
							SecurityCall(1,*i,SecurityStatus);
						}
					}
					break;
				case ERR_NOTSUPPORTED:
					if (s[*i].ThreadTerminate) break;
					if (*s[*i].SecurityCallBack != NULL) {
						SecurityStatus		= 0;
						s[*i].SecurityStatus 	= 0;
						SecurityCall = *s[*i].SecurityCallBack;
						SecurityCall(1,*i,SecurityStatus);
					}
				default:
					break;
				}
			}
			if (s[*i].ThreadTerminate) break;
			if (*s[*i].SMSCallBack!=NULL) {
				WaitForSingleObject(s[*i].Mutex, INFINITE );
				error=s[*i].s.Phone.Functions->GetSMSStatus(&s[*i].s,&SMSStatus);
				ReleaseMutex(s[*i].Mutex);
				if (SetErrorCounter(*i, error)) continue;
				if (s[*i].ThreadTerminate) break;
				if (error == ERR_NONE) {
					if (SMSStatus.SIMUsed+SMSStatus.PhoneUsed != 0) {
						SMSCall = *s[*i].SMSCallBack;
						SMSCall(1,*i);
					}
				}
			}
			if (s[*i].ThreadTerminate) break;
			my_sleep(200);
			if (s[*i].ThreadTerminate) break;
		}
	}

	s[*i].Used		= false;
	s[*i].hCommWatchThread 	= NULL;
	s[*i].dwThreadID 	= 0;

	return(TRUE);
}

static void CreatePhoneThread(int *phone,
			      void (**PhoneCallBack)    (int x, int s, boolean connected),
			      void (**SecurityCallBack) (int x, int s, GSM_SecurityCodeType State),
			      void (**SMSCallBack)      (int x, int s))
{
	s[*phone].Mutex 		= CreateMutex( NULL, FALSE, NULL );
	s[*phone].SecurityStatus	= 0;
	s[*phone].number 		= *phone;
	
	s[*phone].PhoneCallBack 	= PhoneCallBack;
	s[*phone].SecurityCallBack 	= SecurityCallBack;
	s[*phone].SMSCallBack 		= SMSCallBack;
	s[*phone].ThreadTerminate 	= false;
	s[*phone].hCommWatchThread 	= CreateThread((LPSECURITY_ATTRIBUTES) NULL,
				  0,
				  (LPTHREAD_START_ROUTINE) LoopProc,
				  (LPVOID) &s[*phone].number,
				  0, &s[*phone].dwThreadID);
}

static void CheckConnectionType(int  *phone,
				char *connection,
				char *connection_to_check,
				GSM_Error *error,
				GSM_Error *error2,
			        void (**PhoneCallBack)    (int x, int s, boolean connected),
			        void (**SecurityCallBack) (int x, int s, GSM_SecurityCodeType State),
			        void (**SMSCallBack)      (int x, int s))
{
	void (*PhoneCall) (int x, int s, boolean connected);

	s[*phone].s.CurrentConfig->Connection = connection_to_check;
	*error=GSM_InitConnection(&s[*phone].s,2);
	switch (*error) {
	case ERR_NONE:
		strcpy(connection,connection_to_check);
		s[*phone].errors = 0;
		CreatePhoneThread(phone,PhoneCallBack,SecurityCallBack,SMSCallBack);
		if (*s[*phone].PhoneCallBack != NULL) {
			PhoneCall = *s[*phone].PhoneCallBack;
			PhoneCall(1,*phone,true);
		}
	case ERR_DEVICEOPENERROR:
		break;
	default:
		*error2 = GSM_TerminateConnection(&s[*phone].s);
	}
}

GSM_Error WINAPI mystartconnection(int *phone,
				   char *device,
				   char *connection,
				   char *model,
				   char *logfile,
				   char *logfiletype,
				   void (**PhoneCallBack)    (int x, int s, boolean connected),
				   void (**SecurityCallBack) (int x, int s, GSM_SecurityCodeType State),
				   void (**SMSCallBack)	     (int x, int s))
{
	int 		i;
	GSM_Error 	error,error2;

#ifndef WIN32
	setlocale(LC_ALL, "");
#else
	setlocale(LC_ALL, ".OCP");
#endif

	*phone = 0;
	for (i=1;i<10;i++) {
		if (!s[i].Used) {
			s[i].Used 	= true;
			*phone		= i;
			break;
		}
	}
	if (*phone == 0) return ERR_MOREMEMORY;

	s[*phone].s.ConfigNum			= 1;
	s[*phone].s.msg	 			= NULL;
	s[*phone].s.CurrentConfig		= &s[*phone].s.Config[0];
    	s[*phone].s.CurrentConfig->Localize 	= "";
	s[*phone].s.CurrentConfig->SyncTime	= "";
	s[*phone].s.CurrentConfig->DebugFile 	= malloc( strlen(logfile)+1 );
	if (s[*phone].s.CurrentConfig->DebugFile == NULL) return ERR_MOREMEMORY;
	strcpy(s[*phone].s.CurrentConfig->DebugFile,logfile);
	strcpy(s[*phone].s.CurrentConfig->DebugLevel,logfiletype);
	s[*phone].s.CurrentConfig->LockDevice	= "";
	s[*phone].s.CurrentConfig->StartInfo	= "yes";
	s[*phone].s.CurrentConfig->Device 	= malloc( strlen(device)+1 );
	if (s[*phone].s.CurrentConfig->Device == NULL) return ERR_MOREMEMORY;
	strcpy(s[*phone].s.CurrentConfig->Device,device);
	strcpy(s[*phone].s.CurrentConfig->Model,model);

	if (connection[0] == 0) {
		CheckConnectionType(phone,connection,"at115200",&error,&error2,PhoneCallBack,SecurityCallBack,SMSCallBack);
		switch (error) {
			case ERR_NONE:
			case ERR_DEVICEOPENERROR:
				return error;
			default:
				if (error2 != ERR_NONE) return error2;
		}
		CheckConnectionType(phone,connection,"at19200",&error,&error2,PhoneCallBack,SecurityCallBack,SMSCallBack);
		switch (error) {
			case ERR_NONE:
			case ERR_DEVICEOPENERROR:
				return error;
			default:
				if (error2 != ERR_NONE) return error2;
		}
		CheckConnectionType(phone,connection,"fbus",&error,&error2,PhoneCallBack,SecurityCallBack,SMSCallBack);
		switch (error) {
			case ERR_NONE:
			case ERR_DEVICEOPENERROR:
				return error;
			default:
				if (error2 != ERR_NONE) return error2;
		}
		strcpy(connection,"");
		return ERR_NOTCONNECTED;
	} else {
		s[*phone].s.CurrentConfig->Connection = malloc( strlen(connection)+1 );
		if (s[*phone].s.CurrentConfig->Connection == NULL) return ERR_MOREMEMORY;
		strcpy(s[*phone].s.CurrentConfig->Connection,connection);
		s[*phone].errors = 250;
		CreatePhoneThread(phone,PhoneCallBack,SecurityCallBack,SMSCallBack);
		return ERR_NONE;
	}
}

GSM_Error WINAPI myendconnection(int phone)
{
	GSM_Error error=ERR_NONE;

	if (s[phone].Used) {
		s[phone].ThreadTerminate = true;
		if (s[phone].s.opened) {
		        WaitForSingleObject(s[phone].Mutex, INFINITE );
			error = GSM_TerminateConnection(&s[phone].s);
		        ReleaseMutex(s[phone].Mutex);
		}
	}
	return error;
}

GSM_Error WINAPI mygetnetworkinfo (int phone, GSM_NetworkInfo *NetworkInfo)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->GetNetworkInfo(&s[phone].s,NetworkInfo);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

GSM_Error WINAPI mygetnextsmsmessage (int phone, GSM_MultiSMSMessage *sms, bool start)
{
	GSM_Error 	error;
	int		i;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->GetNextSMS(&s[phone].s,sms,start);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	if (error == ERR_NONE) {
		for (i=0;i<sms->Number;i++) {
			if (sms->SMS[i].PDU == SMS_Deliver || sms->SMS[i].PDU == SMS_Status_Report) {
				ReverseUnicodeString(sms->SMS[i].SMSC.Number);
			}
			ReverseUnicodeString(sms->SMS[i].Number);
			if (sms->SMS[i].PDU != SMS_Status_Report) {
				ReverseUnicodeString(sms->SMS[i].Name);
			}
			if (sms->SMS[i].Coding == SMS_Coding_Unicode || sms->SMS[i].Coding == SMS_Coding_Default) {
				ReverseUnicodeString(sms->SMS[i].Text);
			}
		}
	}
	return error;
}

GSM_Error WINAPI myentersecuritycode(int phone, GSM_SecurityCode *Code)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->EnterSecurityCode(&s[phone].s,*Code);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

GSM_Error WINAPI mydeletesmsmessage (int phone, GSM_SMSMessage *sms)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->DeleteSMS(&s[phone].s,sms);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

void SendSMSStatus (char *Device, int status)
{
	int i;

	for (i=0;i<10;i++) {
		if (s[i].s.opened && s[i].Used) {
			if (strcmp(s[i].s.CurrentConfig->Device,Device)==0) {
				if (status == 0) {
					s[i].SendSMSStatus = ERR_NONE;
				} else {
					s[i].SendSMSStatus = ERR_UNKNOWN;
				}
			}
		}
	}
}

GSM_Error WINAPI mysendsmsmessage (int phone, GSM_SMSMessage *sms, unsigned int timeout)
{
	GSM_Error 	error;
	GSM_DateTime	Date;
	unsigned int	i,j;
	GSM_SMSMessage	sms2;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

	memcpy(&sms2,sms,sizeof(GSM_SMSMessage));

	if (sms2.SMSC.Location == 0) ReverseUnicodeString(sms2.SMSC.Number);
	ReverseUnicodeString(sms2.Number);
//	ReverseUnicodeString(sms2.Name);
	if (sms2.Coding == SMS_Coding_Unicode || sms2.Coding == SMS_Coding_Default) {
		ReverseUnicodeString(sms2.Text);
	}

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	s[phone].s.User.SendSMSStatus 	= SendSMSStatus;
	s[phone].SendSMSStatus 		= ERR_TIMEOUT;

	error=s[phone].s.Phone.Functions->SendSMS(&s[phone].s,&sms2);
	SetErrorCounter(phone, error);
	if (error != ERR_NONE) {
	        ReleaseMutex(s[phone].Mutex);
		s[phone].s.User.SendSMSStatus = NULL;
		return error;
	}
	for (j=0;j<timeout;j++) {
		GSM_GetCurrentDateTime(&Date);
		i=Date.Second;
		while (i==Date.Second) {
			GSM_ReadDevice(&s[phone].s,false);
			if (s[phone].SendSMSStatus != ERR_TIMEOUT) break;
			GSM_GetCurrentDateTime(&Date);
			my_sleep(20);
		}
		if (s[phone].SendSMSStatus != ERR_TIMEOUT) break;
	}
	ReleaseMutex(s[phone].Mutex);
	SetErrorCounter(phone, s[phone].SendSMSStatus);
	s[phone].s.User.SendSMSStatus = NULL;
	return s[phone].SendSMSStatus;
}

GSM_Error WINAPI mygetsmsstatus (int phone, GSM_SMSMemoryStatus *status)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->GetSMSStatus(&s[phone].s,status);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

GSM_Error WINAPI myaddsmsmessage (int phone, GSM_SMSMessage *sms)
{
	GSM_Error 	error;
	GSM_SMSMessage	sms2;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

	memcpy(&sms2,sms,sizeof(GSM_SMSMessage));
	if (sms2.SMSC.Location == 0) ReverseUnicodeString(sms2.SMSC.Number);
	ReverseUnicodeString(sms2.Number);
	ReverseUnicodeString(sms2.Name);
	if (sms2.Coding == SMS_Coding_Unicode || sms2.Coding == SMS_Coding_Default) {
		ReverseUnicodeString(sms2.Text);
	}

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->AddSMS(&s[phone].s,&sms2);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

void WINAPI mygetnetworkname(char *NetworkCode, char *NetworkName)
{
	memcpy(NetworkName,GSM_GetNetworkName(NetworkCode),UnicodeLength(GSM_GetNetworkName(NetworkCode))*2+2);
	ReverseUnicodeString(NetworkName);
}

void WINAPI mygetgammuversion(char *version)
{
	sprintf(version, "%s",VERSION);
}

GSM_Error WINAPI mygetimei(int phone, char *IMEI)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->GetIMEI(&s[phone].s);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	if (error == ERR_NONE) strcpy(IMEI,s[phone].s.Phone.Data.IMEI);
	return error;
}

GSM_Error WINAPI mygetmanufacturer(int phone, char *manufacturer)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->GetManufacturer(&s[phone].s);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	if (error == ERR_NONE) strcpy(manufacturer,s[phone].s.Phone.Data.Manufacturer);
	return error;
}

GSM_Error WINAPI mygetmodel(int phone, char *model)
{
	if (!s[phone].s.opened) return ERR_NOTCONNECTED;
	strcpy(model,s[phone].s.Phone.Data.Model);
	return ERR_NONE;
}

GSM_Error WINAPI mygetmodelname(int phone, char *model)
{
	if (!s[phone].s.opened) return ERR_NOTCONNECTED;
	strcpy(model,s[phone].s.Phone.Data.ModelInfo->model);
	return ERR_NONE;
}

GSM_Error WINAPI mygetfirmwareversion(int phone, double *version)
{
	if (!s[phone].s.opened) return ERR_NOTCONNECTED;
	*version = s[phone].s.Phone.Data.VerNum;
	return ERR_NONE;
}

GSM_Error WINAPI myreset(int phone, bool hard)
{
	GSM_Error error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	error=s[phone].s.Phone.Functions->Reset(&s[phone].s,hard);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;

}

GSM_Error WINAPI mysmscounter(int 		MessageLength,
			      unsigned char 	*MessageBuffer,
		    	      GSM_UDH	 	UDH,
		    	      GSM_Coding_Type 	Coding,
		    	      int 		*SMSNum,
		    	      int 		*CharsLeft)
{
	/* FIXME. Reverse MessageBuffer */
	GSM_SMSCounter(MessageLength,MessageBuffer,UDH,Coding,SMSNum,CharsLeft);
	return ERR_NONE;
}

GSM_Error WINAPI mymakemultipartsms(unsigned char		*MessageBuffer,
			  	    int				MessageLength,
			  	    GSM_UDH			UDHType,
			  	    GSM_Coding_Type		Coding,
			  	    int				Class,
			  	    unsigned char		ReplaceMessage,
				    GSM_MultiSMSMessage		*SMS)
{
	int i;

	/* FIXME. Reverse MessageBuffer */
	GSM_MakeMultiPartSMS(SMS,MessageBuffer,MessageLength,UDHType,Coding,Class,ReplaceMessage);
	for (i=0;i<SMS->Number;i++) {
		if (SMS->SMS[i].Coding == SMS_Coding_Unicode || SMS->SMS[i].Coding == SMS_Coding_Default) {
			ReverseUnicodeString(SMS->SMS[i].Text);
		}
	}
	return ERR_NONE;
}

/* ------------------ see dct4.c ------------------------------------------- */

static GSM_Error DCT4_ReplyGetSimlock(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	int i,j=0;

	switch (msg.Buffer[3]) {
	case 0x0D:
		for (i=14;i<22;i++) {
			sprintf(s->Phone.Data.PhoneString+j,"%02x",msg.Buffer[i]);
			j=j+2;
		}
		return ERR_NONE;
	}
	return ERR_UNKNOWNRESPONSE;
}

char SecLength;

static GSM_Error DCT4_ReplyGetSecurityCode(GSM_Protocol_Message msg, GSM_StateMachine *s)
{
	if (msg.Length > 12) {
		SecLength = msg.Buffer[13];
		if ((msg.Buffer[17]+18) == msg.Length) {
			sprintf(s->Phone.Data.PhoneString,"%s",msg.Buffer+18);
		}
	}
	return ERR_NONE;
}

static GSM_Reply_Function UserReplyFunctions4[] = {
	{DCT4_ReplyGetSecurityCode,	"\x23",0x03,0x05,ID_User1	},
	{DCT4_ReplyGetSecurityCode,	"\x23",0x03,0x0D,ID_User1	}, 

	{DCT4_ReplyGetSimlock,		"\x53",0x03,0x0D,ID_User6	},
	{NULL,				"\x00",0x00,0x00,ID_None	}
};

GSM_Error WINAPI mygetdct4simlocknetwork(int phone, char *network)
{
	unsigned char 	GetSimlock[4] = {N6110_FRAME_HEADER, 0x0C};
	GSM_Error 	error;

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	s[phone].s.User.UserReplyFunctions = UserReplyFunctions4;
	network[0]			   = 0;
	s[phone].s.Phone.Data.PhoneString  = network;
	error=GSM_WaitFor (&s[phone].s, GetSimlock, 4, 0x53, 4, ID_User6);
	SetErrorCounter(phone, error);
        ReleaseMutex(s[phone].Mutex);
	return error;
}

GSM_Error WINAPI mygetdct4securitycode(int phone, char *code)
{
	GSM_Error 	error;
	unsigned char 	getlen[]={0x00, 0x08, 0x01, 0x0C, 
				  0x00, 0x23, 		//ID 
				  0x00, 0x00, 		//Index
				  0x00, 0x00}; 
	unsigned char 	read[]={0x00, 0x08, 0x02, 0x04,
				0x00, 0x23, 		//ID
				0x00, 0x00, 		//Index
				0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00,
				0x00};                  //Length

	if (!s[phone].s.opened) return ERR_NOTCONNECTED;

        WaitForSingleObject(s[phone].Mutex, INFINITE );
	s[phone].s.User.UserReplyFunctions = UserReplyFunctions4;
	code[0]			   	   = 0;
	s[phone].s.Phone.Data.PhoneString  = code;

	SecLength = 0;
	error=GSM_WaitFor (&s[phone].s, getlen, sizeof(getlen), 0x23, 1, ID_User1);
	SetErrorCounter(phone, error);
	if (SecLength != 0) {
		read[17] = SecLength;
		error=GSM_WaitFor (&s[phone].s, read, sizeof(read), 0x23, 5, ID_User1);
		SetErrorCounter(phone, error);
	}
        ReleaseMutex(s[phone].Mutex);
	return error;
}

/* ------------------------------------------------------------------------- */

int WINAPI mygetstructuresize(int i)
{
	switch (i) {
		case 0: return sizeof(GSM_SMSMessage);
		case 1: return sizeof(GSM_SMSC);
		case 2: return sizeof(GSM_SMS_State);
		case 3: return sizeof(GSM_UDHHeader);
		case 4: return sizeof(bool);
		case 5: return sizeof(GSM_DateTime);
		case 6: return sizeof(int);
		case 7: return sizeof(GSM_NetworkInfo);
		case 8: return sizeof(GSM_UDH);
	}
	return 0;
}

BOOL WINAPI DllMain  ( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved )
{
	int i;

	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			for (i=0;i<10;i++) {
				s[i].s.opened 	= false;
				s[i].Used	= false;
				s[i].dwThreadID = 0;
			}
			break;
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
			break;
		case DLL_PROCESS_DETACH:
			for (i=0;i<10;i++) {
				myendconnection(i);
			}
			break;
    	}
 	return TRUE;
}
