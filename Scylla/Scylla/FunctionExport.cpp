#include <windows.h>
#include "PeParser.h"
#include "ProcessAccessHelp.h"
#include "Scylla.h"
#include "Architecture.h"
#include "FunctionExport.h"
#include "ProcessLister.h"
#include "ApiReader.h"
#include "IATSearch.h"
#include "ImportRebuilder.h"
#include "DllInjectionPlugin.h"
#include  "libdasm.h"


extern HINSTANCE hDllModule;

const WCHAR * WINAPI ScyllaVersionInformationW()
{
	return APPNAME L" " ARCHITECTURE L" " APPVERSION;
}

const char * WINAPI ScyllaVersionInformationA()
{
	return APPNAME_S " " ARCHITECTURE_S " " APPVERSION_S;
}

DWORD WINAPI ScyllaVersionInformationDword()
{
	return APPVERSIONDWORD;
}

BOOL DumpProcessW(const WCHAR * fileToDump, DWORD_PTR imagebase, DWORD_PTR entrypoint, const WCHAR * fileResult)
{
	PeParser * peFile = 0;

	if (fileToDump)
	{
		peFile = new PeParser(fileToDump, true);
	}
	else
	{
		peFile = new PeParser(imagebase, true);
	}

	return peFile->dumpProcess(imagebase, entrypoint, fileResult);
}

BOOL WINAPI ScyllaRebuildFileW(const WCHAR * fileToRebuild, BOOL removeDosStub, BOOL updatePeHeaderChecksum, BOOL createBackup)
{

	if (createBackup)
	{
		if (!ProcessAccessHelp::createBackupFile(fileToRebuild))
		{
			return FALSE;
		}
	}

	PeParser peFile(fileToRebuild, true);
	if (peFile.readPeSectionsFromFile())
	{
		peFile.setDefaultFileAlignment();
		if (removeDosStub)
		{
			peFile.removeDosStub();
		}
		peFile.alignAllSectionHeaders();
		peFile.fixPeHeader();

		if (peFile.savePeFileToDisk(fileToRebuild))
		{
			if (updatePeHeaderChecksum)
			{
				PeParser::updatePeHeaderChecksum(fileToRebuild, (DWORD)ProcessAccessHelp::getFileSize(fileToRebuild));
			}
			return TRUE;
		}
	}

	return FALSE;
}

BOOL WINAPI ScyllaRebuildFileA(const char * fileToRebuild, BOOL removeDosStub, BOOL updatePeHeaderChecksum, BOOL createBackup)
{
	WCHAR fileToRebuildW[MAX_PATH];
	if (MultiByteToWideChar(CP_ACP, 0, fileToRebuild, -1, fileToRebuildW, _countof(fileToRebuildW)) == 0)
	{
		return FALSE;
	}

	return ScyllaRebuildFileW(fileToRebuildW, removeDosStub, updatePeHeaderChecksum, createBackup);
}

BOOL WINAPI ScyllaDumpCurrentProcessW(const WCHAR * fileToDump, DWORD_PTR imagebase, DWORD_PTR entrypoint, const WCHAR * fileResult)
{
	ProcessAccessHelp::setCurrentProcessAsTarget();

	return DumpProcessW(fileToDump, imagebase, entrypoint, fileResult);
}

BOOL WINAPI ScyllaDumpProcessW(DWORD_PTR pid, const WCHAR * fileToDump, DWORD_PTR imagebase, DWORD_PTR entrypoint, const WCHAR * fileResult)
{
	if (ProcessAccessHelp::openProcessHandle((DWORD)pid))
	{
		return DumpProcessW(fileToDump, imagebase, entrypoint, fileResult);
	}
	else
	{
		return FALSE;
	}	
}

BOOL WINAPI ScyllaDumpCurrentProcessA(const char * fileToDump, DWORD_PTR imagebase, DWORD_PTR entrypoint, const char * fileResult)
{
	WCHAR fileToDumpW[MAX_PATH];
	WCHAR fileResultW[MAX_PATH];

	if (fileResult == 0)
	{
		return FALSE;
	}

	if (MultiByteToWideChar(CP_ACP, 0, fileResult, -1, fileResultW, _countof(fileResultW)) == 0)
	{
		return FALSE;
	}

	if (fileToDump != 0)
	{
		if (MultiByteToWideChar(CP_ACP, 0, fileToDump, -1, fileToDumpW, _countof(fileToDumpW)) == 0)
		{
			return FALSE;
		}

		return ScyllaDumpCurrentProcessW(fileToDumpW, imagebase, entrypoint, fileResultW);
	}
	else
	{
		return ScyllaDumpCurrentProcessW(0, imagebase, entrypoint, fileResultW);
	}
}

