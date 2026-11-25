#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(SHEETCORE_CAPI_BUILD_DLL)
#define SC_API __declspec(dllexport)
#elif defined(_WIN32) && defined(SHEETCORE_CAPI_USE_DLL)
#define SC_API __declspec(dllimport)
#else
#define SC_API
#endif

typedef struct sc_engine sc_engine_t;

typedef enum sc_status {
    SC_OK = 0,
    SC_ERR_INVALID_ARG = 1,
    SC_ERR_NOT_FOUND = 2,
    SC_ERR_ALREADY_EXISTS = 3,
    SC_ERR_PARSE = 4,
    SC_ERR_IO = 5,
    SC_ERR_INTERNAL = 6
} sc_status_t;

typedef enum sc_value_type {
    SC_VAL_EMPTY = 0,
    SC_VAL_NUMBER = 1,
    SC_VAL_STRING = 2,
    SC_VAL_BOOL = 3,
    SC_VAL_DATETIME = 4,
    SC_VAL_ERROR = 5
} sc_value_type_t;

SC_API sc_engine_t* sc_engine_create(void);
SC_API void sc_engine_destroy(sc_engine_t* engine);

SC_API sc_status_t sc_add_sheet(sc_engine_t* engine, const char* name, uint32_t* out_sheet_id);
SC_API sc_status_t sc_set_number(sc_engine_t* engine, uint32_t sheet_id, const char* a1, double value);
SC_API sc_status_t sc_set_string(sc_engine_t* engine, uint32_t sheet_id, const char* a1, const char* text);
SC_API sc_status_t sc_set_formula(sc_engine_t* engine, uint32_t sheet_id, const char* a1, const char* formula);

SC_API sc_value_type_t sc_get_value_type(const sc_engine_t* engine, uint32_t sheet_id, const char* a1);
SC_API sc_status_t sc_get_number(const sc_engine_t* engine, uint32_t sheet_id, const char* a1, double* out_value);
SC_API size_t sc_get_display_string(const sc_engine_t* engine, uint32_t sheet_id, const char* a1,
                                    char* buffer, size_t buffer_size);

SC_API sc_status_t sc_recalculate(sc_engine_t* engine);
SC_API sc_status_t sc_save(const sc_engine_t* engine, const char* path);
SC_API sc_status_t sc_load(sc_engine_t* engine, const char* path);

#ifdef __cplusplus
}
#endif
