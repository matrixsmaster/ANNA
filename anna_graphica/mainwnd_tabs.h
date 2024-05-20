#pragma once

#include "mainwnd.h"

static const char* filetype_names[ANNA_NUM_FILETYPES] = {
    "LLM",
    "prompt",
    "dialog as text",
    "dialog as MD",
    "dialog as HTML",
    "model state",
    "CLiP encoder",
    "attachment",
    "logit biases",
};

static const char* filetype_filters[ANNA_NUM_FILETYPES] = {
    "Supported files (*.gguf *.dummy *.lscs);;GGUF files (*.gguf);;Dummy files (*.dummy);;LSCS files (*.lscs);;All files (*.*)",
    "Text files (*.txt);;All files (*.*)",
    "Text files (*.txt)",
    "Markdown files (*.md);;Text files (*.txt)",
    "HTML files (*.html *.htm)",
    "ANNA save states (*.anna);;All files (*.*)",
    "GGUF files (*.gguf);;All files (*.*)",
    "Image files (*.png *.jpg *.jpeg *.bmp *.xpm *.ppm *.pbm *.pgm *.xbm *.xpm);;Text files (*.txt);;All files (*.*)",
    "Logit bias files (*.ini);;All files (*.*)",
};

static const char* filetype_defaults[ANNA_NUM_FILETYPES] = {
    ".gguf",
    ".txt",
    ".txt",
    ".md",
    ".html",
    ".anna",
    ".gguf",
    ".txt",
    ".ini",
};

static const char* md_fix_in_tab[] = {
    "\\n\\*\\*([^*\\n]+\\s?)\\n", "\n**\\1**\n**",
    "([^\\n])\\n([^\\n])", "\\1\n\n\\2",
    "([^\\\\])#", "\\1\\#",
    "\\n\\*\\*[ \t]+", "\n**",
    "([^\\\\]|^)<", "\\1\\<",
    "\\n[*]{4}\\n", "\n\n",
    NULL, NULL // terminator
};

static const char* md_fix_out_tab[] = {
    "([^\\n])\\n([^\\n])", "\\1\n\n\\2",
    "([^\\\\]|^)<", "\\1\\<",
    NULL, NULL // terminator
};
