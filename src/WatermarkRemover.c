// Watermark Remover by Brendler - v2.0
// © 2026 Brendler · Todos os direitos reservados
// Método: igual ao uwd2 — localiza CDesktopWatermark::s_DesktopBuildPaint (shell32.dll)
// pelos símbolos públicos da Microsoft e coloca um "ret" no início dessa função
// dentro do explorer.exe em execução. Não altera nenhum arquivo do Windows.
// O efeito dura até o Explorer/PC reiniciar (por isso existe a opção de iniciar com o Windows).
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <winhttp.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

#define APP_NAME    L"Watermark Remover by Brendler"
#define APP_TITLE   APP_NAME
#define APP_VERSION L"2.0"
#define REPO_URL    L"https://github.com/fbrendler/WatermarkRemover-Brendler"
#define REPO_SHORT  L"github.com/fbrendler/WatermarkRemover-Brendler"
#define RUN_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_NAME  L"BrendlerWatermarkRemover"
#define OLD_RUN_NAME L"BrendlerRemoverMarcaDagua"

static WCHAR g_dir[MAX_PATH];   // %LOCALAPPDATA%\Brendler\WatermarkRemover
static FILE *g_log;
static BOOL g_silent;

static void Log(const wchar_t *fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fwprintf(g_log, L"%02d/%02d %02d:%02d:%02d  ", t.wDay, t.wMonth, t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfwprintf(g_log, fmt, a); va_end(a);
    fwprintf(g_log, L"\n"); fflush(g_log);
}

static void Msg(LPCWSTR text, UINT icon) {
    if (!g_silent) MessageBoxW(NULL, text, APP_TITLE, icon);
}

/* ---------- 1. Lê GUID/Age do PDB a partir do cabeçalho da DLL ---------- */
typedef struct { DWORD sig; GUID guid; DWORD age; char name[1]; } RSDS;

static BOOL GetPdbInfo(HMODULE mod, WCHAR *pdbName, WCHAR *key) {
    BYTE *b = (BYTE *)((ULONG_PTR)mod & ~(ULONG_PTR)3);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (!dd.VirtualAddress) return FALSE;
    IMAGE_DEBUG_DIRECTORY *d = (IMAGE_DEBUG_DIRECTORY *)(b + dd.VirtualAddress);
    for (DWORD i = 0; i < dd.Size / sizeof(*d); i++) {
        if (d[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
        RSDS *r = (RSDS *)(b + d[i].AddressOfRawData);
        if (r->sig != 0x53445352) continue; // 'RSDS'
        const char *n = r->name, *s = strrchr(n, '\\');
        if (s) n = s + 1;
        MultiByteToWideChar(CP_ACP, 0, n, -1, pdbName, MAX_PATH);
        GUID g = r->guid;
        swprintf(key, 64, L"%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%lX",
                 g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                 g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7], r->age);
        return TRUE;
    }
    return FALSE;
}

/* ---------- 2. Baixa o PDB do servidor de símbolos da Microsoft ---------- */
static BOOL Download(LPCWSTR path, LPCWSTR dest) {
    BOOL ok = FALSE;
    HINTERNET s = WinHttpOpen(L"Microsoft-Symbol-Server/10.0.0.0",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"Microsoft-Symbol-Server/10.0.0.0",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!s) return FALSE;
    HINTERNET c = WinHttpConnect(s, L"msdl.microsoft.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        Log(L"Download HTTP %lu", code);
        if (code == 200) {
            WCHAR tmp[MAX_PATH]; swprintf(tmp, MAX_PATH, L"%ls.part", dest);
            HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                static BYTE buf[65536]; DWORD n, w; ok = TRUE;
                while (WinHttpReadData(r, buf, sizeof(buf), &n) && n) {
                    if (!WriteFile(f, buf, n, &w, NULL)) { ok = FALSE; break; }
                }
                CloseHandle(f);
                if (ok) ok = MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING);
                else DeleteFileW(tmp);
            }
        }
    } else Log(L"Falha de conexão (erro %lu)", GetLastError());
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

/* ---------- 3. Procura a função da marca d'água no PDB ---------- */
typedef struct { ULONG64 base, addr; int score; WCHAR name[256]; } FindCtx;

static BOOL CALLBACK EnumCb(PSYMBOL_INFOW si, ULONG size, PVOID p) {
    FindCtx *f = (FindCtx *)p;
    int score = wcsstr(si->Name, L"CDesktopWatermark::s_DesktopBuildPaint") ? 3 :
                wcsstr(si->Name, L"::s_DesktopBuildPaint") ? 2 : 1;
    Log(L"  símbolo: %ls (RVA 0x%llX)", si->Name, si->Address - f->base);
    if (score > f->score) {
        f->score = score; f->addr = si->Address; lstrcpynW(f->name, si->Name, 256);
    }
    return TRUE;
}

static DWORD FindWatermarkRva(LPCWSTR dllName) {
    HMODULE mod = LoadLibraryExW(dllName, NULL, 0);
    if (!mod) { Log(L"Não carregou %ls", dllName); return 0; }
    WCHAR dllPath[MAX_PATH]; GetModuleFileNameW(mod, dllPath, MAX_PATH);
    WCHAR pdb[MAX_PATH], key[64];
    if (!GetPdbInfo(mod, pdb, key)) { Log(L"Sem info de PDB em %ls", dllName); return 0; }

    WCHAR folder[MAX_PATH], file[MAX_PATH], url[512];
    swprintf(folder, MAX_PATH, L"%ls\\simbolos\\%ls", g_dir, key);
    swprintf(file, MAX_PATH, L"%ls\\%ls", folder, pdb);
    SHCreateDirectoryExW(NULL, folder, NULL);
    if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES) {
        swprintf(url, 512, L"/download/symbols/%ls/%ls/%ls", pdb, key, pdb);
        Log(L"Baixando %ls", url);
        if (!Download(url, file)) { Log(L"Falha ao baixar símbolos"); return 0; }
    } else Log(L"Usando símbolos já baixados: %ls", key);

    HANDLE hp = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!SymInitializeW(hp, folder, FALSE)) { Log(L"SymInitialize falhou %lu", GetLastError()); return 0; }
    const ULONG64 BASE = 0x10000000;
    DWORD rva = 0;
    if (SymLoadModuleExW(hp, NULL, dllPath, NULL, BASE, 0, NULL, 0)) {
        IMAGEHLP_MODULEW64 mi = { sizeof(mi) };
        SymGetModuleInfoW64(hp, BASE, &mi);
        Log(L"Tipo de símbolo carregado: %d (3 = PDB)", mi.SymType);
        FindCtx f = { BASE, 0, 0, L"" };
        SymEnumSymbolsW(hp, BASE, L"*DesktopBuildPaint*", EnumCb, &f);
        if (f.addr) { rva = (DWORD)(f.addr - BASE); Log(L"Escolhido: %ls RVA 0x%lX", f.name, rva); }
    } else Log(L"SymLoadModule falhou %lu", GetLastError());
    SymCleanup(hp);
    return rva;
}

