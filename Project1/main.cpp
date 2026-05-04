#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "sqlite3.h"

#pragma comment(lib, "comctl32.lib")

// --- Идентификаторы ---
#define ID_BTN_GPU      201
#define ID_BTN_MGR_MENU 202
#define ID_BTN_EXIT      206

#define ID_MENU_MANUF   501
#define ID_MENU_ARCH    502
#define ID_MENU_MEMTYPE 503
#define ID_MENU_BUS     504

#define ID_MGR_LIST     301
#define ID_MGR_EDIT     302
#define ID_MGR_ADD      303
#define ID_MGR_DEL      304
#define ID_MGR_SEARCH   308

#define ID_GPU_LIST     401
#define ID_GPU_MODEL    402
#define ID_GPU_PRICE    403 
#define ID_GPU_ADD      407
#define ID_GPU_DEL      408
#define ID_GPU_SEARCH   409
#define ID_GPU_STYPE    410
#define ID_GPU_PRICE_MIN 411
#define ID_GPU_PRICE_MAX 412

// --- Глобальные переменные ---
sqlite3* db = NULL;
const char* currentTable = "";
int editId = -1;

HWND hGpuList, hModelEdit, hPriceEdit, hCmbManuf, hCmbArch, hCmbMemType, hCmbBus;
HWND hGpuSearch, hGpuSearchType, hBtnAddGpu, hPriceMin, hPriceMax;
HWND hMgrList, hMgrEdit, hMgrSearch, hMgrLblSearch, hMgrBtnAdd;

// --- Вспомогательные функции ---

void ExecSQL(const char* sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, NULL, NULL, &errMsg) != SQLITE_OK) {
        MessageBoxA(NULL, errMsg, "Ошибка SQL", MB_OK | MB_ICONERROR);
        sqlite3_free(errMsg);
    }
}

void GetLvText(HWND hList, int iItem, int iSub, char* buf, int len) {
    LVITEMA lvi = { 0 };
    lvi.iSubItem = iSub; lvi.pszText = buf; lvi.cchTextMax = len;
    SendMessageA(hList, LVM_GETITEMTEXTA, (WPARAM)iItem, (LPARAM)&lvi);
}

std::string SafeSQL(const char* input) {
    std::string s = input ? input : "";
    size_t pos = 0;
    while ((pos = s.find("'", pos)) != std::string::npos) { s.replace(pos, 1, "''"); pos += 2; }
    return s;
}

bool IsIdUsed(int id, const std::string& tableName) {
    std::string column;
    if (tableName == "Manufacturers") column = "manuf_id";
    else if (tableName == "Architecture") column = "arch_id";
    else if (tableName == "MemoryType") column = "memtype_id";
    else if (tableName == "BusType") column = "bus_id";
    else return false;

    std::string sql = "SELECT COUNT(*) FROM GPU WHERE " + column + " = " + std::to_string(id);
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count > 0;
}

bool DoesValueExist(const std::string& val, const std::string& tableName) {
    std::string sql = "SELECT COUNT(*) FROM " + tableName + " WHERE value = '" + SafeSQL(val.c_str()) + "'";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count > 0;
}

void InitDatabase() {
    sqlite3_open("computers.db", &db);
    ExecSQL("CREATE TABLE IF NOT EXISTS Manufacturers (id INTEGER PRIMARY KEY, value TEXT);");
    ExecSQL("CREATE TABLE IF NOT EXISTS Architecture (id INTEGER PRIMARY KEY, value TEXT);");
    ExecSQL("CREATE TABLE IF NOT EXISTS MemoryType (id INTEGER PRIMARY KEY, value TEXT);");
    ExecSQL("CREATE TABLE IF NOT EXISTS BusType (id INTEGER PRIMARY KEY, value TEXT);");
    ExecSQL("CREATE TABLE IF NOT EXISTS GPU (id INTEGER PRIMARY KEY, model TEXT, manuf_id INTEGER, arch_id INTEGER, memtype_id INTEGER, bus_id INTEGER, price REAL);");
}

// --- Логика справочников ---

