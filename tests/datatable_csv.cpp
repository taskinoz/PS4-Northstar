#include "northstar_ps4/datatable_csv.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace northstar::ps4::mods;

static void Check(bool ok, const char* what, int line) {
    if (!ok) { std::fprintf(stderr, "line %d: %s\n", line, what); std::exit(1); }
}
#define CHECK(x) Check((x), #x, __LINE__)

int main() {
    DatatableCsv table;
    std::string error;
    CHECK(ParseDatatableCsv("name,count,cost,asset\r\nalpha,7,1.5,ui/a\r\n"
        "\"comma, value\",0,-2.25,\"ui/\"\"quoted\"\"\"", table, error));
    CHECK(table.columns.size() == 4);
    CHECK(table.rows.size() == 2);
    CHECK(table.Column("cost") == 2);
    CHECK(table.Column("missing") == -1);
    CHECK(*table.Cell(0, 1) == "7");
    CHECK(*table.Cell(1, 0) == "comma, value");
    CHECK(*table.Cell(1, 3) == "ui/\"quoted\"");
    CHECK(table.Cell(-1, 0) == nullptr);
    CHECK(table.Cell(0, 9) == nullptr);

    const std::string bom = "\xef\xbb\xbfkey,value\nfirst,\nsecond,ok\n";
    CHECK(ParseDatatableCsv(bom, table, error));
    CHECK(table.columns[0] == "key");
    CHECK(table.rows.size() == 2);
    CHECK(table.rows[0][1].empty());

    CHECK(!ParseDatatableCsv("a,b\n\"unterminated,b", table, error));
    CHECK(error.find("unterminated") != std::string::npos);
    CHECK(!ParseDatatableCsv("a,b\n\"line\nfeed\",x", table, error));
    CHECK(error.find("newline") != std::string::npos);
    CHECK(!ParseDatatableCsv("", table, error));

    std::puts("datatable_csv tests passed");
    return 0;
}