/* ---------- 4. Aplica o "ret" no(s) explorer.exe ---------- */
static ULONG_PTR RemoteModuleBase(DWORD pid, LPCWSTR name) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me = { sizeof(me) }; ULONG_PTR base = 0;
    if (Module32FirstW(s, &me)) do {
        if (!lstrcmpiW(me.szModule, name)) { base = (ULONG_PTR)me.modBaseAddr; break; }
    } while (Module32NextW(s, &me));
    CloseHandle(s);
    return base;
}

// retorna nº de explorers corrigidos (ou já corrigidos)
static int PatchExplorers(LPCWSTR dllName, DWORD rva, const BYTE *expected) {
    int done = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (s == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(s, &pe)) do {
        if (lstrcmpiW(pe.szExeFile, L"explorer.exe")) continue;
        ULONG_PTR base = RemoteModuleBase(pe.th32ProcessID, dllName);
        if (!base) continue;
        HANDLE p = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                               PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) { Log(L"Sem acesso ao explorer %lu (%lu)", pe.th32ProcessID, GetLastError()); continue; }
        LPVOID at = (LPVOID)(base + rva);
        BYTE cur[8]; SIZE_T n = 0;
        if (ReadProcessMemory(p, at, cur, sizeof(cur), &n)) {
            if (cur[0] == 0xC3) { Log(L"Explorer %lu já estava corrigido", pe.th32ProcessID); done++; }
            else if (memcmp(cur, expected, sizeof(cur))) {
                Log(L"Explorer %lu: bytes não conferem, não alterado (segurança)", pe.th32ProcessID);
            } else {
                DWORD old; BYTE ret = 0xC3;
                if (VirtualProtectEx(p, at, 1, PAGE_EXECUTE_READWRITE, &old) &&
                    WriteProcessMemory(p, at, &ret, 1, &n)) {
                    VirtualProtectEx(p, at, 1, old, &old);
                    FlushInstructionCache(p, at, 1);
                    Log(L"Explorer %lu corrigido", pe.th32ProcessID); done++;
                } else Log(L"Falha ao escrever no explorer %lu (%lu)", pe.th32ProcessID, GetLastError());
            }
        }
        CloseHandle(p);
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return done;
}

