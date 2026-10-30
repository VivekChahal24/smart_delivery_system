# Smart Delivery System

A native Windows desktop application for managing courier records and dispatching deliveries through a three-level priority queue. The program is written in C and uses the Win32 API and common controls to provide a graphical interface.

## Project Overview

The application keeps courier details in memory while it is running and saves the records to a local binary file. Users can add, find, filter, dispatch, complete, cancel, and remove courier records. A dashboard displays status totals, and the application can calculate delivery statistics and export a text report.

The record array is limited to **100 couriers**. Each courier record stores an ID, customer name, destination, weight, priority, status, and timestamps for creation, dispatch, and delivery.

## Features

- **Courier management:** Add records with a positive, unique ID; customer; destination; weight; and priority.
- **Priority-based dispatch:** Select the next pending courier in this order:
  1. Urgent
  2. Normal
  3. Low
- **FIFO within each priority:** Among couriers of the same priority, the earliest-added pending record is selected first.
- **Lifecycle controls:** Dispatch a pending courier, mark an in-transit courier as delivered, cancel a pending courier, or delete a completed/cancelled record.
- **Search and filtering:** Search by courier ID and filter the table by status.
- **Dashboard and statistics:** View status counts, priority totals, total and average weight, completion rate, and average delivery time for timed deliveries.
- **Text report export:** Write the courier table and calculated statistics to `delivery_report.txt`.
- **Persistent local records:** Load and save courier data in `smart_delivery_queue.dat`.

## Priority Queue: Main Logic

The queue is implemented by scanning priorities from highest to lowest, then scanning records in their stored order. The first matching pending record is dispatched:

```c
int nextPending(void)
{
    for (int p = 1; p <= 3; p++)
        for (int i = 0; i < courierCount; i++)
            if (!strcmp(couriers[i].status, "Pending") &&
                couriers[i].priority == p)
                return i;
    return -1;
}
```

Priority values are `1` for Urgent, `2` for Normal, and `3` for Low. Returning `-1` means there are no pending couriers.

## Delivery Workflow

```text
Add courier
    |
    v
  Pending -------> Cancelled
    |
    | Dispatch next by priority
    v
 In Transit
    |
    | Mark delivered
    v
 Delivered

Delivered or Cancelled records can be deleted.
```

## Build and Run

### Requirements

- Windows
- A C compiler with Windows SDK headers and libraries
- Windows common controls (`comctl32`) and GDI (`gdi32`)

### Microsoft C/C++ compiler

From a Visual Studio Developer Command Prompt in the project folder:

```bat
cl /nologo /O2 smart_delivery_system.c /link /SUBSYSTEM:WINDOWS comctl32.lib gdi32.lib
```

Then run:

```bat
smart_delivery_system.exe
```

The repository folder already contains a Windows executable, but rebuilding from the source is recommended when making code changes.

## Files

| File | Purpose |
|---|---|
| `smart_delivery_system.c` | Application source code: Win32 interface, courier operations, queue selection, persistence, statistics, and report export. |
| `smart_delivery_queue.dat` | Runtime binary store for courier records. Created or updated by the application. |
| `delivery_report.txt` | Runtime text export containing courier rows and summary statistics. |
| `smart_delivery_system.exe` | Prebuilt Windows application. |

The data and report files are generated runtime artifacts, not source files. They may contain customer information; **do not publish real records**. Exclude local copies from a public repository and provide sanitized sample data only if needed.

## Data and Reporting Notes

- The application creates or reads its data and report files in its current working directory.
- The queue file contains a format signature and record count before the saved records. The program checks the signature and record count when loading.
- The file stores C structures directly, so it is intended for use with this application build on Windows, not as a portable interchange format.
- The report is a snapshot produced when **Export Report** is selected; it is not updated continuously. Regenerate it after changing records.
- In the supplied project folder, the existing report snapshot lists **3 couriers**, while the saved queue file reports **4 records**. These local artifacts do not represent the same snapshot; export a fresh report before relying on its figures.

## Limitations

- The in-memory record capacity is fixed at 100.
- Records are stored locally in a binary file; there is no database, network service, or multi-user synchronization.
- The interface uses the Windows API and is not intended to run natively on macOS or Linux.
