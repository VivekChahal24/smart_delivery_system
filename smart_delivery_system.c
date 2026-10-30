#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")

/* SMART DELIVERY SYSTEM
 * Array of structs + 3-level priority selection (Urgent > Normal > Low,
 * FIFO inside each level) + binary file storage.
 * Life cycle: Pending -> In Transit -> Delivered | Pending -> Cancelled */

#define MAX 100
#define DATA_FILE   "smart_delivery_queue.dat"
#define REPORT_FILE "delivery_report.txt"
#define SIGNATURE   "SDQUEUE2"

enum { BTN_ADD = 1, BTN_VIEW, BTN_SEARCH, BTN_NEXT, BTN_DELIVERED, BTN_STATS,
       BTN_CLEAR, BTN_EXIT, BTN_CANCEL, BTN_DELETE, BTN_EXPORT, IDC_FILTER };

#define HEADER_H 70
#define LEFT_X   15
#define LEFT_W   290
#define RIGHT_X  320
#define STATUS_H 28
#define COL_BG     RGB(240, 243, 248)
#define COL_HEADER RGB(25, 55, 109)

#define WARN(t) MessageBoxA(hMain, t, "Warning", MB_OK | MB_ICONWARNING)
#define INFO(t) MessageBoxA(hMain, t, "Info", MB_OK | MB_ICONINFORMATION)
#define OUT(t)  SetWindowTextA(hOutput, t)
#define DK(v)   ((v) > 35 ? (v) - 35 : 0)

typedef struct {
    int id;
    char customer[50];
    char destination[50];
    float weight;
    int priority;          /* 1 = Urgent, 2 = Normal, 3 = Low */
    char status[20];
    time_t created, dispatched, delivered;
} Courier;

Courier couriers[MAX];
int courierCount = 0;

HWND hId, hCustomer, hDestination, hWeight, hPriority;
HWND hList, hOutput, hStatus, hFilter, hListTitle, hFilterLabel, hOutLabel, hMain;
HINSTANCE hInstance;
HBRUSH hBgBrush, hHeaderBrush;
HFONT fNormal, fBold, fTitle, fMono;

/* ---------------- HELPERS ---------------- */

const char *priorityName(int p) { return p == 1 ? "Urgent" : p == 2 ? "Normal" : "Low"; }

void formatTime(time_t t, char *buf, size_t size)
{
    if (t == 0) snprintf(buf, size, "-");
    else strftime(buf, size, "%d-%m-%Y %H:%M:%S", localtime(&t));
}

COLORREF statusColor(const char *s)
{
    if (!strcmp(s, "Pending"))    return RGB(255, 243, 176);
    if (!strcmp(s, "In Transit")) return RGB(187, 222, 251);
    if (!strcmp(s, "Delivered"))  return RGB(200, 230, 201);
    return RGB(224, 224, 224); /* Cancelled */
}

/* button colors, indexed by button id */
static const COLORREF btnColors[] = {
    0, RGB(39, 174, 96), RGB(93, 109, 126), RGB(41, 128, 185),
    RGB(230, 126, 34), RGB(22, 160, 133), RGB(52, 73, 94),
    RGB(127, 140, 141), RGB(90, 90, 90), RGB(211, 84, 0),
    RGB(192, 57, 43), RGB(44, 130, 201)
};

HFONT makeFont(int height, int weight, const char *face)
{
    return CreateFontA(height, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       0, 0, CLEARTYPE_QUALITY, 0, face);
}

int countStatus(const char *s)
{
    int n = 0;
    for (int i = 0; i < courierCount; i++)
        if (!strcmp(couriers[i].status, s)) n++;
    return n;
}

/* Priority queue: next pending courier = lowest priority number, oldest first */
int nextPending(void)
{
    for (int p = 1; p <= 3; p++)
        for (int i = 0; i < courierCount; i++)
            if (!strcmp(couriers[i].status, "Pending") && couriers[i].priority == p)
                return i;
    return -1;
}

int findCourierById(int id)
{
    for (int i = 0; i < courierCount; i++)
        if (couriers[i].id == id) return i;
    return -1;
}

/* ---------------- FILE HANDLING ---------------- */

