/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#pragma once

#include <qnamespace.h>

struct LuaEditorHighlight {
    const char** exprs;
    bool pad;
    Qt::GlobalColor color;
    bool bold, italic;
};

static const char* lua_keywords[] = {
    "function",
    "if", "then", "else", "elseif",
    "while", "do",
    "repeat", "until",
    "for", "in",
    "goto",
    "end",
    "break",
    "return",
    "not", "and", "or",
    "coroutine",
    "table",
    "string",
    "bit32",
    "math",
    NULL
};

static const char* lua_indent[] = {
    "then", "else", "do"
};

static const char* lua_types[] = {
    "local", "type",
    "nil", "false", "true",
    "_ENV","_VERSION",
    "print","tostring","tonumber",
    "assert","error","collectgarbage",
    "getmetatable","setmetatable","next","pairs","ipairs",
    "pcall","xpcall",
    "rawequal","rawget","rawlen","rawset",
    "select","type",
    NULL
};

static const char* lua_quotes[] = {
    "\"[^\"]*\"",
    NULL
};

static const char* lua_funcs[] = {
    "\\b[A-Za-z0-9_]+(?=\\()",
    NULL
};

static const char* lua_comments[] = {
    "--[^\n]*",
    NULL
};

static const LuaEditorHighlight lua_table[] = {
    { lua_keywords, true, Qt::darkBlue, true, false },
    { lua_types, true, Qt::darkGreen, true, false },
    { lua_quotes, false, Qt::red, false, false },
    { lua_funcs, false, Qt::transparent, true, false },
    { lua_comments, false, Qt::blue, false, true, }
};
