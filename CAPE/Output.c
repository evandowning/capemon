/*
CAPE - Config And Payload Extraction
Copyright(C) 2015, 2016 Context Information Security. (kevin.oreilly@contextis.com)

This program is free software : you can redistribute it and / or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.If not, see <http://www.gnu.org/licenses/>.
*/
#include <time.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <Shlwapi.h>
#include "cape.h"
#include "..\pipe.h"
#include "..\config.h"

//#define DEBUG_COMMENTS
#define MAX_INT_STRING_LEN	10 // 4294967294
#define BUFFER_SIZE			0x200

TCHAR DebugBuffer[MAX_PATH];
TCHAR PipeBuffer[MAX_PATH];
TCHAR ErrorBuffer[MAX_PATH];
CHAR DebuggerLine[MAX_PATH];

extern char* GetResultsPath(char* FolderName);
extern struct CapeMetadata *CapeMetaData;
extern ULONG_PTR base_of_dll_of_interest;
HANDLE DebuggerLog;
extern SIZE_T LastWriteLength;
extern BOOL StopTrace;

//**************************************************************************************
void OutputString(_In_ LPCTSTR lpOutputString, va_list args)
//**************************************************************************************
{
	if (g_config.disable_logging)
		return;

	memset(DebugBuffer, 0, MAX_PATH*sizeof(CHAR));
	_vsntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, lpOutputString, args);
	if (g_config.standalone)
		OutputDebugString(DebugBuffer);
	else
	{
		memset(PipeBuffer, 0, MAX_PATH*sizeof(CHAR));
		_sntprintf_s(PipeBuffer, MAX_PATH, _TRUNCATE, "DEBUG:%s", DebugBuffer);
		pipe(PipeBuffer, strlen(PipeBuffer));
	}
	return;
}

//**************************************************************************************
void DebugOutput(_In_ LPCTSTR lpOutputString, ...)
//**************************************************************************************
{
	va_list args;
	va_start(args, lpOutputString);
	OutputString(lpOutputString, args);
	va_end(args);
	return;
}

//**************************************************************************************
void ErrorOutput(_In_ LPCTSTR lpOutputString, ...)
//**************************************************************************************
{
	va_list args;
	LPVOID lpMsgBuf;
	DWORD ErrorCode;

	ErrorCode = GetLastError();
	va_start(args, lpOutputString);

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		ErrorCode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0,
		NULL);

	memset(DebugBuffer, 0, MAX_PATH*sizeof(CHAR));
	_vsntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, lpOutputString, args);

	memset(ErrorBuffer, 0, MAX_PATH*sizeof(CHAR));
	_sntprintf_s(ErrorBuffer, MAX_PATH, _TRUNCATE, "Error %d (0x%x) - %s: %s", ErrorCode, ErrorCode, DebugBuffer, (char*)lpMsgBuf);
	if (g_config.standalone)
		OutputDebugString(ErrorBuffer);
	else
	{
		memset(PipeBuffer, 0, MAX_PATH*sizeof(CHAR));
		_sntprintf_s(PipeBuffer, MAX_PATH, _TRUNCATE, "DEBUG:%s", ErrorBuffer);
		pipe(PipeBuffer, strlen(PipeBuffer));
	}

	va_end(args);

	return;
}

//**************************************************************************************
void DoOutputFile(_In_ LPCTSTR lpOutputFile)
//**************************************************************************************
{
	TCHAR OutputBuffer[MAX_PATH];
	memset(OutputBuffer, 0, MAX_PATH*sizeof(TCHAR));
	_sntprintf_s(OutputBuffer, MAX_PATH, _TRUNCATE, "FILE_DUMP:%s", lpOutputFile);
	pipe(OutputBuffer, strlen(OutputBuffer));
	return;
}

