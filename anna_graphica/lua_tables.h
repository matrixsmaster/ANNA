/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#pragma once

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
        "math"
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
        "select","type"
};
