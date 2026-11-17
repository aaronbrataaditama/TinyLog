#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: SpreadsheetWriter.cpp (WP-D). Spec 7.2-7.6.
#include "export/ZipWriter.h"
#include "storage/SessionRecord.h"
#include "util/Status.h"

// Streams xl/worksheets/sheet1.xml into the ZipWriter. Begin() opens the zip entry; End() closes it.
class SheetStream {
public:
    explicit SheetStream(ZipWriter& zip) : zip_(zip) {}
    Status Begin();                                // XML decl, <worksheet>, frozen header pane, <cols>, <sheetData>, header row
    Status AddSession(const CompletedSession& s);  // one <row> with the six cells (see plan WP-D)
    Status End();                                  // </sheetData></worksheet>, EndEntry

private:
    ZipWriter& zip_;
    int        row_ = 1;   // last written row number
};

// [Content_Types].xml, _rels/.rels, xl/workbook.xml (one sheet "Sessions"), xl/_rels/workbook.xml.rels, xl/styles.xml
Status WriteStaticParts(ZipWriter& zip);