//**************************************************************************************
void CapeOutputFile(_In_ LPCTSTR lpOutputFile)
//**************************************************************************************
{
#ifdef CAPE_V1
	char MetadataPath[MAX_PATH];
	HANDLE hMetadata;
	SIZE_T BufferSize;
	char *Buffer;
	DWORD dwBytesWritten;

	if (CapeMetaData && CapeMetaData->DumpType == PROCDUMP)
	{
		memset(MetadataPath, 0, MAX_PATH * sizeof(TCHAR));
		_sntprintf_s(MetadataPath, MAX_PATH, _TRUNCATE, "%s_info.txt", lpOutputFile);
		hMetadata = CreateFile(MetadataPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hMetadata == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS)
		{
			DebugOutput("CAPE metadata filename exists already: %s", MetadataPath);
			return;
		}

		if (hMetadata == INVALID_HANDLE_VALUE)
		{
			ErrorOutput("Could not create CAPE metadata file");
			return;
		}

		BufferSize = 3 * (MAX_PATH + MAX_INT_STRING_LEN + 2) + 2; //// max size string can be

		Buffer = malloc(BufferSize);

		// if our file of interest is a dll, we need to update cape module path now
		if (base_of_dll_of_interest)
		{
			if (g_config.file_of_interest == NULL)
			{
				DebugOutput("CAPE Error: g_config.file_of_interest is NULL.\n", g_config.file_of_interest);
				return;
			}

			CapeMetaData->ModulePath = (char*)malloc(MAX_PATH);
			WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, (LPCWSTR)g_config.file_of_interest, (int)wcslen(g_config.file_of_interest)+1, CapeMetaData->ModulePath, MAX_PATH, NULL, NULL);
		}
		else
			CapeMetaData->ModulePath = CapeMetaData->ProcessPath;

		// This metadata format is specific to process dumps
		_snprintf_s(Buffer, BufferSize, BufferSize, "%d\n%d\n%s\n%s\n", CapeMetaData->DumpType, CapeMetaData->Pid, CapeMetaData->ProcessPath, CapeMetaData->ModulePath);

		if (FALSE == WriteFile(hMetadata, Buffer, (DWORD)strlen(Buffer), &dwBytesWritten, NULL))
		{
			DebugOutput("WriteFile error on CAPE metadata file %s\n");
			CloseHandle(hMetadata);
			free(Buffer);
			return;
		}

		CloseHandle(hMetadata);

		memset(DebugBuffer, 0, MAX_PATH*sizeof(TCHAR));
		_sntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, "Process dump output file: %s", lpOutputFile);

		if (g_config.standalone)
			OutputDebugString(DebugBuffer);

		memset(PipeBuffer, 0, MAX_PATH*sizeof(TCHAR));
		_sntprintf_s(PipeBuffer, MAX_PATH, _TRUNCATE, "FILE_DUMP:%s", lpOutputFile);
		pipe(PipeBuffer, strlen(PipeBuffer));

	}
	else if (CapeMetaData && CapeMetaData->DumpType != PROCDUMP)
	{
		memset(MetadataPath, 0, MAX_PATH * sizeof(TCHAR));
		_sntprintf_s(MetadataPath, MAX_PATH, _TRUNCATE, "%s_info.txt", lpOutputFile);
		hMetadata = CreateFile(MetadataPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

		if (hMetadata == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS)
		{
			DebugOutput("CAPE metadata filename exists already: %s", MetadataPath);
			return;
		}

		if (hMetadata == INVALID_HANDLE_VALUE)
		{
			ErrorOutput("Could not create CAPE metadata file");
			return;
		}

		BufferSize = 3 * (MAX_PATH + MAX_INT_STRING_LEN + 2) + 2; //// max size string can be

		Buffer = malloc(BufferSize);

		if (!CapeMetaData->ProcessPath)
			CapeMetaData->ProcessPath = "Unknown path";
		CapeMetaData->ModulePath = CapeMetaData->ProcessPath;

		if (CapeMetaData->DumpType == UNPACKED_PE || CapeMetaData->DumpType == UNPACKED_SHELLCODE)
		{
			// Unpacker-specific format
			_snprintf_s(Buffer, BufferSize, BufferSize, "%d\n%d\n%s\n%s\n0x%p\n", CapeMetaData->DumpType, CapeMetaData->Pid, CapeMetaData->ProcessPath, CapeMetaData->ModulePath, CapeMetaData->Address);
		}
		else if (CapeMetaData->DumpType == INJECTION_PE || CapeMetaData->DumpType == INJECTION_SHELLCODE || CapeMetaData->DumpType == EVILGRAB_PAYLOAD || CapeMetaData->DumpType == EVILGRAB_DATA)
		{
			if (CapeMetaData->TargetProcess && CapeMetaData->ProcessPath)
			// Injection-specific format
				_snprintf_s(Buffer, BufferSize, BufferSize, "%d\n%d\n%s\n%s\n%s\n%d\n", CapeMetaData->DumpType, CapeMetaData->Pid, CapeMetaData->ProcessPath, CapeMetaData->ModulePath, CapeMetaData->TargetProcess, CapeMetaData->TargetPid);
		}
		else
			if (CapeMetaData->ProcessPath)
				_snprintf_s(Buffer, BufferSize, BufferSize, "%d\n%d\n%s\n%s\n", CapeMetaData->DumpType, CapeMetaData->Pid, CapeMetaData->ProcessPath, CapeMetaData->ModulePath);

		if (FALSE == WriteFile(hMetadata, Buffer, (DWORD)strlen(Buffer), &dwBytesWritten, NULL))
		{
			DebugOutput("WriteFile error on CAPE metadata file %s\n");
			CloseHandle(hMetadata);
			free(Buffer);
			return;
		}

		CloseHandle(hMetadata);

		memset(DebugBuffer, 0, MAX_PATH*sizeof(TCHAR));
		_sntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, "CAPE Output file: %s", lpOutputFile);

		if (g_config.standalone)
			OutputDebugString(DebugBuffer);

		memset(PipeBuffer, 0, MAX_PATH*sizeof(TCHAR));
		_sntprintf_s(PipeBuffer, MAX_PATH, _TRUNCATE, "FILE_CAPE:%s", lpOutputFile);
		pipe(PipeBuffer, strlen(PipeBuffer));
	}
	else
		DebugOutput("No CAPE metadata (or wrong type) for file: %s\n", lpOutputFile);

	return;