static BOOL CALLBACK RedrawCb(HWND h, LPARAM l) {
    WCHAR c[64]; GetClassNameW(h, c, 64);
    if (!lstrcmpW(c, L"WorkerW") || !lstrcmpW(c, L"Progman"))
        RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
    return TRUE;
}

static void RefreshDesktop(void) {
    EnumWindows(RedrawCb, 0);
    WCHAR wp[MAX_PATH] = L"";
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wp, 0) && wp[0] &&
        GetFileAttributesW(wp) != INVALID_FILE_ATTRIBUTES)
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, wp, SPIF_SENDCHANGE);
    EnumWindows(RedrawCb, 0);
}

static BOOL RemoveNow(void) {
    LPCWSTR dlls[] = { L"shell32.dll", L"explorerframe.dll", L"windows.storage.dll" };
    for (int i = 0; i < 3; i++) {
        Log(L"Procurando em %ls", dlls[i]);
        DWORD rva = FindWatermarkRva(dlls[i]);
        if (!rva) continue;
        HMODULE m = GetModuleHandleW(dlls[i]);
        BYTE expected[8]; memcpy(expected, (BYTE *)m + rva, 8);
        int n = PatchExplorers(dlls[i], rva, expected);
        if (n > 0) { RefreshDesktop(); return TRUE; }
    }
    return FALSE;
}

/* ---------- 5. Iniciar com o Windows ---------- */
static BOOL SetAutostart(BOOL on) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, OLD_RUN_NAME); // entrada da versão antiga
    if (!on) return RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_NAME) == ERROR_SUCCESS;
    WCHAR self[MAX_PATH], dest[MAX_PATH], cmd[MAX_PATH + 32];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(dest, MAX_PATH, L"%ls\\WatermarkRemoverByBrendler.exe", g_dir);
    if (lstrcmpiW(self, dest)) CopyFileW(self, dest, FALSE);
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" /silent", dest);
    return RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_NAME, REG_SZ, cmd,
                           (DWORD)(wcslen(cmd) + 1) * sizeof(WCHAR)) == ERROR_SUCCESS;
}

static BOOL AutostartOn(void) {
    DWORD sz = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_NAME, RRF_RT_REG_SZ, NULL, NULL, &sz) == ERROR_SUCCESS;
}

/* ---------- 6. Sobre ---------- */
static WCHAR g_logPath[MAX_PATH];
static HICON g_icon;

