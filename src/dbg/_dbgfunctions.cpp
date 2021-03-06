/**
 @file _dbgfunctions.cpp

 @brief Implements the dbgfunctions class.
 */

#include "_global.h"
#include "_dbgfunctions.h"
#include "assemble.h"
#include "debugger.h"
#include "jit.h"
#include "patches.h"
#include "memory.h"
#include "disasm_fast.h"
#include "stackinfo.h"
#include "symbolinfo.h"
#include "module.h"
#include "exhandlerinfo.h"

static DBGFUNCTIONS _dbgfunctions;

const DBGFUNCTIONS* dbgfunctionsget()
{
    return &_dbgfunctions;
}

static bool _assembleatex(duint addr, const char* instruction, char* error, bool fillnop)
{
    return assembleat(addr, instruction, nullptr, error, fillnop);
}

static bool _sectionfromaddr(duint addr, char* section)
{
    std::vector<MODSECTIONINFO> sections;
    if(ModSectionsFromAddr(addr, &sections))
    {
        for(const auto & cur : sections)
        {
            if(addr >= cur.addr && addr < cur.addr + (cur.size + (0x1000 - 1) & ~(0x1000 - 1)))
            {
                strcpy_s(section, MAX_SECTION_SIZE, cur.name);
                return true;
            }
        }
    }
    return false;
}

static bool _patchget(duint addr)
{
    return PatchGet(addr, nullptr);
}

static bool _patchinrange(duint start, duint end)
{
    if(start > end)
        std::swap(start, end);

    for(duint i = start; i <= end; i++)
    {
        if(_patchget(i))
            return true;
    }

    return false;
}

static bool _mempatch(duint va, const unsigned char* src, duint size)
{
    return MemPatch(va, src, size, nullptr);
}

static void _patchrestorerange(duint start, duint end)
{
    if(start > end)
        std::swap(start, end);

    for(duint i = start; i <= end; i++)
        PatchDelete(i, true);

    GuiUpdatePatches();
}

static bool _patchrestore(duint addr)
{
    return PatchDelete(addr, true);
}

static void _getcallstack(DBGCALLSTACK* callstack)
{
    stackgetcallstack(GetContextDataEx(hActiveThread, UE_CSP), (CALLSTACK*)callstack);
}

static void _getsehchain(DBGSEHCHAIN* sehchain)
{
    std::vector<duint> SEHList;
    ExHandlerGetSEH(SEHList);
    sehchain->total = SEHList.size();
    if(sehchain->total > 0)
    {
        sehchain->records = (DBGSEHRECORD*)BridgeAlloc(sehchain->total * sizeof(DBGSEHRECORD));
        for(size_t i = 0; i < sehchain->total; i++)
        {
            sehchain->records[i].addr = SEHList[i];
            MemRead(SEHList[i] + 4, &sehchain->records[i].handler, sizeof(duint));
        }
    }
}

static bool _getjitauto(bool* jit_auto)
{
    return dbggetjitauto(jit_auto, notfound, NULL, NULL);
}

static bool _getcmdline(char* cmd_line, size_t* cbsize)
{
    if(!cmd_line && !cbsize)
        return false;
    char* cmdline;
    if(!dbggetcmdline(&cmdline, NULL))
        return false;
    if(!cmd_line && cbsize)
        *cbsize = strlen(cmdline) + sizeof(char);
    else if(cmd_line)
        strcpy(cmd_line, cmdline);
    efree(cmdline, "_getcmdline:cmdline");
    return true;
}

static bool _setcmdline(const char* cmd_line)
{
    return dbgsetcmdline(cmd_line, nullptr);
}

static bool _getjit(char* jit, bool jit64)
{
    arch dummy;
    char jit_tmp[JIT_ENTRY_MAX_SIZE] = "";
    if(jit != NULL)
    {
        if(!dbggetjit(jit_tmp, jit64 ? x64 : x32, &dummy, NULL))
            return false;
        strcpy_s(jit, MAX_SETTING_SIZE, jit_tmp);
    }
    else // if jit input == NULL: it returns false if there are not an OLD JIT STORED.
    {
        char oldjit[MAX_SETTING_SIZE] = "";
        if(!BridgeSettingGet("JIT", "Old", (char*) & oldjit))
            return false;
    }

    return true;
}

