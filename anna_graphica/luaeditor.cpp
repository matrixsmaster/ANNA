/* Based on code from Qt help. Modified and extended by Dmitry 'MatrixS_Master' Solovyev, 2024 */
#include <QPainter>
#include <QTextBlock>
#include "luaeditor.h"
#include "lua_tables.h"

LuaEditor::LuaEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    lineNumberArea = new LineNumberArea(this);
    connect(this, &LuaEditor::blockCountChanged, this, &LuaEditor::updateLineNumberAreaWidth);
    connect(this, &LuaEditor::updateRequest, this, &LuaEditor::updateLineNumberArea);
    updateLineNumberAreaWidth(0);

    QFont font;
    font.setFamily("monospace");
    font.setFixedPitch(true);
    font.setPointSize(AG_LUAED_DEF_FONT);

    highlighter = new Highlighter(document());
}

LuaEditor::~LuaEditor()
{
    delete highlighter;
    delete lineNumberArea;
}

int LuaEditor::lineNumberAreaWidth()
{
    int digits = 1;
    int max = std::max(1,blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + 3;
}

void LuaEditor::updateLineNumberAreaWidth(int /* newBlockCount */)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void LuaEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) lineNumberArea->scroll(0, dy);
    else lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void LuaEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void LuaEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), Qt::lightGray);
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(Qt::black);
            painter.drawText(0, top, lineNumberArea->width(), fontMetrics().height(), Qt::AlignRight, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

Highlighter::Highlighter(QTextDocument *parent) : QSyntaxHighlighter(parent)
{
    for (auto &&i : lua_table) {
        HighlightingRule rule;
        QTextCharFormat fmt;

        fmt.setFontFamily("monospace");
        if (i.color != Qt::transparent) fmt.setForeground(i.color);
        if (i.bold) fmt.setFontWeight(QFont::Bold);
        fmt.setFontItalic(i.italic);

        for (int j = 0; i.exprs[j]; j++) {
            QString pat(i.exprs[j]);
            if (i.pad) pat = "\\b" + pat + "\\b";
            rule.pattern = QRegularExpression(pat);
            rule.format = fmt;
            highlightingRules.append(rule);
        }
    }
}

void Highlighter::highlightBlock(const QString &text)
{
    for (auto &&i : qAsConst(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = i.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), i.format);
        }
    }
}
