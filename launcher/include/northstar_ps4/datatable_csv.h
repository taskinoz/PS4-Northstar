#pragma once

#include <cstddef>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

namespace northstar::ps4::mods {

struct DatatableCsv {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;

    int Column(const char* name) const noexcept {
        if (!name) return -1;
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (columns[i] == name) return static_cast<int>(i);
        return -1;
    }

    const std::string* Cell(int row, int column) const noexcept {
        if (row < 0 || column < 0 || static_cast<std::size_t>(row) >= rows.size()) return nullptr;
        const auto& values = rows[static_cast<std::size_t>(row)];
        if (static_cast<std::size_t>(column) >= values.size()) return nullptr;
        return &values[static_cast<std::size_t>(column)];
    }
};

struct DatatableVector { float x, y, z; };

inline bool ParseDatatableVector(const std::string& text, DatatableVector& out) noexcept {
    const char* cursor = text.c_str();
    if (*cursor++ != '<') return false;
    float values[3]{};
    for (int component = 0; component < 3; ++component) {
        char* end = nullptr;
        errno = 0;
        values[component] = std::strtof(cursor, &end);
        if (errno || end == cursor) return false;
        cursor = end;
        if (component < 2) {
            if (*cursor++ != ',') return false;
        } else if (*cursor++ != '>' || *cursor != '\0') return false;
    }
    out = {values[0], values[1], values[2]};
    return true;
}

// Northstar's disk datatables are ordinary CSV files. Newlines inside quoted
// fields are rejected to match the PC loader; commas, CRLF, empty fields and
// doubled quotes inside a quoted field are supported.
inline bool ParseDatatableCsv(const char* data, std::size_t size, DatatableCsv& out,
    std::string& error) {
    out = {};
    error.clear();
    if (!data || size == 0) { error = "datatable is empty"; return false; }

    std::size_t pos = 0;
    if (size >= 3 && static_cast<unsigned char>(data[0]) == 0xef &&
        static_cast<unsigned char>(data[1]) == 0xbb &&
        static_cast<unsigned char>(data[2]) == 0xbf) pos = 3;

    std::vector<std::string> row;
    std::string field;
    bool quoted = false, afterQuote = false, sawAny = false, fieldStarted = false;
    auto finishField = [&]() {
        row.push_back(field);
        field.clear();
        afterQuote = false;
        fieldStarted = false;
    };
    auto finishRow = [&]() {
        finishField();
        if (out.columns.empty()) out.columns = row;
        else out.rows.push_back(row);
        row.clear();
        sawAny = false;
    };

    for (; pos < size; ++pos) {
        const char ch = data[pos];
        if (ch == '\0') { error = "datatable contains a NUL byte"; return false; }
        if (quoted) {
            if (ch == '"') {
                if (pos + 1 < size && data[pos + 1] == '"') { field.push_back('"'); ++pos; }
                else { quoted = false; afterQuote = true; }
            } else if (ch == '\r' || ch == '\n') {
                error = "unexpected newline in quoted datatable field";
                return false;
            } else field.push_back(ch);
            sawAny = true;
            continue;
        }
        if (afterQuote && ch != ',' && ch != '\r' && ch != '\n') {
            error = "unexpected text after closing quote";
            return false;
        }
        if (ch == '"') {
            if (fieldStarted) { error = "unexpected quote in unquoted field"; return false; }
            quoted = true;
            fieldStarted = true;
            sawAny = true;
        } else if (ch == ',') {
            finishField();
            sawAny = true;
        } else if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && pos + 1 < size && data[pos + 1] == '\n') ++pos;
            finishRow();
        } else {
            field.push_back(ch);
            fieldStarted = true;
            sawAny = true;
        }
    }
    if (quoted) { error = "unterminated quoted datatable field"; return false; }
    if (sawAny || !field.empty() || !row.empty() || afterQuote) finishRow();
    if (out.columns.empty()) { error = "datatable has no columns"; return false; }
    return true;
}

inline bool ParseDatatableCsv(const std::string& text, DatatableCsv& out, std::string& error) {
    return ParseDatatableCsv(text.data(), text.size(), out, error);
}

} // namespace northstar::ps4::mods