BOOL WINAPI ScyllaDumpProcessA(DWORD_PTR pid, const char * fileToDump, DWORD_PTR imagebase, DWORD_PTR entrypoint, const char * fileResult)
{
	WCHAR fileToDumpW[MAX_PATH];
	WCHAR fileResultW[MAX_PATH];

	if (fileResult == 0)
	{
		return FALSE;
	}

	if (MultiByteToWideChar(CP_ACP, 0, fileResult, -1, fileResultW, _countof(fileResultW)) == 0)
	{
		return FALSE;
	}

	if (fileToDump != 0)
	{
		if (MultiByteToWideChar(CP_ACP, 0, fileToDump, -1, fileToDumpW, _countof(fileToDumpW)) == 0)
		{
			return FALSE;
		}

		return ScyllaDumpProcessW(pid, fileToDumpW, imagebase, entrypoint, fileResultW);
	}
	else
	{
		return ScyllaDumpProcessW(pid, 0, imagebase, entrypoint, fileResultW);
	}
}

INT WINAPI ScyllaStartGui(DWORD dwProcessId, HINSTANCE mod)
{
	GUI_DLL_PARAMETER guiParam;
	guiParam.dwProcessId = dwProcessId;
	guiParam.mod = mod;

	return InitializeGui(hDllModule, (LPARAM)&guiParam);
}

int WINAPI ScyllaIatSearch(DWORD dwProcessId, DWORD_PTR * iatStart, DWORD * iatSize, DWORD_PTR searchStart, BOOL advancedSearch)
{
	ApiReader apiReader;
	ProcessLister processLister;
	Process *processPtr = 0;
	IATSearch iatSearch;

	std::vector<Process>& processList = processLister.getProcessListSnapshotNative();
	for(std::vector<Process>::iterator it = processList.begin(); it != processList.end(); ++it)
	{
		if(it->PID == dwProcessId)
		{
			processPtr = &(*it);
			break;
		}
	}

	if(!processPtr) return SCY_ERROR_PIDNOTFOUND;

	ProcessAccessHelp::closeProcessHandle();
	apiReader.clearAll();

	if (!ProcessAccessHelp::openProcessHandle(processPtr->PID))
	{
		return SCY_ERROR_PROCOPEN;
	}

	ProcessAccessHelp::getProcessModules(ProcessAccessHelp::hProcess, ProcessAccessHelp::moduleList);

	ProcessAccessHelp::selectedModule = 0;
	ProcessAccessHelp::targetImageBase = processPtr->imageBase;
	ProcessAccessHelp::targetSizeOfImage = processPtr->imageSize;

	apiReader.readApisFromModuleList();

	int retVal = SCY_ERROR_IATNOTFOUND;

	if (advancedSearch)
	{
		if (iatSearch.searchImportAddressTableInProcess(searchStart, iatStart, iatSize, true))
		{
			retVal = SCY_ERROR_SUCCESS;
		}
	}
	else
	{
		if (iatSearch.searchImportAddressTableInProcess(searchStart, iatStart, iatSize, false))
		{
			retVal = SCY_ERROR_SUCCESS;
		}
	}

	processList.clear();
	ProcessAccessHelp::closeProcessHandle();
	apiReader.clearAll();

	return retVal;
}


DWORD_PTR getNumberOfUnresolvedImports( std::map<DWORD_PTR, ImportModuleThunk> & moduleList )
{
	std::map<DWORD_PTR, ImportModuleThunk>::iterator iterator1;
	std::map<DWORD_PTR, ImportThunk>::iterator iterator2;
	ImportModuleThunk * moduleThunk = 0;
	ImportThunk * importThunk = 0;
	DWORD_PTR dwNumber = 0;

	iterator1 = moduleList.begin();

	while (iterator1 != moduleList.end())
	{
		moduleThunk = &(iterator1->second);

		iterator2 = moduleThunk->thunkList.begin();

		while (iterator2 != moduleThunk->thunkList.end())
		{
			importThunk = &(iterator2->second);

			if (importThunk->valid == false)
			{
				dwNumber++;
			}

			iterator2++;
		}

		iterator1++;
	}

	return dwNumber;
}