bool _getprocesslist(DBGPROCESSINFO** entries, int* count)
{
    std::vector<PROCESSENTRY32> list;
    if(!dbglistprocesses(&list))
        return false;
    *count = (int)list.size();
    if(!*count)
        return false;
    *entries = (DBGPROCESSINFO*)BridgeAlloc(*count * sizeof(DBGPROCESSINFO));
    for(int i = 0; i < *count; i++)
    {
        (*entries)[*count - i - 1].dwProcessId = list.at(i).th32ProcessID;
        strcpy_s((*entries)[*count - i - 1].szExeFile, list.at(i).szExeFile);
    }
    return true;
}

static void _memupdatemap()
{
    MemUpdateMap();
    GuiUpdateMemoryView();
}

static duint _getaddrfromline(const char* szSourceFile, int line)
{
    LONG displacement = 0;
    IMAGEHLP_LINE64 lineData;
    memset(&lineData, 0, sizeof(lineData));
    lineData.SizeOfStruct = sizeof(lineData);
    if(!SymGetLineFromName64(fdProcessInfo->hProcess, NULL, szSourceFile, line, &displacement, &lineData))
        return 0;
    return (duint)lineData.Address;
}

static bool _getsourcefromaddr(duint addr, char* szSourceFile, int* line)
{
    char sourceFile[MAX_STRING_SIZE] = "";
    if(!SymGetSourceLine(addr, sourceFile, line))
        return false;
    if(!FileExists(sourceFile))
        return false;
    if(szSourceFile)
        strcpy_s(szSourceFile, MAX_STRING_SIZE, sourceFile);
    return true;
}

static bool _valfromstring(const char* string, duint* value)
{
    return valfromstring(string, value);
}

void dbgfunctionsinit()
{
    _dbgfunctions.AssembleAtEx = _assembleatex;
    _dbgfunctions.SectionFromAddr = _sectionfromaddr;
    _dbgfunctions.ModNameFromAddr = ModNameFromAddr;
    _dbgfunctions.ModBaseFromAddr = ModBaseFromAddr;
    _dbgfunctions.ModBaseFromName = ModBaseFromName;
    _dbgfunctions.ModSizeFromAddr = ModSizeFromAddr;
    _dbgfunctions.Assemble = assemble;
    _dbgfunctions.PatchGet = _patchget;
    _dbgfunctions.PatchInRange = _patchinrange;
    _dbgfunctions.MemPatch = _mempatch;
    _dbgfunctions.PatchRestoreRange = _patchrestorerange;
    _dbgfunctions.PatchEnum = (PATCHENUM)PatchEnum;
    _dbgfunctions.PatchRestore = _patchrestore;
    _dbgfunctions.PatchFile = (PATCHFILE)PatchFile;
    _dbgfunctions.ModPathFromAddr = ModPathFromAddr;
    _dbgfunctions.ModPathFromName = ModPathFromName;
    _dbgfunctions.DisasmFast = disasmfast;
    _dbgfunctions.MemUpdateMap = _memupdatemap;
    _dbgfunctions.GetCallStack = _getcallstack;
    _dbgfunctions.GetSEHChain = _getsehchain;
    _dbgfunctions.SymbolDownloadAllSymbols = SymDownloadAllSymbols;
    _dbgfunctions.GetJit = _getjit;
    _dbgfunctions.GetJitAuto = _getjitauto;
    _dbgfunctions.GetDefJit = dbggetdefjit;
    _dbgfunctions.GetProcessList = _getprocesslist;
    _dbgfunctions.GetPageRights = MemGetPageRights;
    _dbgfunctions.SetPageRights = MemSetPageRights;
    _dbgfunctions.PageRightsToString = MemPageRightsToString;
    _dbgfunctions.IsProcessElevated = IsProcessElevated;
    _dbgfunctions.GetCmdline = _getcmdline;
    _dbgfunctions.SetCmdline = _setcmdline;
    _dbgfunctions.FileOffsetToVa = valfileoffsettova;
    _dbgfunctions.VaToFileOffset = valvatofileoffset;
    _dbgfunctions.GetAddrFromLine = _getaddrfromline;
    _dbgfunctions.GetSourceFromAddr = _getsourcefromaddr;
    _dbgfunctions.ValFromString = _valfromstring;
    _dbgfunctions.PatchGetEx = (PATCHGETEX)PatchGet;
}