void RefreshMgrList() {
    if (!hMgrList) return;
    ListView_DeleteAllItems(hMgrList);
    std::string sql = "SELECT id, value FROM " + std::string(currentTable) + " WHERE 1=1";
    char sch[256] = { 0 }; GetWindowTextA(hMgrSearch, sch, 256);
    if (strlen(sch) > 0) sql += " AND value LIKE '%" + SafeSQL(sch) + "%'";

    sqlite3_stmt* s;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &s, NULL) == SQLITE_OK) {
        int r = 0;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LVITEMA li = { 0 }; li.mask = LVIF_TEXT; li.iItem = r;
            li.pszText = (LPSTR)sqlite3_column_text(s, 0); SendMessageA(hMgrList, LVM_INSERTITEMA, 0, (LPARAM)&li);
            li.iSubItem = 1; li.pszText = (LPSTR)sqlite3_column_text(s, 1); SendMessageA(hMgrList, LVM_SETITEMTEXTA, r, (LPARAM)&li);
            r++;
        } sqlite3_finalize(s);
    }
}

LRESULT CALLBACK MgrProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        editId = -1;
        hMgrLblSearch = CreateWindow(L"STATIC", L"Поиск:", WS_CHILD | WS_VISIBLE, 10, 12, 50, 20, hwnd, NULL, NULL, NULL);
        hMgrSearch = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 65, 10, 225, 25, hwnd, (HMENU)ID_MGR_SEARCH, NULL, NULL);
        hMgrList = CreateWindowEx(0, WC_LISTVIEW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 10, 45, 285, 165, hwnd, (HMENU)ID_MGR_LIST, NULL, NULL);
        ListView_SetExtendedListViewStyle(hMgrList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        {
            LVCOLUMNA lvc = { 0 }; lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.pszText = (LPSTR)"ID"; lvc.cx = 40; SendMessageA(hMgrList, LVM_INSERTCOLUMNA, 0, (LPARAM)&lvc);
            lvc.pszText = (LPSTR)"Значение"; lvc.cx = 220; SendMessageA(hMgrList, LVM_INSERTCOLUMNA, 1, (LPARAM)&lvc);
        }
        hMgrEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 220, 180, 25, hwnd, (HMENU)ID_MGR_EDIT, NULL, NULL);
        hMgrBtnAdd = CreateWindow(L"BUTTON", L"Добавить", WS_CHILD | WS_VISIBLE, 200, 220, 95, 25, hwnd, (HMENU)ID_MGR_ADD, NULL, NULL);
        CreateWindow(L"BUTTON", L"Удалить", WS_CHILD | WS_VISIBLE, 10, 255, 285, 30, hwnd, (HMENU)ID_MGR_DEL, NULL, NULL);
        RefreshMgrList(); break;

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == ID_MGR_LIST && nm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hMgrList, -1, LVNI_SELECTED);
            if (sel != -1) {
                char idStr[16] = { 0 }, valStr[256] = { 0 };
                GetLvText(hMgrList, sel, 0, idStr, 16); GetLvText(hMgrList, sel, 1, valStr, 256);
                editId = atoi(idStr); SetWindowTextA(hMgrEdit, valStr); SetWindowText(hMgrBtnAdd, L"Обновить");
            }
        }
    } break;

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == hMgrSearch) RefreshMgrList();
        if (LOWORD(wParam) == ID_MGR_ADD) {
            char b[256] = { 0 }; GetWindowTextA(hMgrEdit, b, 256); if (strlen(b) == 0) return 0;
            if (editId == -1 && DoesValueExist(b, currentTable)) { MessageBox(hwnd, L"Уже есть!", L"Ошибка", MB_OK); return 0; }
            if (editId == -1) ExecSQL(("INSERT INTO " + std::string(currentTable) + " (value) VALUES ('" + SafeSQL(b) + "')").c_str());
            else { ExecSQL(("UPDATE " + std::string(currentTable) + " SET value='" + SafeSQL(b) + "' WHERE id=" + std::to_string(editId)).c_str()); SetWindowText(hMgrBtnAdd, L"Добавить"); editId = -1; }
            SetWindowText(hMgrEdit, L""); RefreshMgrList();
        }
        if (LOWORD(wParam) == ID_MGR_DEL) {
            int sel = (int)SendMessage(hMgrList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
            if (sel != -1) {
                char idStr[16] = { 0 }; GetLvText(hMgrList, sel, 0, idStr, 16);
                if (IsIdUsed(atoi(idStr), currentTable)) MessageBox(hwnd, L"Используется!", L"Ошибка", MB_OK);
                else if (MessageBox(hwnd, L"Удалить?", L"БД", MB_YESNO) == IDYES) { ExecSQL(("DELETE FROM " + std::string(currentTable) + " WHERE id=" + idStr).c_str()); RefreshMgrList(); }
            }
        } break;
    } return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// --- Логика каталога GPU ---