// Copia o final do log para a área de transferência (para colar no relato do GitHub)
static void CopyLogToClipboard(void) {
    HANDLE f = CreateFileW(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    char buf[16384]; DWORD n = 0;
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
        if (sz.QuadPart > (LONGLONG)sizeof(buf) - 1) {
            LARGE_INTEGER pos; pos.QuadPart = sz.QuadPart - (sizeof(buf) - 1);
            SetFilePointerEx(f, pos, NULL, FILE_BEGIN);
        }
        ReadFile(f, buf, sizeof(buf) - 1, &n, NULL);
        CloseHandle(f);
    }
    buf[n] = 0;
    WCHAR hdr[256];
    WCHAR cb[64] = L"?"; DWORD cbs = sizeof(cb);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild", RRF_RT_REG_SZ, NULL, cb, &cbs);
    swprintf(hdr, 256, L"%ls %ls — Windows build %ls\r\n\r\n", APP_NAME, APP_VERSION, cb);
    char *start = buf;
    if ((BYTE)start[0] == 0xEF && (BYTE)start[1] == 0xBB && (BYTE)start[2] == 0xBF) start += 3;
    int wl = MultiByteToWideChar(CP_UTF8, 0, start, -1, NULL, 0);
    size_t total = (wcslen(hdr) + wl + 1) * sizeof(WCHAR);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!g) return;
    WCHAR *w = (WCHAR *)GlobalLock(g);
    lstrcpyW(w, hdr);
    MultiByteToWideChar(CP_UTF8, 0, start, -1, w + wcslen(hdr), wl);
    GlobalUnlock(g);
    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g);
        CloseClipboard();
    } else GlobalFree(g);
}

enum { BTN_REMOVE = 101, BTN_AUTO_ON, BTN_AUTO_OFF, BTN_LOG, BTN_ABOUT, BTN_REPORT, BTN_PROJECT };

static HRESULT CALLBACK AboutCb(HWND h, UINT msg, WPARAM wp, LPARAM lp, LONG_PTR ref) {
    if (msg == TDN_BUTTON_CLICKED) {
        if (wp == BTN_REPORT) {
            CopyLogToClipboard();
            ShellExecuteW(h, L"open", REPO_URL L"/issues/new", NULL, NULL, SW_SHOWNORMAL);
            return S_FALSE; // mantém a janela aberta
        }
        if (wp == BTN_PROJECT) {
            ShellExecuteW(h, L"open", REPO_URL, NULL, NULL, SW_SHOWNORMAL);
            return S_FALSE;
        }
    }
    return S_OK;
}

static void ShowAbout(void) {
    TASKDIALOG_BUTTON btns[] = {
        { BTN_REPORT,  L"Relatar problema\nAbre a página de problemas do projeto no GitHub. "
                       L"O registro do app é copiado para você colar no relato." },
        { BTN_PROJECT, L"Página do projeto\n" REPO_SHORT },
    };
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_HICON_MAIN;
    tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    tc.pszWindowTitle = L"Sobre";
    tc.hMainIcon = g_icon;
    tc.pszMainInstruction = APP_NAME;
    tc.pszContent = L"Versão " APP_VERSION L"\n© 2026 Brendler · Todos os direitos reservados";
    tc.pButtons = btns; tc.cButtons = 2;
    tc.pszFooter = L"Remove a marca d'água das versões Insider do Windows sem alterar nenhum "
                   L"arquivo do sistema.";
    tc.pfCallback = AboutCb;
    tc.cxWidth = 270;
    TaskDialogIndirect(&tc, NULL, NULL, NULL);
}