#else
	SIZE_T BufferSize;
	char *MetadataString;

	if (CapeMetaData && CapeMetaData->DumpType == PROCDUMP)
	{
		BufferSize = 4 * (MAX_PATH + MAX_INT_STRING_LEN + 2) + 2; //// max size string can be

		MetadataString = malloc(BufferSize);

		// if our file of interest is a dll, we need to update cape module path now
		if (base_of_dll_of_interest)
		{
			if (g_config.file_of_interest == NULL)
			{
				DebugOutput("CAPE Error: g_config.file_of_interest is NULL.\n", g_config.file_of_interest);
				return;
			}

			CapeMetaData->ModulePath = (char*)malloc(MAX_PATH);
			WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, (LPCWSTR)g_config.file_of_interest, (int)wcslen(g_config.file_of_interest)+1, CapeMetaData->ModulePath, MAX_PATH, NULL, NULL);
		}
		else
			CapeMetaData->ModulePath = CapeMetaData->ProcessPath;

		// This metadata format is specific to process dumps
		_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath);

		memset(DebugBuffer, 0, MAX_PATH*sizeof(TCHAR));
		_sntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, "Process dump output file: %s", lpOutputFile);
		if (g_config.standalone)
			OutputDebugString(DebugBuffer);
		else
		{
			char OutputBuffer[BUFFER_SIZE];
			memset(OutputBuffer, 0, BUFFER_SIZE*sizeof(char));
			_snprintf_s(OutputBuffer, BUFFER_SIZE, _TRUNCATE, "FILE_DUMP:%s|%d|%d|%s", lpOutputFile, CapeMetaData->Pid, CapeMetaData->PPid, MetadataString);
			pipe(OutputBuffer, strlen(OutputBuffer));
		}
	}
	else if (CapeMetaData && CapeMetaData->DumpType != PROCDUMP)
	{
		BufferSize = 4 * (MAX_PATH + MAX_INT_STRING_LEN + 2) + 2; //// max size string can be

		MetadataString = malloc(BufferSize);

		if (!CapeMetaData->ProcessPath)
			CapeMetaData->ProcessPath = "Unknown path";
		CapeMetaData->ModulePath = CapeMetaData->ProcessPath;

		if (CapeMetaData->DumpType == UNPACKED_PE || CapeMetaData->DumpType == UNPACKED_SHELLCODE)
		{
			// Unpacker-specific format
			_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?0x%p;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath, CapeMetaData->Address);
		}
		else if (CapeMetaData->DumpType == INJECTION_PE || CapeMetaData->DumpType == INJECTION_SHELLCODE)
		{
			if (CapeMetaData->TargetProcess)
			// Injection-specific format
				_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?%s;?%d;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath, CapeMetaData->TargetProcess, CapeMetaData->TargetPid);
			else
			{
				DebugOutput("Output: TargetProcess missing for dump from process %d", CapeMetaData->Pid);
				_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath);
			}
		}
		else
			if (CapeMetaData->TypeString && strlen(CapeMetaData->TypeString))
			{
				CapeMetaData->DumpType = TYPE_STRING;
				_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?%s;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath, CapeMetaData->TypeString);
			}
			else
				_snprintf_s(MetadataString, BufferSize, BufferSize, "%d;?%s;?%s;?", CapeMetaData->DumpType, CapeMetaData->ProcessPath, CapeMetaData->ModulePath);

		if (g_config.standalone)
		{
			memset(DebugBuffer, 0, MAX_PATH*sizeof(TCHAR));
			_sntprintf_s(DebugBuffer, MAX_PATH, _TRUNCATE, "CAPE Output file: %s", lpOutputFile);
			OutputDebugString(DebugBuffer);
		}
		else
		{
			char OutputBuffer[BUFFER_SIZE];
			memset(OutputBuffer, 0, BUFFER_SIZE*sizeof(char));
			_sntprintf_s(OutputBuffer, BUFFER_SIZE, _TRUNCATE, "FILE_CAPE:%s|%d|%d|%s", lpOutputFile, CapeMetaData->Pid, CapeMetaData->PPid, MetadataString);
			pipe(OutputBuffer, strlen(OutputBuffer));
		}
	}
	else
		DebugOutput("No CAPE metadata (or wrong type) for file: %s\n", lpOutputFile);

	return;