void RefreshGpuList() {
    if (!hGpuList) return;
    ListView_DeleteAllItems(hGpuList);

    char nF[256] = { 0 }, pMin[64] = { 0 }, pMax[64] = { 0 };
    GetWindowTextA(hGpuSearch, nF, 256);
    GetWindowTextA(hPriceMin, pMin, 64);
    GetWindowTextA(hPriceMax, pMax, 64);
    int type = (int)SendMessage(hGpuSearchType, CB_GETCURSEL, 0, 0);

    std::string sql = "SELECT G.id, G.model, M.value, A.value, MT.value, B.value, G.price FROM GPU G "
        "LEFT JOIN Manufacturers M ON G.manuf_id = M.id LEFT JOIN Architecture A ON G.arch_id = A.id "
        "LEFT JOIN MemoryType MT ON G.memtype_id = MT.id LEFT JOIN BusType B ON G.bus_id = B.id WHERE 1=1";

    if (strlen(nF) > 0) {
        // Поиск по: Модель(0), Бренд(1), Архитектура(2), Память(3), Шина(4)
        std::string col = (type == 0) ? "G.model" : (type == 1) ? "M.value" : (type == 2) ? "A.value" : (type == 3) ? "MT.value" : "B.value";
        sql += " AND " + col + " LIKE '%" + SafeSQL(nF) + "%'";
    }

    if (strlen(pMin) > 0) sql += " AND G.price >= " + std::string(pMin);
    if (strlen(pMax) > 0) sql += " AND G.price <= " + std::string(pMax);

    sqlite3_stmt* stmt; if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {
        int row = 0; while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int i = 0; i < 7; i++) {
                const char* t = (const char*)sqlite3_column_text(stmt, i);
                LVITEMA li = { 0 }; li.mask = LVIF_TEXT; li.iItem = row; li.iSubItem = i; li.pszText = (LPSTR)(t ? t : "-");
                if (i == 0) SendMessageA(hGpuList, LVM_INSERTITEMA, 0, (LPARAM)&li); else SendMessageA(hGpuList, LVM_SETITEMTEXTA, row, (LPARAM)&li);
            } row++;
        } sqlite3_finalize(stmt);
    }
}