void saveData(void)
{
    FILE *fp = fopen(DATA_FILE, "wb");
    if (!fp) {
        MessageBoxA(hMain, "Unable to save records.", "File Error", MB_OK | MB_ICONERROR);
        return;
    }
    fwrite(SIGNATURE, 8, 1, fp);
    fwrite(&courierCount, sizeof(int), 1, fp);
    fwrite(couriers, sizeof(Courier), courierCount, fp);
    fclose(fp);
}

void loadData(void)
{
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) return;

    char sig[8];
    int n = 0;

    if (fread(sig, 8, 1, fp) != 1 || memcmp(sig, SIGNATURE, 8) ||
        fread(&n, sizeof(int), 1, fp) != 1 || n < 0 || n > MAX ||
        fread(couriers, sizeof(Courier), n, fp) != (size_t)n) {
        MessageBoxA(NULL, "Saved file is invalid or from an old version.\n"
                    "Starting with an empty record list.",
                    "File Warning", MB_OK | MB_ICONWARNING);
        n = 0;
    }
    courierCount = n;
    fclose(fp);
}

/* ---------------- GUI HELPERS ---------------- */

HWND makeControl(const char *type, const char *text, DWORD style,
                 int x, int y, int w, int h, HMENU id)
{
    return CreateWindowExA(0, type, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, w, h, hMain, id, hInstance, NULL);
}