#endif
}

//**************************************************************************************
void DebuggerOutput(_In_ LPCTSTR lpOutputString, ...)
//**************************************************************************************
{
	va_list args;
	char *FullPathName, *OutputFilename, *Character;

	va_start(args, lpOutputString);

	if (g_config.no_logs > 1)
		return;
	else if (g_config.no_logs)
	{
		OutputString(lpOutputString, args);
		va_end(args);
		return;
	}

	FullPathName = GetResultsPath("debugger");

	OutputFilename = (char*)malloc(MAX_PATH);

	if (OutputFilename == NULL)
	{
		ErrorOutput("DebuggerOutput: failed to allocate memory for file name string");
		return;
	}

	sprintf_s(OutputFilename, MAX_PATH, "%d.log", GetCurrentProcessId());

	PathAppend(FullPathName, OutputFilename);

	free(OutputFilename);

	if (!DebuggerLog && !StopTrace)
	{
		time_t Time;
		CHAR TimeBuffer[64];

		DebuggerLog = CreateFile(FullPathName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

		if (DebuggerLog == INVALID_HANDLE_VALUE)
		{
			ErrorOutput("DebuggerOutput: Unable to open debugger logfile %s.\n", FullPathName);
			return;
		}
		DebugOutput("DebuggerOutput: Debugger logfile %s.\n", FullPathName);

		time(&Time);
		memset(DebuggerLine, 0, MAX_PATH*sizeof(CHAR));
		ctime_s(TimeBuffer, 64, (const time_t *)&Time);
		_snprintf_s(DebuggerLine, MAX_PATH, _TRUNCATE, "CAPE Sandbox - Debugger log: %s" , TimeBuffer);
		WriteFile(DebuggerLog, DebuggerLine, (DWORD)strlen(DebuggerLine), (LPDWORD)&LastWriteLength, NULL);
		while (*lpOutputString == 0x0a)
			lpOutputString++;
	}
	else if (StopTrace)
	{
		va_end(args);
		return;
	}

	memset(DebuggerLine, 0, MAX_PATH*sizeof(CHAR));
	_vsnprintf_s(DebuggerLine, MAX_PATH, _TRUNCATE, lpOutputString, args);
	Character = DebuggerLine;
	while (*Character)
	{   // Restrict to ASCII range
		if (*Character < 0x0a || *Character > 0x7E)
			*Character = 0x3F;  // '?'
		Character++;
	}
	WriteFile(DebuggerLog, DebuggerLine, (DWORD)strlen(DebuggerLine), (LPDWORD)&LastWriteLength, NULL);

	va_end(args);

	return;
}