LRESULT CALLBACK GpuProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        editId = -1;
        CreateWindow(L"STATIC", L"Поиск:", WS_CHILD | WS_VISIBLE, 10, 12, 45, 20, hwnd, NULL, NULL, NULL);
        hGpuSearch = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 55, 10, 150, 25, hwnd, (HMENU)ID_GPU_SEARCH, NULL, NULL);
        hGpuSearchType = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 210, 10, 120, 200, hwnd, (HMENU)ID_GPU_STYPE, NULL, NULL);
        { const wchar_t* t[] = { L"Модель", L"Бренд", L"Архитектура", L"Память", L"Шина" }; for (int i = 0; i < 5; i++) SendMessage(hGpuSearchType, CB_ADDSTRING, 0, (LPARAM)t[i]); }
        SendMessage(hGpuSearchType, CB_SETCURSEL, 0, 0);

        // Панель диапазона цен
        CreateWindow(L"STATIC", L"Цена от:", WS_CHILD | WS_VISIBLE, 340, 12, 55, 20, hwnd, NULL, NULL, NULL);
        hPriceMin = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 400, 10, 70, 25, hwnd, (HMENU)ID_GPU_PRICE_MIN, NULL, NULL);
        CreateWindow(L"STATIC", L"до:", WS_CHILD | WS_VISIBLE, 475, 12, 25, 20, hwnd, NULL, NULL, NULL);
        hPriceMax = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 505, 10, 70, 25, hwnd, (HMENU)ID_GPU_PRICE_MAX, NULL, NULL);

        hGpuList = CreateWindowEx(0, WC_LISTVIEW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_BORDER, 10, 45, 765, 185, hwnd, (HMENU)ID_GPU_LIST, NULL, NULL);
        ListView_SetExtendedListViewStyle(hGpuList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        {
            const char* c[] = { "ID", "Модель", "Бренд", "Архитектура", "Тип памяти", "Шина", "Цена" };
            for (int i = 0; i < 7; i++) {
                LVCOLUMNA l = { 0 }; l.mask = LVCF_TEXT | LVCF_WIDTH; l.pszText = (LPSTR)c[i];
                l.cx = (i == 0 ? 40 : 110); SendMessageA(hGpuList, LVM_INSERTCOLUMNA, i, (LPARAM)&l);
            }
        }

        hModelEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 270, 120, 25, hwnd, (HMENU)ID_GPU_MODEL, NULL, NULL);
        hCmbManuf = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 135, 270, 120, 200, hwnd, NULL, NULL, NULL);
        hCmbArch = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 260, 270, 120, 200, hwnd, NULL, NULL, NULL);
        hCmbMemType = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 385, 270, 120, 200, hwnd, NULL, NULL, NULL);
        hCmbBus = CreateWindow(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 510, 270, 120, 200, hwnd, NULL, NULL, NULL);
        hPriceEdit = CreateWindow(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 635, 270, 140, 25, hwnd, (HMENU)ID_GPU_PRICE, NULL, NULL);

        {
            auto Fill = [](HWND h, const char* t) { sqlite3_stmt* s; sqlite3_prepare_v2(db, ("SELECT id, value FROM " + std::string(t)).c_str(), -1, &s, NULL);
            while (sqlite3_step(s) == SQLITE_ROW) { int idx = (int)SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)sqlite3_column_text(s, 1)); SendMessage(h, CB_SETITEMDATA, idx, (LPARAM)sqlite3_column_int(s, 0)); }
            sqlite3_finalize(s); SendMessage(h, CB_SETCURSEL, 0, 0); };
            Fill(hCmbManuf, "Manufacturers"); Fill(hCmbArch, "Architecture"); Fill(hCmbMemType, "MemoryType"); Fill(hCmbBus, "BusType");
        }
        hBtnAddGpu = CreateWindow(L"BUTTON", L"Добавить", WS_CHILD | WS_VISIBLE, 10, 310, 375, 30, hwnd, (HMENU)ID_GPU_ADD, NULL, NULL);
        CreateWindow(L"BUTTON", L"Удалить", WS_CHILD | WS_VISIBLE, 395, 310, 375, 30, hwnd, (HMENU)ID_GPU_DEL, NULL, NULL);
        RefreshGpuList(); break;

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == ID_GPU_LIST && nm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hGpuList, -1, LVNI_SELECTED);
            if (sel != -1) {
                char idStr[16], modStr[256], prcStr[64];
                GetLvText(hGpuList, sel, 0, idStr, 16);
                GetLvText(hGpuList, sel, 1, modStr, 256);
                GetLvText(hGpuList, sel, 6, prcStr, 64);
                editId = atoi(idStr);
                SetWindowTextA(hModelEdit, modStr);
                SetWindowTextA(hPriceEdit, prcStr);
                SetWindowText(hBtnAddGpu, L"Обновить");
            }
        }
    } break;

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE) RefreshGpuList();
        if (LOWORD(wParam) == ID_GPU_ADD) {
            char mod[256] = { 0 }, prc[64] = { 0 };
            GetWindowTextA(hModelEdit, mod, 256);
            GetWindowTextA(hPriceEdit, prc, 64);
            if (strlen(mod) == 0) return 0;

            auto gID = [](HWND c) { int s = (int)SendMessage(c, CB_GETCURSEL, 0, 0); return (s == -1) ? 0 : (int)SendMessage(c, CB_GETITEMDATA, s, 0); };
            int mID = gID(hCmbManuf), aID = gID(hCmbArch), mtID = gID(hCmbMemType), bID = gID(hCmbBus);
            double priceVal = atof(prc);

            if (editId == -1) {
                sqlite3_stmt* st; sqlite3_prepare_v2(db, "INSERT INTO GPU (model, manuf_id, arch_id, memtype_id, bus_id, price) VALUES (?,?,?,?,?,?)", -1, &st, NULL);
                sqlite3_bind_text(st, 1, mod, -1, SQLITE_TRANSIENT); sqlite3_bind_int(st, 2, mID); sqlite3_bind_int(st, 3, aID); sqlite3_bind_int(st, 4, mtID); sqlite3_bind_int(st, 5, bID);
                sqlite3_bind_double(st, 6, priceVal);
                sqlite3_step(st); sqlite3_finalize(st);
            }
            else {
                sqlite3_stmt* st; std::string sql = "UPDATE GPU SET model=?, manuf_id=?, arch_id=?, memtype_id=?, bus_id=?, price=? WHERE id=" + std::to_string(editId);
                sqlite3_prepare_v2(db, sql.c_str(), -1, &st, NULL);
                sqlite3_bind_text(st, 1, mod, -1, SQLITE_TRANSIENT); sqlite3_bind_int(st, 2, mID); sqlite3_bind_int(st, 3, aID); sqlite3_bind_int(st, 4, mtID); sqlite3_bind_int(st, 5, bID);
                sqlite3_bind_double(st, 6, priceVal);
                sqlite3_step(st); sqlite3_finalize(st);
                editId = -1; SetWindowText(hBtnAddGpu, L"Добавить");
            }
            SetWindowText(hModelEdit, L""); SetWindowText(hPriceEdit, L""); RefreshGpuList();
        }
        if (LOWORD(wParam) == ID_GPU_DEL) {
            int sel = (int)SendMessage(hGpuList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
            if (sel != -1 && MessageBox(hwnd, L"Удалить?", L"БД", MB_YESNO) == IDYES) {
                char idStr[16] = { 0 }; GetLvText(hGpuList, sel, 0, idStr, 16);
                ExecSQL(("DELETE FROM GPU WHERE id = " + std::string(idStr)).c_str()); RefreshGpuList();
            }
        } break;
    } return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// --- Главное окно ---