void addUnresolvedImports( PUNRESOLVED_IMPORT firstUnresImp, std::map<DWORD_PTR, ImportModuleThunk> & moduleList )
{
	std::map<DWORD_PTR, ImportModuleThunk>::iterator iterator1;
	std::map<DWORD_PTR, ImportThunk>::iterator iterator2;
	ImportModuleThunk * moduleThunk = 0;
	ImportThunk * importThunk = 0;

	iterator1 = moduleList.begin();

	while (iterator1 != moduleList.end())
	{
		moduleThunk = &(iterator1->second);

		iterator2 = moduleThunk->thunkList.begin();

		while (iterator2 != moduleThunk->thunkList.end())
		{
			importThunk = &(iterator2->second);

			if (importThunk->valid == false)
			{
				firstUnresImp->InvalidApiAddress = importThunk->apiAddressVA;
				firstUnresImp->ImportTableAddressPointer = importThunk->va;
				firstUnresImp++;
			}

			iterator2++;
		}

		iterator1++;
	}

	firstUnresImp->InvalidApiAddress = 0;
	firstUnresImp->ImportTableAddressPointer = 0;
}

void displayModuleList(std::map<DWORD_PTR, ImportModuleThunk> & moduleList )
{
	std::map<DWORD_PTR, ImportModuleThunk>::iterator iterator1;
	std::map<DWORD_PTR, ImportThunk>::iterator iterator2;
	ImportModuleThunk * moduleThunk = 0;
	ImportThunk * importThunk = 0;

	iterator1 = moduleList.begin();

	while (iterator1 != moduleList.end())
	{
		moduleThunk = &(iterator1->second);

		iterator2 = moduleThunk->thunkList.begin();

		while (iterator2 != moduleThunk->thunkList.end())
		{
			importThunk = &(iterator2->second);

			printf("VA : %08x\t API ADDRESS : %08x\n", importThunk->va, importThunk->apiAddressVA);
			fflush(stdout);

			iterator2++;
		}

		iterator1++;
	}

}

void customFix(DWORD_PTR numberOfUnresolvedImports, std::map<DWORD_PTR, ImportModuleThunk> moduleList){
	printf("Unresolved imports detected...\n");

	PUNRESOLVED_IMPORT unresolvedImport = 0;
	unresolvedImport = (PUNRESOLVED_IMPORT)calloc(numberOfUnresolvedImports, sizeof(UNRESOLVED_IMPORT));

	addUnresolvedImports(unresolvedImport, moduleList);
	//local variable
	int insDelta;
	char buffer[2048];
	DWORD_PTR IATbase;
	int i,j;
	INSTRUCTION inst;
	DWORD_PTR invalidApiAddress = 0;
		
	while (unresolvedImport->ImportTableAddressPointer != 0) //last element is a nulled struct
	{
		memset(buffer, 0x00, sizeof(buffer));
		insDelta = 0;
		invalidApiAddress = unresolvedImport->InvalidApiAddress;
		printf(buffer, "API Address = 0x%p\t IAT Address = 0x%p\n",  invalidApiAddress, unresolvedImport->ImportTableAddressPointer);
		//get the starting IAT address to be analyzed yet
		IATbase = unresolvedImport->InvalidApiAddress;
		for (j = 0; j <  1000; j++)
		{
			memset(&inst, 0x00, sizeof(INSTRUCTION));
			//get the disassembled instruction
			i = get_instruction(&inst, (BYTE *)IATbase, MODE_32);
			memset(buffer, 0x00, sizeof(buffer));
			get_instruction_string(&inst, FORMAT_ATT, 0, buffer, sizeof(buffer));
			//check if it is a jump
			if (strstr(buffer, "jmp"))
			{
				//calculate the correct answer (add the invalidApiAddress to the destination of the jmp because it is a short jump)
				unsigned int correct_address = ( (unsigned int)strtol(strstr(buffer, "jmp") + 4 + 2, NULL, 16)) + invalidApiAddress - insDelta;
				printf(" \nIAT ENTRY = %08x\t CONTENT = %08x\n" , unresolvedImport->ImportTableAddressPointer, *(DWORD*)(unresolvedImport->ImportTableAddressPointer));
				//writeToLogFile(buffer2);
				*(DWORD*)(unresolvedImport->ImportTableAddressPointer) =  correct_address;
				printf(" IAT ENTRY = %08x\t CONTENT = %08x\n" , unresolvedImport->ImportTableAddressPointer, *(DWORD*)(unresolvedImport->ImportTableAddressPointer));
				//unresolvedImport->InvalidApiAddress = correct_address;
				printf(" JUMP Dest = %08x\n\n" , correct_address);
				fflush(stdout);
				break;
			}
			//if not increment the delta for the next fix (es : if we have encountered 4 instruction before the correct jmp we have to decrement the correct_address by 16 byte)
			else{
				insDelta = insDelta + i;
			}
			//check the next row inthe IAT
			IATbase = IATbase + i;
		}
		unresolvedImport++; //next pointer to struct
	}

}

