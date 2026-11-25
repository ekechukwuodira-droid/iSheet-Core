#include "sheetcore/capi/sheetcore_capi.h"
#include "sheetcore/sheetcore.hpp"

#include <algorithm>
#include <cstring>

struct sc_engine {
    sheetcore::SheetCoreEngine impl;
};

namespace {

sc_status_t mapError(sheetcore::ErrorCode code) {
    switch (code) {
        case sheetcore::ErrorCode::Ok: return SC_OK;
        case sheetcore::ErrorCode::InvalidArgument: return SC_ERR_INVALID_ARG;
        case sheetcore::ErrorCode::NotFound: return SC_ERR_NOT_FOUND;
        case sheetcore::ErrorCode::AlreadyExists: return SC_ERR_ALREADY_EXISTS;
        case sheetcore::ErrorCode::ParseError: return SC_ERR_PARSE;
        case sheetcore::ErrorCode::IOError: return SC_ERR_IO;
        default: return SC_ERR_INTERNAL;
    }
}

} // namespace

extern "C" {

sc_engine_t* sc_engine_create(void) {
    return new sc_engine();
}

void sc_engine_destroy(sc_engine_t* engine) {
    delete engine;
}

sc_status_t sc_add_sheet(sc_engine_t* engine, const char* name, uint32_t* out_sheet_id) {
    if (!engine || !name) return SC_ERR_INVALID_ARG;
    auto res = engine->impl.workbook().addSheet(name);
    if (!res) return mapError(res.error().code);
    if (out_sheet_id) *out_sheet_id = res.value()->id();
    return SC_OK;
}

sc_status_t sc_set_number(sc_engine_t* engine, uint32_t sheet_id, const char* a1, double value) {
    if (!engine || !a1) return SC_ERR_INVALID_ARG;
    auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return SC_ERR_NOT_FOUND;
    auto res = ws->setValue(a1, value);
    return res ? SC_OK : mapError(res.error().code);
}

sc_status_t sc_set_string(sc_engine_t* engine, uint32_t sheet_id, const char* a1, const char* text) {
    if (!engine || !a1 || !text) return SC_ERR_INVALID_ARG;
    auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return SC_ERR_NOT_FOUND;
    auto res = ws->setValue(a1, std::string(text));
    return res ? SC_OK : mapError(res.error().code);
}

sc_status_t sc_set_formula(sc_engine_t* engine, uint32_t sheet_id, const char* a1, const char* formula) {
    if (!engine || !a1 || !formula) return SC_ERR_INVALID_ARG;
    auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return SC_ERR_NOT_FOUND;
    auto res = ws->setFormula(a1, formula);
    return res ? SC_OK : mapError(res.error().code);
}

sc_value_type_t sc_get_value_type(const sc_engine_t* engine, uint32_t sheet_id, const char* a1) {
    if (!engine || !a1) return SC_VAL_ERROR;
    const auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return SC_VAL_ERROR;
    auto v = ws->getValue(a1);
    if (sheetcore::isEmpty(v)) return SC_VAL_EMPTY;
    if (sheetcore::isNumber(v)) return SC_VAL_NUMBER;
    if (sheetcore::isText(v)) return SC_VAL_STRING;
    if (sheetcore::isBool(v)) return SC_VAL_BOOL;
    if (sheetcore::isDateTime(v)) return SC_VAL_DATETIME;
    return SC_VAL_ERROR;
}

sc_status_t sc_get_number(const sc_engine_t* engine, uint32_t sheet_id, const char* a1, double* out_value) {
    if (!engine || !a1 || !out_value) return SC_ERR_INVALID_ARG;
    const auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return SC_ERR_NOT_FOUND;
    auto num = sheetcore::toNumber(ws->getValue(a1));
    if (!num) return SC_ERR_INVALID_ARG;
    *out_value = *num;
    return SC_OK;
}

size_t sc_get_display_string(const sc_engine_t* engine, uint32_t sheet_id, const char* a1,
                             char* buffer, size_t buffer_size) {
    if (!engine || !a1) return 0;
    const auto* ws = engine->impl.workbook().getSheet(sheet_id);
    if (!ws) return 0;
    std::string s = sheetcore::toDisplayString(ws->getValue(a1));
    if (buffer && buffer_size > 0) {
        size_t copyLen = std::min(s.size(), buffer_size - 1);
        std::memcpy(buffer, s.data(), copyLen);
        buffer[copyLen] = '\0';
    }
    return s.size();
}

sc_status_t sc_recalculate(sc_engine_t* engine) {
    if (!engine) return SC_ERR_INVALID_ARG;
    engine->impl.recalculate();
    return SC_OK;
}

sc_status_t sc_save(const sc_engine_t* engine, const char* path) {
    if (!engine || !path) return SC_ERR_INVALID_ARG;
    auto res = engine->impl.save(path);
    return res ? SC_OK : mapError(res.error().code);
}

sc_status_t sc_load(sc_engine_t* engine, const char* path) {
    if (!engine || !path) return SC_ERR_INVALID_ARG;
    auto res = engine->impl.load(path);
    return res ? SC_OK : mapError(res.error().code);
}

} // extern "C"