void OpenMgr(HWND parent, const char* tbl) {
    currentTable = tbl;
    WNDCLASS wcM = { 0 }; wcM.lpfnWndProc = MgrProc; wcM.lpszClassName = L"MgrWin"; wcM.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcM.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wcM);
    CreateWindow(L"MgrWin", L"Справочник", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 250, 250, 320, 340, parent, NULL, NULL, NULL);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: InitDatabase();
        CreateWindow(L"BUTTON", L"Видеокарты", WS_CHILD | WS_VISIBLE, 50, 30, 300, 45, hwnd, (HMENU)ID_BTN_GPU, NULL, NULL);
        CreateWindow(L"BUTTON", L"Справочники", WS_CHILD | WS_VISIBLE, 50, 90, 300, 45, hwnd, (HMENU)ID_BTN_MGR_MENU, NULL, NULL);
        CreateWindow(L"BUTTON", L"Выход", WS_CHILD | WS_VISIBLE, 50, 160, 300, 45, hwnd, (HMENU)ID_BTN_EXIT, NULL, NULL);
        break;
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == ID_BTN_EXIT) PostMessage(hwnd, WM_CLOSE, 0, 0);
        if (wmId == ID_BTN_GPU) {
            WNDCLASS wcG = { 0 }; wcG.lpfnWndProc = GpuProc; wcG.lpszClassName = L"GpuWin"; wcG.hCursor = LoadCursor(NULL, IDC_ARROW);
            wcG.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClass(&wcG);
            CreateWindow(L"GpuWin", L"Каталог", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 800, 400, hwnd, NULL, NULL, NULL);
        }
        if (wmId == ID_BTN_MGR_MENU) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_MENU_MANUF, L"Производители");
            AppendMenu(hMenu, MF_STRING, ID_MENU_ARCH, L"Архитектура");
            AppendMenu(hMenu, MF_STRING, ID_MENU_MEMTYPE, L"Тип памяти");
            AppendMenu(hMenu, MF_STRING, ID_MENU_BUS, L"Тип шины");
            RECT rc; GetWindowRect((HWND)lParam, &rc);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        if (wmId == ID_MENU_MANUF)  OpenMgr(hwnd, "Manufacturers");
        if (wmId == ID_MENU_ARCH)   OpenMgr(hwnd, "Architecture");
        if (wmId == ID_MENU_MEMTYPE) OpenMgr(hwnd, "MemoryType");
        if (wmId == ID_MENU_BUS)      OpenMgr(hwnd, "BusType");
        break;
    }
    case WM_DESTROY: if (db) sqlite3_close(db); PostQuitMessage(0); break;
    } return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hI, _In_opt_ HINSTANCE hP, _In_ LPWSTR pC, _In_ int nS) {
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = WindowProc; wc.hInstance = hI; wc.lpszClassName = L"MainWin";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    CreateWindow(L"MainWin", L"База данных GPU", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 100, 100, 420, 280, NULL, NULL, hI, NULL);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}