int WINAPI ScyllaIatFixAutoW(DWORD_PTR iatAddr, DWORD iatSize, DWORD dwProcessId, const WCHAR * dumpFile, const WCHAR * iatFixFile)
{
	printf("\n\n\n\n\n STIANO ENTRANDO!!\n\n\n\n");	
	ApiReader apiReader;
	ProcessLister processLister;
	Process *processPtr = 0;

	std::map<DWORD_PTR, ImportModuleThunk> moduleList;

	std::vector<Process>& processList = processLister.getProcessListSnapshotNative();

	for(std::vector<Process>::iterator it = processList.begin(); it != processList.end(); ++it)
	{
		if(it->PID == dwProcessId)
		{
			processPtr = &(*it);					//Get the Processn which has the PID dwProcessPid
			break;
		}
	}

	if(!processPtr) return SCY_ERROR_PIDNOTFOUND;

	ProcessAccessHelp::closeProcessHandle();
	apiReader.clearAll();

	if (!ProcessAccessHelp::openProcessHandle(processPtr->PID))
	{
		return SCY_ERROR_PROCOPEN;
	}
	ProcessAccessHelp::getProcessModules(ProcessAccessHelp::hProcess, ProcessAccessHelp::moduleList);  //In ProcessAccessHelp::moduleList List of the Dll loaded by the process and other useful information of the Process with PID equal dwProcessId
	ProcessAccessHelp::selectedModule = 0;
	ProcessAccessHelp::targetImageBase = processPtr->imageBase;
	ProcessAccessHelp::targetSizeOfImage = processPtr->imageSize;
	
	apiReader.readApisFromModuleList();		//fill the apiReader::apiList with the function exported by the dll in ProcessAccessHelp::moduleList

	apiReader.readAndParseIAT(iatAddr, iatSize, moduleList);

	//DEBUG

	DWORD_PTR numberOfUnresolvedImports = getNumberOfUnresolvedImports(moduleList);
	printf("n\n\n\n\nNUMERO : %d!!!!\n\n\n\n", numberOfUnresolvedImports);
	
	if (numberOfUnresolvedImports != 0){

		customFix(numberOfUnresolvedImports, moduleList);

		apiReader.clearAll();

		moduleList.clear();

		ProcessAccessHelp::getProcessModules(ProcessAccessHelp::hProcess, ProcessAccessHelp::moduleList);

		//apiReader.readApisFromModuleList();

		apiReader.readAndParseIAT(iatAddr, iatSize, moduleList);

		printf("\n\n\n\n\n CI SIAMO!!\n\n\n\n");	
		
	}

	displayModuleList(moduleList);

	//FINE DEBUG

	//add IAT section to dump

	ImportRebuilder importRebuild(dumpFile);

	importRebuild.enableOFTSupport();

	int retVal = SCY_ERROR_IATWRITE;

	if (importRebuild.rebuildImportTable(iatFixFile, moduleList))
	{	
		retVal = SCY_ERROR_SUCCESS;
	}

	processList.clear();

	moduleList.clear();
	ProcessAccessHelp::closeProcessHandle();
		
	apiReader.clearAll();

	printf("\n\n\n\n\n STIANO USCENDO!!\n\n\n\n");	

	return retVal;
}