void addButton(const char *text, int id, int x, int y, int w)
{
    makeControl("BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, 34, (HMENU)(INT_PTR)id);
}

void clearInputs(void)
{
    SetWindowTextA(hId, "");
    SetWindowTextA(hCustomer, "");
    SetWindowTextA(hDestination, "");
    SetWindowTextA(hWeight, "");
    SendMessageA(hPriority, CB_SETCURSEL, 1, 0);
}

BOOL CALLBACK setFontProc(HWND child, LPARAM font)
{
    SendMessageA(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

/* ---------------- HEADER + DASHBOARD CARDS ---------------- */

void cardRect(int i, RECT *r)
{
    RECT rc;
    GetClientRect(hMain, &rc);
    int cardW = (rc.right - 15 - RIGHT_X - 36) / 4;

    r->left = RIGHT_X + i * (cardW + 12);
    r->top = HEADER_H + 15;
    r->right = r->left + cardW;
    r->bottom = r->top + 70;
}

void invalidateCards(void)
{
    RECT a, b;
    cardRect(0, &a);
    cardRect(3, &b);
    a.right = b.right;
    InvalidateRect(hMain, &a, TRUE);
}

void paintHeaderAndCards(HDC dc)
{
    RECT rc;
    GetClientRect(hMain, &rc);

    RECT hr = {0, 0, rc.right, HEADER_H};
    FillRect(dc, &hr, hHeaderBrush);

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, fTitle);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutA(dc, 25, 17, "SMART DELIVERY SYSTEM", 21);

    const char *labels[4] = {"Total Couriers", "Pending", "In Transit", "Delivered"};
    int counts[4] = {courierCount, countStatus("Pending"),
                     countStatus("In Transit"), countStatus("Delivered")};
    COLORREF accents[4] = {RGB(52, 73, 94), RGB(230, 126, 34),
                           RGB(41, 128, 185), RGB(39, 174, 96)};

    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH border = CreateSolidBrush(RGB(215, 221, 232));

    for (int i = 0; i < 4; i++) {
        RECT r;
        cardRect(i, &r);
        FillRect(dc, &r, white);
        FrameRect(dc, &r, border);

        RECT strip = {r.left, r.top, r.left + 6, r.bottom};
        HBRUSH accent = CreateSolidBrush(accents[i]);
        FillRect(dc, &strip, accent);
        DeleteObject(accent);

        char num[20];
        snprintf(num, sizeof(num), "%d", counts[i]);

        SelectObject(dc, fTitle);
        SetTextColor(dc, accents[i]);
        TextOutA(dc, r.left + 20, r.top + 6, num, (int)strlen(num));

        SelectObject(dc, fNormal);
        SetTextColor(dc, RGB(100, 110, 125));
        TextOutA(dc, r.left + 20, r.top + 44, labels[i], (int)strlen(labels[i]));
    }

    DeleteObject(white);
    DeleteObject(border);
}

/* ---------------- LAYOUT ---------------- */

void layoutControls(void)
{
    if (!hList || !hOutput) return;

    RECT rc;
    GetClientRect(hMain, &rc);

    int w = rc.right, rightW = w - 15 - RIGHT_X;
    int statusY = rc.bottom - STATUS_H;
    int outputY = statusY - 10 - 150;
    int outLabelY = outputY - 26;
    int listY = HEADER_H + 15 + 70 + 40;
    int listH = outLabelY - 8 - listY;
    if (listH < 100) listH = 100;

    MoveWindow(hListTitle, RIGHT_X, listY - 26, 200, 22, TRUE);
    MoveWindow(hFilterLabel, w - 220, listY - 26, 50, 22, TRUE);
    MoveWindow(hFilter, w - 165, listY - 30, 150, 160, TRUE);
    MoveWindow(hList, RIGHT_X, listY, rightW, listH, TRUE);
    MoveWindow(hOutLabel, RIGHT_X, outLabelY, 300, 22, TRUE);
    MoveWindow(hOutput, RIGHT_X, outputY, rightW, 150, TRUE);
    MoveWindow(hStatus, 0, statusY, w, STATUS_H, TRUE);
}

/* ---------------- TABLE ---------------- */

void refreshTable(void)
{
    static const char *filters[] = {"All", "Pending", "In Transit", "Delivered", "Cancelled"};
    int filter = (int)SendMessageA(hFilter, CB_GETCURSEL, 0, 0);

    ListView_DeleteAllItems(hList);

    for (int i = 0; i < courierCount; i++) {
        Courier *c = &couriers[i];
        if (filter > 0 && strcmp(c->status, filters[filter]) != 0)
            continue;

        char text[100];
        LVITEMA item = {0};
        snprintf(text, sizeof(text), "%d", c->id);
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(hList);
        item.lParam = i;
        item.pszText = text;
        int row = ListView_InsertItem(hList, &item);

        ListView_SetItemText(hList, row, 1, c->customer);
        ListView_SetItemText(hList, row, 2, c->destination);
        snprintf(text, sizeof(text), "%.2f", c->weight);
        ListView_SetItemText(hList, row, 3, text);
        ListView_SetItemText(hList, row, 4, (LPSTR)priorityName(c->priority));
        ListView_SetItemText(hList, row, 5, c->status);
        formatTime(c->created, text, sizeof(text));
        ListView_SetItemText(hList, row, 6, text);
    }

    char s[150];
    snprintf(s, sizeof(s), "   Total couriers: %d   |   Pending in queue: %d   |   Showing: %d",
             courierCount, countStatus("Pending"), ListView_GetItemCount(hList));
    SetWindowTextA(hStatus, s);
    invalidateCards();
}

int selectedCourier(void)
{
    int row = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (row == -1) {
        WARN("Select a courier from the table first.");
        return -1;
    }
    char id[30];
    ListView_GetItemText(hList, row, 0, id, sizeof(id));
    return findCourierById(atoi(id));
}

/* ---------------- ACTIONS ---------------- */

void addCourier(void)
{
    if (courierCount >= MAX) { WARN("Maximum 100 couriers allowed."); return; }

    char idText[30], customer[50], destination[50], weightText[30];
    GetWindowTextA(hId, idText, sizeof(idText));
    GetWindowTextA(hCustomer, customer, sizeof(customer));
    GetWindowTextA(hDestination, destination, sizeof(destination));
    GetWindowTextA(hWeight, weightText, sizeof(weightText));

    if (!idText[0] || !customer[0] || !destination[0] || !weightText[0]) {
        WARN("Please fill in every field.");
        return;
    }

    char *end;
    long id = strtol(idText, &end, 10);
    if (*end || id <= 0 || id > 2147483647L) { WARN("Enter a valid positive courier ID."); return; }
    if (findCourierById((int)id) != -1)      { WARN("This courier ID already exists."); return; }

    float weight = strtof(weightText, &end);
    if (*end || weight <= 0 || weight > 1000000.0f) {
        WARN("Enter a valid weight greater than zero.");
        return;
    }

    int sel = (int)SendMessageA(hPriority, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) { WARN("Select a priority."); return; }

    Courier *c = &couriers[courierCount++];
    memset(c, 0, sizeof(Courier));
    c->id = (int)id;
    strcpy(c->customer, customer);
    strcpy(c->destination, destination);
    c->weight = weight;
    c->priority = sel + 1;
    strcpy(c->status, "Pending");
    c->created = time(NULL);

    saveData();
    refreshTable();
    clearInputs();
    INFO("Courier added successfully!\nStatus: Pending");
}

void viewCouriers(void)
{
    SendMessageA(hFilter, CB_SETCURSEL, 0, 0);
    refreshTable();
    OUT("All courier records are displayed in the table.\r\n\r\n"
        "Colors:\r\nYellow = Pending\r\nBlue   = In Transit\r\n"
        "Green  = Delivered\r\nGrey   = Cancelled");
}

void searchCourier(void)
{
    char idText[30], *end;
    GetWindowTextA(hId, idText, sizeof(idText));

    if (!idText[0]) { WARN("Enter the Courier ID in the ID field."); return; }

    long id = strtol(idText, &end, 10);
    if (*end || id <= 0 || id > 2147483647L) { WARN("Enter a valid courier ID."); return; }

    int index = findCourierById((int)id);
    if (index == -1) { OUT("Courier not found."); return; }

    Courier *c = &couriers[index];
    char t1[40], t2[40], t3[40], out[1000];
    formatTime(c->created, t1, sizeof(t1));
    formatTime(c->dispatched, t2, sizeof(t2));
    formatTime(c->delivered, t3, sizeof(t3));

    snprintf(out, sizeof(out),
             "COURIER DETAILS\r\n\r\n"
             "Courier ID  : %d\r\nCustomer    : %s\r\nDestination : %s\r\n"
             "Weight      : %.2f KG\r\nPriority    : %d (%s)\r\nStatus      : %s\r\n\r\n"
             "Created     : %s\r\nDispatched  : %s\r\nDelivered   : %s",
             c->id, c->customer, c->destination, c->weight,
             c->priority, priorityName(c->priority), c->status, t1, t2, t3);
    OUT(out);

    /* show all rows, then highlight the found one */
    if (SendMessageA(hFilter, CB_GETCURSEL, 0, 0) != 0) {
        SendMessageA(hFilter, CB_SETCURSEL, 0, 0);
        refreshTable();
    }
    for (int row = 0; row < ListView_GetItemCount(hList); row++) {
        char rowId[30];
        ListView_GetItemText(hList, row, 0, rowId, sizeof(rowId));
        if (atoi(rowId) == (int)id) {
            ListView_SetItemState(hList, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(hList, row, FALSE);
            break;
        }
    }
}

void deliverNext(void)
{
    int index = nextPending();
    if (index == -1) { INFO("No pending couriers in the queue."); return; }

    Courier *c = &couriers[index];
    char msg[400];
    snprintf(msg, sizeof(msg),
             "Next courier in PRIORITY queue:\n\nID: %d\nCustomer: %s\n"
             "Destination: %s\nPriority: %s\n\nMove this courier to In Transit?",
             c->id, c->customer, c->destination, priorityName(c->priority));

    if (MessageBoxA(hMain, msg, "Confirm Dispatch", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    strcpy(c->status, "In Transit");
    c->dispatched = time(NULL);
    saveData();
    refreshTable();

    snprintf(msg, sizeof(msg),
             "COURIER DISPATCHED\r\n\r\nCourier ID  : %d\r\nCustomer    : %s\r\n"
             "Destination : %s\r\nPriority    : %s\r\nStatus      : In Transit\r\n\r\n"
             "Use Mark Delivered after the courier arrives.",
             c->id, c->customer, c->destination, priorityName(c->priority));
    OUT(msg);
}

void markDelivered(void)
{
    int index = selectedCourier();
    if (index == -1) return;

    Courier *c = &couriers[index];
    if (strcmp(c->status, "In Transit") != 0) {
        WARN("Select a courier whose status is In Transit.");
        return;
    }

    strcpy(c->status, "Delivered");
    c->delivered = time(NULL);
    saveData();
    refreshTable();

    char msg[400];
    snprintf(msg, sizeof(msg),
             "DELIVERY COMPLETED SUCCESSFULLY\r\n\r\nCourier ID   : %d\r\n"
             "Customer     : %s\r\nDestination  : %s\r\nFinal Status : Delivered",
             c->id, c->customer, c->destination);
    OUT(msg);
}

void cancelCourier(void)
{
    int index = selectedCourier();
    if (index == -1) return;

    Courier *c = &couriers[index];
    if (strcmp(c->status, "Pending") != 0) { WARN("Only Pending couriers can be cancelled."); return; }

    if (MessageBoxA(hMain, "Cancel this courier?", "Confirm Cancel",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    strcpy(c->status, "Cancelled");
    saveData();
    refreshTable();
    OUT("Courier cancelled and removed from the queue.");
}

void deleteCourier(void)
{
    int index = selectedCourier();
    if (index == -1) return;

    Courier *c = &couriers[index];
    if (strcmp(c->status, "Delivered") && strcmp(c->status, "Cancelled")) {
        WARN("Only Delivered or Cancelled records can be deleted.\n"
             "Cancel a Pending courier first.");
        return;
    }

    if (MessageBoxA(hMain, "Delete this record permanently?", "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) != IDYES)
        return;

    for (int i = index; i < courierCount - 1; i++)
        couriers[i] = couriers[i + 1];
    courierCount--;

    saveData();
    refreshTable();
    OUT("Record deleted.");
}

/* ---------------- STATISTICS + EXPORT ---------------- */

void buildStats(char *out, size_t size)
{
    int byPriority[4] = {0}, timed = 0;
    double totalWeight = 0, totalSec = 0;

    for (int i = 0; i < courierCount; i++) {
        Courier *c = &couriers[i];
        totalWeight += c->weight;
        byPriority[c->priority]++;
        if (!strcmp(c->status, "Delivered") && c->dispatched && c->delivered) {
            totalSec += difftime(c->delivered, c->dispatched);
            timed++;
        }
    }

    int delivered = countStatus("Delivered");
    double n = courierCount;
    double avgSec = timed ? totalSec / timed : 0;

    snprintf(out, size,
             "SMART DELIVERY STATISTICS\r\n==========================\r\n\r\n"
             "Total Couriers: %d\r\nPending: %d\r\nIn Transit: %d\r\n"
             "Delivered: %d\r\nCancelled: %d\r\n\r\n"
             "Urgent (1): %d\r\nNormal (2): %d\r\nLow (3): %d\r\n\r\n"
             "Total Weight: %.2f KG\r\nAverage Weight: %.2f KG\r\n"
             "Completion Rate: %.1f%%\r\nAvg Delivery Time: %.0f sec (%.2f min)",
             courierCount, countStatus("Pending"), countStatus("In Transit"),
             delivered, countStatus("Cancelled"),
             byPriority[1], byPriority[2], byPriority[3],
             totalWeight, n ? totalWeight / n : 0, n ? delivered * 100.0 / n : 0,
             avgSec, avgSec / 60.0);
}

void statistics(void)
{
    char out[1500];
    buildStats(out, sizeof(out));
    OUT(out);
}

void exportReport(void)
{
    FILE *fp = fopen(REPORT_FILE, "wb");
    if (!fp) {
        MessageBoxA(hMain, "Unable to create the report file.", "Export Error", MB_OK | MB_ICONERROR);
        return;
    }

    fprintf(fp, "SMART DELIVERY SYSTEM - REPORT\r\n==============================\r\n\r\n");
    fprintf(fp, "%-6s %-18s %-18s %9s %-8s %-11s\r\n",
            "ID", "Customer", "Destination", "Weight", "Priority", "Status");
    fprintf(fp, "------------------------------------------------------------------------\r\n");

    for (int i = 0; i < courierCount; i++) {
        Courier *c = &couriers[i];
        fprintf(fp, "%-6d %-18s %-18s %9.2f %-8s %-11s\r\n", c->id, c->customer,
                c->destination, c->weight, priorityName(c->priority), c->status);
    }

    char stats[1500];
    buildStats(stats, sizeof(stats));
    fprintf(fp, "\r\n%s\r\n", stats);
    fclose(fp);

    INFO("Report saved as " REPORT_FILE "\nin the program folder.");
}

/* ---------------- CREATE GUI ---------------- */

void createInterface(void)
{
    fNormal = makeFont(-15, FW_NORMAL, "Segoe UI");
    fBold   = makeFont(-15, FW_BOLD, "Segoe UI");
    fTitle  = makeFont(-30, FW_BOLD, "Segoe UI");
    fMono   = makeFont(-14, FW_NORMAL, "Consolas");

    /* Left panel: details */
    makeControl("BUTTON", "Courier Details", BS_GROUPBOX, LEFT_X, HEADER_H + 15, LEFT_W, 215, NULL);

    const char *labels[] = {"Courier ID:", "Customer:", "Destination:", "Weight (KG):"};
    HWND *fields[] = {&hId, &hCustomer, &hDestination, &hWeight};
    int fy = HEADER_H + 15 + 30;

    for (int i = 0; i < 4; i++, fy += 36) {
        makeControl("STATIC", labels[i], 0, 30, fy + 3, 90, 22, NULL);
        *fields[i] = makeControl("EDIT", "", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                                 125, fy, 165, 26, NULL);
    }

    makeControl("STATIC", "Priority:", 0, 30, fy + 3, 90, 22, NULL);
    hPriority = makeControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                            125, fy, 165, 120, NULL);
    SendMessageA(hPriority, CB_ADDSTRING, 0, (LPARAM)"1 - Urgent");
    SendMessageA(hPriority, CB_ADDSTRING, 0, (LPARAM)"2 - Normal");
    SendMessageA(hPriority, CB_ADDSTRING, 0, (LPARAM)"3 - Low");
    SendMessageA(hPriority, CB_SETCURSEL, 1, 0);

    /* Left panel: buttons */
    int groupY = HEADER_H + 15 + 215 + 10;
    makeControl("BUTTON", "Actions", BS_GROUPBOX, LEFT_X, groupY, LEFT_W, 285, NULL);

    static const struct { const char *text; int id; } btns[] = {
        {"Add Courier", BTN_ADD},       {"Search ID", BTN_SEARCH},
        {"Next Courier", BTN_NEXT},     {"Mark Delivered", BTN_DELIVERED},
        {"Cancel Courier", BTN_CANCEL}, {"Delete Record", BTN_DELETE},
        {"Statistics", BTN_STATS},      {"Export Report", BTN_EXPORT},
        {"View All", BTN_VIEW},         {"Clear Fields", BTN_CLEAR}
    };
    int by = groupY + 28;

    for (int i = 0; i < 10; i++)
        addButton(btns[i].text, btns[i].id, i % 2 ? 166 : 30, by + 42 * (i / 2), 128);

    addButton("Exit", BTN_EXIT, 30, by + 42 * 5, 264);

    /* Right panel */
    hListTitle   = makeControl("STATIC", "COURIER RECORDS", 0, RIGHT_X, 0, 200, 22, NULL);
    hFilterLabel = makeControl("STATIC", "Show:", 0, 0, 0, 50, 22, NULL);
    hFilter = makeControl("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                          0, 0, 150, 160, (HMENU)(INT_PTR)IDC_FILTER);

    const char *filters[] = {"All", "Pending", "In Transit", "Delivered", "Cancelled"};
    for (int i = 0; i < 5; i++)
        SendMessageA(hFilter, CB_ADDSTRING, 0, (LPARAM)filters[i]);
    SendMessageA(hFilter, CB_SETCURSEL, 0, 0);

    hList = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                            LVS_SHOWSELALWAYS | WS_TABSTOP,
                            0, 0, 100, 100, hMain, NULL, hInstance, NULL);
    ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    const char *headers[] = {"ID", "Customer", "Destination", "Weight", "Priority", "Status", "Created"};
    int widths[] = {55, 125, 140, 75, 75, 95, 160};

    for (int i = 0; i < 7; i++) {
        LVCOLUMNA col = {0};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPSTR)headers[i];
        col.cx = widths[i];
        ListView_InsertColumn(hList, i, &col);
    }

    hOutLabel = makeControl("STATIC", "OUTPUT / STATISTICS", 0, RIGHT_X, 0, 300, 22, NULL);
    hOutput = makeControl("EDIT", "Welcome to Smart Delivery System.",
                          WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
                          0, 0, 100, 100, NULL);
    hStatus = makeControl("STATIC", "Ready", SS_LEFT | SS_CENTERIMAGE, 0, 0, 100, STATUS_H, NULL);

    EnumChildWindows(hMain, setFontProc, (LPARAM)fNormal);
    SendMessageA(hOutput, WM_SETFONT, (WPARAM)fMono, TRUE);
    SendMessageA(hListTitle, WM_SETFONT, (WPARAM)fBold, TRUE);
    SendMessageA(hOutLabel, WM_SETFONT, (WPARAM)fBold, TRUE);

    layoutControls();
    refreshTable();
}

/* ---------------- WINDOW PROCEDURE ---------------- */

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case BTN_ADD:       addCourier();    break;
        case BTN_VIEW:      viewCouriers();  break;
        case BTN_SEARCH:    searchCourier(); break;
        case BTN_NEXT:      deliverNext();   break;
        case BTN_DELIVERED: markDelivered(); break;
        case BTN_STATS:     statistics();    break;
        case BTN_CLEAR:     clearInputs();   break;
        case BTN_CANCEL:    cancelCourier(); break;
        case BTN_DELETE:    deleteCourier(); break;
        case BTN_EXPORT:    exportReport();  break;
        case BTN_EXIT:      DestroyWindow(hwnd); break;
        case IDC_FILTER:
            if (HIWORD(wp) == CBN_SELCHANGE) refreshTable();
            break;
        }
        return 0;

    case WM_SIZE:
        layoutControls();
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = 1100;
        info->ptMinTrackSize.y = 760;
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (fTitle) paintHeaderAndCards(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc = (HDC)wp;
        HWND control = (HWND)lp;

        if (control == hStatus) {
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, COL_HEADER);
            return (LRESULT)hHeaderBrush;
        }
        if (control == hOutput) {
            SetTextColor(dc, RGB(30, 30, 30));
            SetBkColor(dc, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        SetTextColor(dc, RGB(40, 50, 70));
        SetBkColor(dc, COL_BG);
        return (LRESULT)hBgBrush;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT item = (LPDRAWITEMSTRUCT)lp;
        if (item->CtlType != ODT_BUTTON) break;

        COLORREF color = btnColors[item->CtlID];
        if (item->itemState & ODS_SELECTED)
            color = RGB(DK(GetRValue(color)), DK(GetGValue(color)), DK(GetBValue(color)));

        FillRect(item->hDC, &item->rcItem, hBgBrush);

        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush);
        HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
                  item->rcItem.right, item->rcItem.bottom, 10, 10);
        SelectObject(item->hDC, oldBrush);
        SelectObject(item->hDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);

        char text[64];
        GetWindowTextA(item->hwndItem, text, sizeof(text));
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, RGB(255, 255, 255));
        SelectObject(item->hDC, fBold);

        RECT tr = item->rcItem;
        DrawTextA(item->hDC, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (item->itemState & ODS_FOCUS) {
            RECT focus = item->rcItem;
            InflateRect(&focus, -4, -4);
            DrawFocusRect(item->hDC, &focus);
        }
        return TRUE;
    }

    case WM_NOTIFY: {
        LPNMHDR header = (LPNMHDR)lp;

        /* color every row by its status */
        if (header->hwndFrom == hList && header->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW draw = (LPNMLVCUSTOMDRAW)lp;

            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;

            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int i = (int)draw->nmcd.lItemlParam;
                if (i >= 0 && i < courierCount) {
                    draw->clrTextBk = statusColor(couriers[i].status);
                    draw->clrText = RGB(0, 0, 0);
                }
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        break;
    }

    case WM_DESTROY:
        saveData();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---------------- MAIN ---------------- */

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmd, int show)
{
    (void)previous;
    (void)cmd;
    hInstance = instance;

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    hBgBrush = CreateSolidBrush(COL_BG);
    hHeaderBrush = CreateSolidBrush(COL_HEADER);

    loadData();

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = instance;
    wc.lpszClassName = "SmartDeliveryQueueWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = hBgBrush;
    if (!RegisterClassA(&wc)) return 1;

    hMain = CreateWindowExA(0, wc.lpszClassName,
                            "Smart Delivery System - Priority Queue Management",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            50, 20, 1200, 800, NULL, NULL, instance, NULL);
    if (!hMain) return 1;

    createInterface();
    ShowWindow(hMain, show);
    UpdateWindow(hMain);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageA(hMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return (int)msg.wParam;
}