/* ---------- Interface ---------- */
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE hp, LPWSTR cmd, int show) {
    WCHAR la[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, la);
    swprintf(g_dir, MAX_PATH, L"%ls\\Brendler\\WatermarkRemover", la);
    SHCreateDirectoryExW(NULL, g_dir, NULL);
    swprintf(g_logPath, MAX_PATH, L"%ls\\log.txt", g_dir);
    g_log = _wfopen(g_logPath, L"a, ccs=UTF-8");
    g_silent = cmd && wcsstr(cmd, L"/silent");
    Log(L"==== %ls %ls iniciado %ls ====", APP_NAME, APP_VERSION, g_silent ? L"(automático)" : L"");

    if (g_silent) {
        // Espera o Explorer terminar de abrir (até ~2 min) e tenta algumas vezes
        for (int i = 0; i < 24; i++) {
            if (FindWindowW(L"Shell_TrayWnd", NULL) && RemoveNow()) { Log(L"OK"); return 0; }
            Sleep(5000);
        }
        Log(L"Não foi possível remover no modo automático");
        return 1;
    }

    InitCommonControls();
    LoadIconWithScaleDown(hi, MAKEINTRESOURCEW(1), GetSystemMetrics(SM_CXICON) * 2,
                          GetSystemMetrics(SM_CYICON) * 2, &g_icon);
    if (!g_icon) g_icon = LoadIconW(hi, MAKEINTRESOURCEW(1));

    TASKDIALOG_BUTTON btns[] = {
        { BTN_REMOVE,   L"Remover a marca d'água agora\nNão altera arquivos do Windows. Vale até reiniciar o PC." },
        { BTN_AUTO_ON,  L"Remover agora e sempre que o PC ligar\nO programa roda sozinho, escondido, a cada login." },
        { BTN_AUTO_OFF, L"Desativar a remoção automática\nA marca d'água volta depois de reiniciar." },
        { BTN_LOG,      L"Abrir o relatório (log)\nPara diagnóstico se algo não funcionar." },
        { BTN_ABOUT,    L"Sobre\nVersão, relatar problema e página do projeto." },
    };
    int b = 0;
    for (;;) {
        WCHAR content[300];
        swprintf(content, 300,
            L"Na 1ª vez (e depois de cada atualização do Windows) o programa baixa da Microsoft "
            L"um arquivo de símbolos, então é preciso internet.\n\nRemoção automática: %ls",
            AutostartOn() ? L"ATIVADA" : L"desativada");
        TASKDIALOGCONFIG tc = { sizeof(tc) };
        tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_HICON_MAIN;
        tc.dwCommonButtons = TDCBF_CLOSE_BUTTON;
        tc.pszWindowTitle = APP_TITLE;
        tc.hMainIcon = g_icon;
        tc.pszMainInstruction = L"Remover a marca d'água do Windows Insider";
        tc.pszContent = content;
        tc.pButtons = btns; tc.cButtons = 5;
        tc.pszFooter = L"© 2026 Brendler · Versão " APP_VERSION;
        b = 0;
        if (FAILED(TaskDialogIndirect(&tc, &b, NULL, NULL))) return 1;
        if (b == BTN_ABOUT) { ShowAbout(); continue; }
        break;
    }

    if (b == BTN_LOG) { ShellExecuteW(NULL, L"open", g_logPath, NULL, NULL, SW_SHOWNORMAL); return 0; }
    if (b == BTN_AUTO_OFF) {
        SetAutostart(FALSE);
        Msg(L"Remoção automática desativada.", MB_ICONINFORMATION);
        return 0;
    }
    if (b != BTN_REMOVE && b != BTN_AUTO_ON) return 0;

    HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    BOOL ok = RemoveNow();
    SetCursor(old);
    if (b == BTN_AUTO_ON) SetAutostart(TRUE);

    if (ok) Msg(b == BTN_AUTO_ON
                ? L"Pronto! Marca d'água removida.\n\nA remoção automática está ativada: ao ligar o PC "
                  L"ela some sozinha alguns segundos depois do login."
                : L"Pronto! Marca d'água removida.\n\nSe reiniciar o PC, rode o programa de novo "
                  L"(ou ative a remoção automática).",
                MB_ICONINFORMATION);
    else Msg(L"Não foi possível remover a marca d'água.\n\nCausas comuns:\n"
             L"• Sem internet para baixar os símbolos da Microsoft\n"
             L"• O antivírus bloqueou o programa\n"
             L"• Esta build do Windows mudou o nome da função\n\n"
             L"Em \"Sobre → Relatar problema\" você pode enviar o relatório pelo GitHub.",
             MB_ICONWARNING);
    return ok ? 0 : 1;
}
